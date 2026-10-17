/**
 * Minimal H.264 streamer for FH8852V201 + CV2003 — majestic replacement.
 *
 * No vendor SDK linkage (there's no .so to link against on this chip —
 * see ../PORTING_GUIDE.md). Every call here is either a direct ioctl on
 * the kernel modules, or one of the clean-room libraries in sdk/lib/.
 *
 * VERIFICATION STATUS BY SECTION — read this before trusting any one part:
 *
 *   PROVEN ON REAL HARDWARE: device opens, /proc/driver config writes,
 *     VMM allocation, VPSS setup. Ported verbatim from
 *     sdk/tests/archive/test_minimal_regs.c, which got MIPI frames flowing
 *     at 1920x1080@25fps.
 *
 *   BYTE-EXACT TO VENDOR, NOT YET HARDWARE-TESTED WITH THESE VALUES: CV2003
 *     sensor bring-up and the MIPI ioctls (sdk/lib/cv2003_driver.c), ported
 *     from ipcam's cv2003_mipi_sensor_if (Sensor_Init/SetSensorFmt/
 *     mipi_init/SetIntt/SetGain). The MIPI payloads changed from the
 *     hardware-tested test_minimal_regs.c values ({0,8,1}, {0,1,0,0,0,0})
 *     to the vendor's ({0,9,1}, {0,0,0,1,0,0}), now issued from inside
 *     sensor set-format like the vendor does. Bus sequence pinned by
 *     sdk/tests/test_cv2003_bus.c. See README "CV2003 Init Sequence".
 *
 *   DERIVED THIS SESSION, NOT YET HARDWARE-TESTED: the ISP DMA
 *     buffer-address wiring (isp_core_alloc_buf_addr, sdk/lib/isp_buf_addr.c)
 *     — every ioctl/offset/constant read off the decompiled vendor binary
 *     and kernel modules, not guessed. See
 *     sdk/headers/ISP_BUF_ADDR_PROTOCOL_V201.md.
 *
 *   REGISTER VALUES CONFIRMED, ACCESS SAFETY STILL GENUINELY BLOCKED: the
 *     ISP register-table bring-up (sdk/lib/isp_init.c's isp_ispp_init/
 *     isp_ispf_init/isp_pkg_enable/isp_reset). As of 2026-10-17 every
 *     register offset and value these write is byte-exact from decompiling
 *     isp_startup_Ispp_Init/Ispf_Init/Isp_PkgEnable/isp_core_system_reset
 *     — no longer "derived from theory." The call order was also corrected
 *     to match the real isp_core_init (buffer-address allocation before
 *     this register table, not after — see isp_setup()'s own comment).
 *     What byte-exact values do NOT resolve: whether writing these
 *     registers via /dev/mem while isp.ko is loaded is safe at all — the
 *     memory-attribute-mismatch/IRQ-race risk documented in
 *     ISP_BUF_ADDR_PROTOCOL_V201.md, never settled before this session
 *     either. Getting the VALUES right reduces the chance a wrong value
 *     causes a hang or corrupted image; it does not touch that separate,
 *     still-untested access-safety question. If the device hangs or
 *     crashes here, that's still this section, not the VPSS/VMM/
 *     buffer-address work before it. isp_set_pic_size (IspInPicCfg)
 *     is byte-exact too, including the SetLscTab/SetNr3dDrcWin search.
 *
 *   CONFIRMED VIA DECOMPILE, NOT HARDWARE-TESTED: VENC (encoder) setup and
 *     the stream-get loop. Every ioctl code and struct layout below was read
 *     off ipcam's real _VENC_SysInit / _VENC_QueryChnMem / _VENC_CreateChn /
 *     _VENC_SetRCAttr / _VENC_StartRecvPic / _VENC_HandleStream /
 *     _VENC_ReleaseStream (ipcam dispatches to these through a per-channel
 *     function-pointer vtable, not direct ioctls — this code skips the
 *     vtable and calls the real ioctls those functions issue directly).
 *     One deliberate substitution: the vendor allocates VENC buffers from
 *     isp_core's shared VMM pool (buffer_malloc_withname); this code uses
 *     an independent vmm_alloc() per buffer instead, which is functionally
 *     equivalent (same VMM_IOCTL_ALLOC, same kernel-side accounting) but
 *     not byte-identical to the vendor's pool bookkeeping. Pulling encoded
 *     bytes out of the stream packs (mmap of /dev/mem at the reported phys
 *     addr) is this code's own addition — there's no vendor majestic path
 *     on this chip to decompile for that step, so it follows the same
 *     mmap pattern already used for ISP/VPSS DMA buffers. Each pack is
 *     assumed to be one complete H.264 NAL unit (addr/length/type triple)
 *     — this matches standard hardware-encoder convention and the pack
 *     struct's own shape, but it is an INFERENCE, not confirmed by decompile:
 *     FH_VENC_GetStream has no internal caller in this binary to check it
 *     against (xref search came up empty — either dynamic dispatch or dead
 *     code in this firmware build), so there's no vendor reference consumer
 *     to verify against.
 *
 *   FROM-SPEC, NOT DECOMPILED: the RTSP/RTP server (rtsp_server.c). Built
 *     directly from RFC 2326 and RFC 6184, deliberately without consulting
 *     majestic's or ipcam's own RTSP implementation — this is public
 *     protocol, not vendor logic, so there was nothing to clean-room here
 *     at all. Scope: two streams (main + sub), H.264/UDP only, optional
 *     Basic auth. See rtsp_server.h for the exact method/feature list.
 *
 *   SECOND CHANNEL (stream2/sub, 800x576): confirmed safe to generalize
 *     from decompile, not guessed — FH_VPSS_ChnInitMem/SetChnAttr/
 *     SetVOMode are all channel-indexed (checked "< 5" channels) and
 *     FH_VPSS_SysInitMem's own real init loops GET_CHN_SIZE/alloc/
 *     SET_CHN_ADDR over every channel with nonzero capability, which is
 *     the structure vpss_setup()/venc_chn_setup() follow. FH_VPSS_OpenChn
 *     and FH_VPSS_Enable take no channel argument at all — called once,
 *     not per channel, confirmed from decompile rather than assumed.
 *
 * Usage:
 *   echo V > /dev/watchdog   # magic close, disables watchdog
 *   killall ipcam
 *   ./mini_streamer
 *   # then: ffplay rtsp://<device-ip>:554/stream1   (main, 1920x1080)
 *   #       ffplay rtsp://<device-ip>:554/stream2   (sub, 800x576)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include "../sdk/lib/isp_init.h"
#include "../sdk/lib/isp_buf_addr.h"
#include "../sdk/lib/cv2003_driver.h"
#include "rtsp_server.h"

/* ---- proven VMM alloc ioctl, byte-for-byte from test_minimal_regs.c ---- */
#define VMM_IOCTL_ALLOC 0xC0506D0A

static int vmm_alloc(int vmm_fd, uint32_t size, const char *name, uint32_t *phys_out)
{
    /* Same ioctl and struct layout as isp_buf_addr.c's vmm_alloc_pool —
       offset 8 = align, offset 12 = size, confirmed from vmm.ko's own
       mmb_alloc (see ISP_BUF_ADDR_PROTOCOL_V201.md). Label left empty,
       matching the real bytes read off ipcam (0x4d1698 is 4 zero bytes). */
    uint8_t req[80];
    memset(req, 0, sizeof(req));
    *(uint32_t *)(req + 8) = 0x1000;
    *(uint32_t *)(req + 12) = size;
    strncpy((char *)(req + 0x1C), name, 15);
    int r = ioctl(vmm_fd, VMM_IOCTL_ALLOC, req);
    *phys_out = (r >= 0) ? *(uint32_t *)req : 0;
    return r;
}

/* ---- proven VPSS ioctl codes, same source ---- */
#define ISP_IOCTL_VPSS_GET_SYS_SIZE   0xC0046966
#define ISP_IOCTL_VPSS_SET_SYS_ADDR   0xC00C6964
#define ISP_IOCTL_VPSS_GET_CHN_SIZE   0xC0106967
#define ISP_IOCTL_VPSS_SET_CHN_ADDR   0xC0186968
#define ISP_IOCTL_VPSS_SET_VI_ATTR    0xC0F4696A
#define ISP_IOCTL_VPSS_ENABLE         0xC0046971
#define ISP_IOCTL_VPSS_SET_CHN_ATTR   0xC00C696C
#define ISP_IOCTL_VPSS_OPEN_CHN       0xC0046973
#define ISP_IOCTL_VPSS_SET_VO_MODE    0xC00869A4
#define ISP_IOCTL_VPSS_GET_FRAME      0xC0686970

/*
 * VENC ioctl codes and struct layouts, all confirmed by decompiling ipcam's
 * _VENC_* internals (see file header). All issued on /dev/enc's fd, not
 * /dev/isp's — VENC is a separate device node from ISP/VPSS on this chip.
 */
#define ISP_IOCTL_VENC_SYS_GET_SIZE   0xC0085401
#define ISP_IOCTL_VENC_SYS_SET_ADDR   0xC00C5400
#define ISP_IOCTL_VENC_QUERY_CHN_MEM  0xC0145402
#define ISP_IOCTL_VENC_CREATE_CHN     0xC01C5403
#define ISP_IOCTL_VENC_SET_RC_ATTR    0xC06C540C
#define ISP_IOCTL_VENC_START_RECV_PIC 0xC0045407
#define ISP_IOCTL_VENC_GET_STREAM     0xC1A04D05
#define ISP_IOCTL_VENC_RELEASE_STREAM 0xC004540A

static volatile int g_running = 1;

static void sig_handler(int sig)
{
    (void)sig;
    g_running = 0;
}

static void write_proc_config(const char *path, const char *value)
{
    int fd = open(path, O_WRONLY);
    if (fd < 0)
        return;
    write(fd, value, strlen(value));
    close(fd);
}

/* Proven /proc/driver config writes — same values as test_minimal_regs.c. */
static void apply_proc_config(void)
{
    static const char *const cfg[][2] = {
        {"/proc/driver/vpu", "vi_1920_1080"},
        {"/proc/driver/vpu", "cap_0_1920_1080"},
        {"/proc/driver/vpu", "cap_1_800_576"},
        {"/proc/driver/vpu", "cap_2_512_288"},
        {"/proc/driver/vpu", "buf_0_2"},
        {"/proc/driver/vpu", "buf_1_3"},
        {"/proc/driver/vpu", "buf_2_1"},
        {"/proc/driver/vpu", "cirbuf_on"},
        {"/proc/driver/vpu", "cbufsize_80"},
        {"/proc/driver/isp", "wdr_off"},
        {"/proc/driver/isp", "cir_on"},
        {"/proc/driver/enc", "stm_1572864"},
        {"/proc/driver/jpeg", "mem_1_131072"},
        {"/proc/driver/jpeg", "mjpg_0_0"},
        {"/proc/driver/clock", "nn_clk,enable,90000000"},
    };
    for (size_t i = 0; i < sizeof(cfg) / sizeof(cfg[0]); i++)
        write_proc_config(cfg[i][0], cfg[i][1]);
}

/*
 * Two channels: stream1 (main, 1920x1080) and stream2 (sub, 800x576 —
 * matches apply_proc_config's "cap_1_800_576"). Per-channel step ordering
 * and parameterization confirmed from FH_VPSS_SysInitMem/SetChnAttr/
 * SetVOMode (all channel-indexed, checked "< 5") and FH_VPSS_OpenChn
 * (no channel argument at all — called once, not per channel): the
 * real vendor init loops GET_CHN_SIZE/alloc/SET_CHN_ADDR over every
 * channel with nonzero capability before doing anything channel-attr
 * related, which is the structure this follows.
 */
typedef struct {
    uint32_t chan;
    uint32_t width;
    uint32_t height;
    const char *vmm_name;
} vpss_chn_desc_t;

static const vpss_chn_desc_t VPSS_CHANNELS[] = {
    {0, 1920, 1080, "vpu-ch-0"},
    {1, 800, 576, "vpu-ch-1"},
};
#define VPSS_CHANNEL_COUNT (sizeof(VPSS_CHANNELS) / sizeof(VPSS_CHANNELS[0]))

static int vpss_setup(int isp_fd, int vmm_fd)
{
    uint32_t sz = 0, ph = 0;

    if (ioctl(isp_fd, ISP_IOCTL_VPSS_GET_SYS_SIZE, &sz) < 0) return -1;
    if (vmm_alloc(vmm_fd, sz, "vpu-sys", &ph) < 0) return -1;
    uint32_t sm[3] = {ph, 0, sz};
    if (ioctl(isp_fd, ISP_IOCTL_VPSS_SET_SYS_ADDR, sm) < 0) return -1;

    for (size_t i = 0; i < VPSS_CHANNEL_COUNT; i++) {
        const vpss_chn_desc_t *d = &VPSS_CHANNELS[i];
        uint32_t q[4] = {d->chan, d->width, d->height, 0};
        if (ioctl(isp_fd, ISP_IOCTL_VPSS_GET_CHN_SIZE, q) < 0) return -1;
        if (vmm_alloc(vmm_fd, q[3], d->vmm_name, &ph) < 0) return -1;
        uint32_t cs[6] = {d->chan, ph, 0, q[3], d->width, d->height};
        if (ioctl(isp_fd, ISP_IOCTL_VPSS_SET_CHN_ADDR, cs) < 0) return -1;
    }

    uint32_t vi[62];
    memset(vi, 0, sizeof(vi));
    vi[0] = 1920;
    vi[1] = 1080;
    if (ioctl(isp_fd, ISP_IOCTL_VPSS_SET_VI_ATTR, vi) < 0) return -1;

    uint32_t en = 0;
    if (ioctl(isp_fd, ISP_IOCTL_VPSS_ENABLE, &en) < 0) return -1;

    for (size_t i = 0; i < VPSS_CHANNEL_COUNT; i++) {
        const vpss_chn_desc_t *d = &VPSS_CHANNELS[i];
        uint32_t ca[3] = {d->chan, d->width, d->height};
        if (ioctl(isp_fd, ISP_IOCTL_VPSS_SET_CHN_ATTR, ca) < 0) return -1;
    }

    uint32_t ch = 0;
    if (ioctl(isp_fd, ISP_IOCTL_VPSS_OPEN_CHN, &ch) < 0) return -1;

    for (size_t i = 0; i < VPSS_CHANNEL_COUNT; i++) {
        uint32_t vo[2] = {VPSS_CHANNELS[i].chan, 1};
        if (ioctl(isp_fd, ISP_IOCTL_VPSS_SET_VO_MODE, vo) < 0) return -1;
    }

    return 0;
}

/*
 * ISP bring-up. The ioctl prelude (everything up to and including 0x690A)
 * is ported verbatim from test_minimal_regs.c's proven sequence, NOT from
 * isp_core_init's exact decompiled order — those two don't fully agree
 * (test_minimal_regs.c's sequence is what's actually been run on hardware
 * without crashing; isp_core_init's is what the vendor binary does). After
 * 0x690A: isp_core_alloc_buf_addr (purely ioctl-mediated, no /dev/mem),
 * then isp_mmap + the register-table bring-up (isp_hw_init, STILL
 * UNRESOLVED — see file header), then isp_start_processing (PkgEnable).
 *
 * ORDER FIX (2026-10-17): previously isp_hw_init() (register writes) ran
 * BEFORE isp_core_alloc_buf_addr(). Decompiling isp_core_init's real
 * top-level sequence this session showed the opposite — buffer-address
 * allocation (`FUN_002da83c`, i.e. isp_core_alloc_buf_addr) runs before
 * isp_core_system_reset and the rest of the register-table bring-up, not
 * after. Swapped to match. This doesn't change isp_core_alloc_buf_addr's
 * own risk profile (still only ioctls, never /dev/mem) — it changes
 * which step happens first relative to the genuinely unresolved
 * register-table writes.
 */
static int isp_setup(isp_ctx_t *isp_ctx, int isp_fd, int vmm_fd, uint32_t width, uint32_t height)
{
    uint32_t zero2[2] = {0, 0};
    if (ioctl(isp_fd, 0x80086929, zero2) < 0) return -1;

    uint32_t scratch12[12] = {0};
    ioctl(isp_fd, 0x80306926, scratch12); /* output unused, matches isp_core_get_buf_size */

    uint8_t stat_info[120] = {0};
    if (ioctl(isp_fd, 0x80786905, stat_info) < 0) return -1;

    uint32_t one = 1;
    if (ioctl(isp_fd, 0x40046908, &one) < 0) return -1;
    if (ioctl(isp_fd, 0x40046909, &one) < 0) return -1;

    uint32_t handshake = 0;
    if (ioctl(isp_fd, 0x690A, &handshake) < 0) return -1; /* isp_start, confirmed benign */

    if (ioctl(isp_fd, 0x4004690F, &one) < 0) return -1;

    isp_core_set_fds(isp_fd, vmm_fd);
    if (isp_core_alloc_buf_addr(width, height) != 0) {
        fprintf(stderr, "isp_core_alloc_buf_addr failed\n");
        return -1;
    }

    fprintf(stderr,
            "WARNING: about to write the ISP register table via /dev/mem.\n"
            "This is the part that was never settled before this session —\n"
            "if the device hangs or the kernel crashes here, that's this\n"
            "step, not the VPSS/VMM/buffer-address work before it.\n");
    if (isp_hw_init(isp_ctx, width, height) < 0) {
        fprintf(stderr, "isp_hw_init failed (chip ID mismatch or mmap failure)\n");
        return -1;
    }

    isp_start_processing(isp_ctx);

    uint32_t zero = 0;
    ioctl(isp_fd, 0x6910, &zero);

    return 0;
}

/*
 * Exact 7-word layout confirmed from _VENC_CreateChn's local-variable frame:
 * ioctl(enc_fd, 0xc01c5403, &this) with chan/width/height/codec as passed
 * by FH_VENC_SetChnAttr, buf_phys/buf_virt/buf_size as allocated by the
 * caller (vendor pulls these from isp_core's shared VMM pool; this code
 * uses its own vmm_alloc() instead — see file header).
 */
typedef struct {
    uint32_t chan;
    uint32_t buf_phys;
    uint32_t buf_virt;
    uint32_t buf_size;
    uint32_t width;
    uint32_t height;
    uint32_t codec; /* 0 = H.264, 1 = H.265 */
} venc_create_chn_t;

/*
 * 26-word (104-byte) layout confirmed from _VENC_SetRCAttr. The function is
 * a big switch on rc_mode (0-0xe) with a different field layout per mode;
 * this is mode 9's branch (bitrate-only CBR-ish mode — the simplest one
 * that needs no QP floor/ceiling tuning from the caller). Field names are
 * this code's own labels, not vendor names — the decompile gave offsets
 * and constants, not identifiers.
 */
typedef struct {
    uint32_t chan;        /* offset 0x00 */
    uint32_t hw_rc_type;  /* offset 0x04 — fixed 2 for mode-9 */
    uint32_t reserved0;   /* offset 0x08 — fixed 0 */
    uint32_t bitrate_a;   /* offset 0x0c */
    uint32_t param2_a;    /* offset 0x10 */
    uint32_t bitrate_b;   /* offset 0x14 */
    uint32_t bitrate_c;   /* offset 0x18 */
    uint32_t bitrate_d;   /* offset 0x1c */
    uint32_t param2_b;    /* offset 0x20 */
    uint32_t param2_c;    /* offset 0x24 */
    uint32_t size_marker; /* offset 0x28 — fixed 8 */
    uint32_t max_bitrate_param; /* offset 0x2c — param_3[3] */
    uint32_t zero0;       /* offset 0x30 */
    uint32_t gop;         /* offset 0x34 — fixed 5 */
    uint32_t enable;      /* offset 0x38 — fixed 1 */
    uint32_t zero1;       /* offset 0x3c */
    uint32_t qp_delta_max;/* offset 0x40 — fixed 200 */
    uint32_t qp_min_a;    /* offset 0x44 — fixed 0x1e (30) */
    uint32_t qp_max_a;    /* offset 0x48 — fixed 0x22 (34) */
    uint32_t zero2;       /* offset 0x4c */
    uint32_t qp_step;     /* offset 0x50 — fixed -2 */
    uint32_t zero3;       /* offset 0x54 */
    uint32_t qp_max_b;    /* offset 0x58 — fixed 0x20 (32) */
    uint32_t qp_min_b;    /* offset 0x5c — fixed 0x1e (30) */
    uint32_t bitrate_floor;/* offset 0x60 — fixed 2000 */
    uint32_t bitrate_ceil;/* offset 0x64 — fixed 4000 */
} venc_rc_attr_mode9_t;

static int venc_set_rc_attr(int enc_fd, uint32_t chan, uint32_t bitrate_kbps)
{
    venc_rc_attr_mode9_t rc;
    memset(&rc, 0, sizeof(rc));
    rc.chan = chan;
    rc.hw_rc_type = 2;
    rc.bitrate_a = rc.bitrate_b = rc.bitrate_c = rc.bitrate_d = bitrate_kbps;
    rc.param2_a = rc.param2_b = rc.param2_c = 0;
    rc.max_bitrate_param = bitrate_kbps * 2;
    rc.size_marker = 8;
    rc.gop = 5;
    rc.enable = 1;
    rc.qp_delta_max = 200;
    rc.qp_min_a = rc.qp_min_b = 0x1e;
    rc.qp_max_a = 0x22;
    rc.qp_max_b = 0x20;
    rc.qp_step = -2;
    rc.bitrate_floor = 2000;
    rc.bitrate_ceil = 4000;
    return ioctl(enc_fd, ISP_IOCTL_VENC_SET_RC_ATTR, &rc);
}

/*
 * GetStream request/response, confirmed from FH_VENC_GetStream's 105-word
 * stack buffer and _VENC_HandleStream's field writes (stream_type 4 = H.264
 * branch). req[0] is the channel on input; everything else is filled by
 * the kernel. Each pack entry is {phys_addr, length, pack_type}.
 */
#define VENC_MAX_PACKS 20
typedef struct {
    uint32_t stream_type; /* [0]: 4 = H.264, 8 = H.265 */
    uint32_t handle;      /* [1]: pass to ReleaseStream */
    uint32_t seq;         /* [2] */
    uint32_t field3;      /* [3] */
    uint32_t field4;      /* [4] */
    uint32_t field5;      /* [5] */
    uint32_t field6;      /* [6] */
    uint32_t pack_count;  /* [7] */
    struct {
        uint32_t phys_addr;
        uint32_t length;
        uint32_t pack_type;
    } packs[VENC_MAX_PACKS];
    uint32_t tail[0x50 - 8 - VENC_MAX_PACKS * 3];
} venc_stream_t;

/*
 * Sys-level VENC memory pool — once, not per channel. Mirrors _VENC_SysInit
 * exactly (confirmed from decompile): one shared pool set up before any
 * channel is created, same pattern as VPSS's SYS_GET_SIZE/SYS_SET_ADDR.
 */
static int venc_sys_init(int enc_fd, int vmm_fd)
{
    uint32_t sys_query[2] = {0, 0}; /* [0] = size, [1] = cached flag */
    if (ioctl(enc_fd, ISP_IOCTL_VENC_SYS_GET_SIZE, sys_query) < 0) {
        perror("VENC_SYS_GET_SIZE");
        return -1;
    }
    uint32_t sys_phys = 0;
    if (vmm_alloc(vmm_fd, sys_query[0], "venc-sys", &sys_phys) < 0) {
        perror("vmm_alloc(venc-sys)");
        return -1;
    }
    uint32_t sys_addr[3] = {sys_phys, 0, sys_query[0]};
    if (ioctl(enc_fd, ISP_IOCTL_VENC_SYS_SET_ADDR, sys_addr) < 0) {
        perror("VENC_SYS_SET_ADDR");
        return -1;
    }
    return 0;
}

static int venc_chn_setup(int enc_fd, int vmm_fd, uint32_t chan, uint32_t width, uint32_t height,
                           uint32_t bitrate_kbps, uint32_t *buf_phys_out, uint32_t *buf_size_out)
{
    uint32_t chn_mem_req[5] = {chan, 0, width, height, 0}; /* [4] = codec, 0 = H.264 */
    if (ioctl(enc_fd, ISP_IOCTL_VENC_QUERY_CHN_MEM, chn_mem_req) < 0) {
        perror("VENC_QUERY_CHN_MEM");
        return -1;
    }
    uint32_t chn_buf_size = chn_mem_req[1];
    uint32_t chn_buf_phys = 0;
    char vmm_name[16];
    snprintf(vmm_name, sizeof(vmm_name), "venc-chn-%u", chan);
    if (vmm_alloc(vmm_fd, chn_buf_size, vmm_name, &chn_buf_phys) < 0) {
        perror("vmm_alloc(venc-chn)");
        return -1;
    }

    venc_create_chn_t chn;
    memset(&chn, 0, sizeof(chn));
    chn.chan = chan;
    chn.buf_phys = chn_buf_phys;
    chn.buf_virt = 0;
    chn.buf_size = chn_buf_size;
    chn.width = width;
    chn.height = height;
    chn.codec = 0;
    if (ioctl(enc_fd, ISP_IOCTL_VENC_CREATE_CHN, &chn) < 0) {
        perror("VENC_CREATE_CHN");
        return -1;
    }

    if (venc_set_rc_attr(enc_fd, chan, bitrate_kbps) < 0) {
        perror("VENC_SET_RC_ATTR");
        return -1;
    }

    if (ioctl(enc_fd, ISP_IOCTL_VENC_START_RECV_PIC) < 0) {
        perror("VENC_START_RECV_PIC");
        return -1;
    }

    *buf_phys_out = chn_buf_phys;
    *buf_size_out = chn_buf_size;
    return 0;
}

/*
 * Strip a leading Annex-B start code, if present. The "one pack = one NAL"
 * assumption (see file header) means this is defensive, not load-bearing —
 * if the hardware already hands back NALs without start codes, off stays 0.
 */
static void strip_start_code(const uint8_t *buf, size_t len, const uint8_t **nal, size_t *nal_len)
{
    size_t off = 0;
    if (len >= 4 && buf[0] == 0 && buf[1] == 0 && buf[2] == 0 && buf[3] == 1)
        off = 4;
    else if (len >= 3 && buf[0] == 0 && buf[1] == 0 && buf[2] == 1)
        off = 3;
    *nal = buf + off;
    *nal_len = len - off;
}

/*
 * One GetStream/ReleaseStream cycle. Pack addresses point into DMA memory,
 * not process memory — each pack is mmap'd from /dev/mem, written to stdout
 * (preserving the raw-H.264-on-stdout usage mode) and handed to the RTSP
 * server as one NAL, then unmapped, before the stream handle is released.
 */
static int venc_get_stream(int enc_fd, int mem_fd, uint32_t chan,
                            rtsp_server_t *rtsp, uint32_t rtp_ts)
{
    venc_stream_t stream;
    memset(&stream, 0, sizeof(stream));
    stream.stream_type = chan;
    if (ioctl(enc_fd, ISP_IOCTL_VENC_GET_STREAM, &stream) < 0)
        return -1;
    if (stream.pack_count == 0)
        return 0;

    for (uint32_t i = 0; i < stream.pack_count && i < VENC_MAX_PACKS; i++) {
        uint32_t phys = stream.packs[i].phys_addr;
        uint32_t len = stream.packs[i].length;
        if (len == 0)
            continue;
        uint32_t page_off = phys & 0xFFF;
        void *map = mmap(NULL, len + page_off, PROT_READ, MAP_SHARED, mem_fd, phys & ~0xFFFUL);
        if (map == MAP_FAILED)
            continue;

        const uint8_t *frame_bytes = (uint8_t *)map + page_off;
        write(STDOUT_FILENO, frame_bytes, len);

        const uint8_t *nal;
        size_t nal_len;
        strip_start_code(frame_bytes, len, &nal, &nal_len);
        if (nal_len > 0) {
            rtsp_server_set_param_set(rtsp, (int)chan, nal, nal_len);
            int is_last = (i + 1 == stream.pack_count);
            rtsp_server_send_nal(rtsp, (int)chan, nal, nal_len, rtp_ts, is_last);
        }

        munmap(map, len + page_off);
    }

    ioctl(enc_fd, ISP_IOCTL_VENC_RELEASE_STREAM, &stream.handle);
    return (int)stream.pack_count;
}

int main(void)
{
    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);
    signal(SIGPIPE, SIG_IGN);

    fprintf(stderr, "=== FH8852V201 Mini Streamer (majestic replacement) ===\n");
    fprintf(stderr, "Sensor: CV2003 @ /dev/i2c-0:0x35\n");
    fprintf(stderr, "Output: raw H.264 NALUs on stdout\n\n");

    apply_proc_config();

    int mp_fd = open("/dev/media_process", O_RDWR);
    int isp_fd = open("/dev/isp", O_RDWR);
    int enc_fd = open("/dev/enc", O_RDWR);
    int jpeg_fd = open("/dev/jpeg", O_RDWR);
    int vmm_fd = open("/dev/vmm_userdev", O_RDWR);
    (void)jpeg_fd;

    if (mp_fd < 0 || isp_fd < 0 || enc_fd < 0 || vmm_fd < 0) {
        fprintf(stderr, "device open failed (mp=%d isp=%d enc=%d vmm=%d)\n", mp_fd, isp_fd, enc_fd, vmm_fd);
        return 1;
    }

    if (cv2003_init() < 0) {
        fprintf(stderr, "cv2003_init failed\n");
        return 1;
    }
    if (cv2003_set_format() < 0) {
        fprintf(stderr, "cv2003_set_format failed\n");
        return 1;
    }

    if (vpss_setup(isp_fd, vmm_fd) < 0) {
        fprintf(stderr, "vpss_setup failed\n");
        return 1;
    }

    isp_ctx_t isp_ctx;
    memset(&isp_ctx, 0, sizeof(isp_ctx));
    if (isp_setup(&isp_ctx, isp_fd, vmm_fd, 1920, 1080) < 0) {
        fprintf(stderr, "isp_setup failed\n");
        return 1;
    }

    if (venc_sys_init(enc_fd, vmm_fd) < 0) {
        fprintf(stderr, "venc_sys_init failed\n");
        return 1;
    }
    for (size_t i = 0; i < VPSS_CHANNEL_COUNT; i++) {
        const vpss_chn_desc_t *d = &VPSS_CHANNELS[i];
        uint32_t buf_phys = 0, buf_size = 0;
        uint32_t bitrate_kbps = (d->chan == 0) ? 2048 : 512;
        if (venc_chn_setup(enc_fd, vmm_fd, d->chan, d->width, d->height, bitrate_kbps,
                            &buf_phys, &buf_size) < 0) {
            fprintf(stderr, "venc_chn_setup failed for channel %u\n", d->chan);
            return 1;
        }
    }

    rtsp_server_t rtsp;
    if (rtsp_server_init(&rtsp, 554) < 0) {
        fprintf(stderr, "rtsp_server_init failed\n");
        return 1;
    }
    const char *rtsp_user = getenv("MINI_STREAMER_RTSP_USER");
    const char *rtsp_pass = getenv("MINI_STREAMER_RTSP_PASS");
    if (rtsp_user && rtsp_pass) {
        rtsp_server_set_credentials(&rtsp, rtsp_user, rtsp_pass);
        fprintf(stderr, "RTSP auth enabled (user: %s)\n", rtsp_user);
    }

    fprintf(stderr, "Pipeline ready: rtsp://0.0.0.0:554/stream1 (main), "
                     "rtsp://0.0.0.0:554/stream2 (sub). Raw H.264 for stream1 also on stdout.\n");
    fprintf(stderr, "Press Ctrl+C to stop\n");

    uint32_t frame_count = 0;
    uint32_t rtp_ts[VPSS_CHANNEL_COUNT] = {0};
    uint32_t chan_frame_count[VPSS_CHANNEL_COUNT] = {0};
    while (g_running) {
        rtsp_server_poll(&rtsp);

        int any_frame = 0;
        for (size_t i = 0; i < VPSS_CHANNEL_COUNT; i++) {
            uint32_t chan = VPSS_CHANNELS[i].chan;
            uint32_t frame[26];
            memset(frame, 0, sizeof(frame));
            frame[0] = chan;
            if (ioctl(isp_fd, ISP_IOCTL_VPSS_GET_FRAME, frame) != 0)
                continue;

            int pack_count = venc_get_stream(enc_fd, isp_ctx.mem_fd, chan, &rtsp, rtp_ts[i]);
            if (pack_count <= 0)
                continue;
            any_frame = 1;

            /* RTCP SR once per second (~25 frames @ 25fps) per RFC 3550 6.4.1 */
            chan_frame_count[i]++;
            if (chan_frame_count[i] % 25 == 0) {
                rtsp_server_send_rtcp_sr(&rtsp, (int)chan, rtp_ts[i]);
            }

            rtp_ts[i] += 3600; /* 90000 Hz RTP clock / 25 fps */

            if (chan == 0) {
                frame_count++;
                if (frame_count % 25 == 0) {
                    fprintf(stderr, "Frames: %u (last GetStream: %d packs)\n", frame_count, pack_count);
                }
            }
        }
        if (!any_frame) usleep(10000);
    }

    rtsp_server_close(&rtsp);
    fprintf(stderr, "\nStopping... (%u frames captured)\n", frame_count);

    cv2003_deinit();
    isp_cleanup(&isp_ctx);
    close(enc_fd);
    close(vmm_fd);
    close(isp_fd);
    close(mp_fd);

    return 0;
}
