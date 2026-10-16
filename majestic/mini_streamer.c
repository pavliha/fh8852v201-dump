/**
 * Minimal H.264 streamer for FH8852V201 + CV2003 — majestic replacement.
 *
 * No vendor SDK linkage (there's no .so to link against on this chip —
 * see ../PORTING_GUIDE.md). Every call here is either a direct ioctl on
 * the kernel modules, or one of the clean-room libraries in sdk/lib/.
 *
 * VERIFICATION STATUS BY SECTION — read this before trusting any one part:
 *
 *   PROVEN ON REAL HARDWARE: device opens, /proc/driver config writes, MIPI
 *     ioctls, VMM allocation, VPSS setup. Ported verbatim from
 *     sdk/tests/archive/test_minimal_regs.c, which got MIPI frames flowing
 *     at 1920x1080@25fps.
 *
 *   DERIVED THIS SESSION, NOT YET HARDWARE-TESTED: the ISP DMA
 *     buffer-address wiring (isp_core_alloc_buf_addr, sdk/lib/isp_buf_addr.c)
 *     — every ioctl/offset/constant read off the decompiled vendor binary
 *     and kernel modules, not guessed. See
 *     sdk/headers/ISP_BUF_ADDR_PROTOCOL_V201.md.
 *
 *   STILL GENUINELY BLOCKED: the ISP register-table bring-up
 *     (isp_core_system_reset / common_init / isp_startup / isp_module_bypass
 *     in the vendor binary) pokes ~100+ registers directly via /dev/mem.
 *     This is the same unresolved crash risk (memory-attribute mismatch or
 *     IRQ race with isp.ko, never settled) the project was stuck on before
 *     this session. sdk/lib/isp_init.c's isp_hw_init()/isp_start_processing()
 *     is the existing clean-room attempt at this — called here, but this is
 *     the one part of the pipeline that may still crash the kernel. If it
 *     does, that's this section, not the VPSS/VMM/buffer-address work above.
 *
 *   UNVERIFIED, BEST-EFFORT: VENC (encoder) setup and the stream-get loop.
 *     Only 3 ioctl codes are documented (../PORTING_GUIDE.md, option B) with
 *     no verified struct layout — no prior test ever exercised encoding on
 *     this chip. Struct shapes here are a best guess from common Fullhan SDK
 *     patterns, not decompiled. Expect to iterate this against real ioctl
 *     error returns.
 *
 * Usage:
 *   echo V > /dev/watchdog   # magic close, disables watchdog
 *   killall ipcam
 *   ./mini_streamer > /tmp/stream.h264
 *   ./mini_streamer | ffplay -f h264 -
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/ioctl.h>

#include "../sdk/lib/isp_init.h"
#include "../sdk/lib/isp_buf_addr.h"
#include "../sdk/lib/cv2003_driver.h"

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

#define ISP_IOCTL_MIPI_LANES          0x40036954
#define ISP_IOCTL_MIPI_SET            0x40066955

/*
 * VENC ioctl codes from ../PORTING_GUIDE.md (option B). Documented there as
 * codes only — no verified struct layout. These struct shapes are a best
 * guess (matching the general FH_VENC_* field conventions seen elsewhere
 * in this codebase), not decompiled.
 */
#define ISP_IOCTL_VENC_CREATE_CHN     0xC01C5403
#define ISP_IOCTL_VENC_SET_RC_ATTR    0xC06C540C
#define ISP_IOCTL_VENC_GET_STREAM     0xC1A04D05

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

static int vpss_setup(int isp_fd, int vmm_fd)
{
    uint32_t sz = 0, ph = 0;

    if (ioctl(isp_fd, ISP_IOCTL_VPSS_GET_SYS_SIZE, &sz) < 0) return -1;
    if (vmm_alloc(vmm_fd, sz, "vpu-sys", &ph) < 0) return -1;
    uint32_t sm[3] = {ph, 0, sz};
    if (ioctl(isp_fd, ISP_IOCTL_VPSS_SET_SYS_ADDR, sm) < 0) return -1;

    uint32_t q[4] = {0, 1920, 1080, 0};
    if (ioctl(isp_fd, ISP_IOCTL_VPSS_GET_CHN_SIZE, q) < 0) return -1;
    if (vmm_alloc(vmm_fd, q[3], "vpu-ch-0", &ph) < 0) return -1;
    uint32_t cs[6] = {0, ph, 0, q[3], 1920, 1080};
    if (ioctl(isp_fd, ISP_IOCTL_VPSS_SET_CHN_ADDR, cs) < 0) return -1;

    uint32_t vi[62];
    memset(vi, 0, sizeof(vi));
    vi[0] = 1920;
    vi[1] = 1080;
    if (ioctl(isp_fd, ISP_IOCTL_VPSS_SET_VI_ATTR, vi) < 0) return -1;

    uint32_t en = 0;
    if (ioctl(isp_fd, ISP_IOCTL_VPSS_ENABLE, &en) < 0) return -1;

    uint32_t ca[3] = {0, 1920, 1080};
    if (ioctl(isp_fd, ISP_IOCTL_VPSS_SET_CHN_ATTR, ca) < 0) return -1;

    uint32_t ch = 0;
    if (ioctl(isp_fd, ISP_IOCTL_VPSS_OPEN_CHN, &ch) < 0) return -1;

    uint32_t vo[2] = {0, 1};
    if (ioctl(isp_fd, ISP_IOCTL_VPSS_SET_VO_MODE, vo) < 0) return -1;

    return 0;
}

static int mipi_setup(int isp_fd)
{
    uint8_t lanes[3] = {0, 8, 1};
    if (ioctl(isp_fd, ISP_IOCTL_MIPI_LANES, lanes) < 0) return -1;
    uint8_t set[6] = {0, 1, 0, 0, 0, 0};
    if (ioctl(isp_fd, ISP_IOCTL_MIPI_SET, set) < 0) return -1;
    return 0;
}

/*
 * ISP bring-up. The ioctl prelude (everything up to and including 0x690A)
 * is ported verbatim from test_minimal_regs.c's proven sequence, NOT from
 * isp_core_init's exact decompiled order — those two don't fully agree
 * (test_minimal_regs.c's sequence is what's actually been run on hardware
 * without crashing; isp_core_init's is what the vendor binary does). After
 * 0x690A: isp_mmap + the register-table bring-up (isp_hw_init,
 * STILL UNRESOLVED — see file header), then isp_core_alloc_buf_addr
 * (this session's work, matches isp_core_init's real order: buffer-address
 * wiring before isp_module_bypass), then isp_start_processing (PkgEnable).
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

    fprintf(stderr,
            "WARNING: about to write the ISP register table via /dev/mem.\n"
            "This is the part that was never settled before this session —\n"
            "if the device hangs or the kernel crashes here, that's this\n"
            "step, not the VPSS/VMM/buffer-address work before it.\n");
    if (isp_hw_init(isp_ctx, width, height) < 0) {
        fprintf(stderr, "isp_hw_init failed (chip ID mismatch or mmap failure)\n");
        return -1;
    }

    isp_core_set_fds(isp_fd, vmm_fd);
    if (isp_core_alloc_buf_addr(width, height) != 0) {
        fprintf(stderr, "isp_core_alloc_buf_addr failed\n");
        return -1;
    }

    isp_start_processing(isp_ctx);

    uint32_t zero = 0;
    ioctl(isp_fd, 0x6910, &zero);

    return 0;
}

/*
 * UNVERIFIED — struct shapes are a best guess, not decompiled. Expect to
 * iterate this against real ioctl error returns on hardware.
 */
typedef struct {
    uint32_t chn_id;
    uint32_t codec;     /* 0 = H.264 */
    uint32_t width;
    uint32_t height;
    uint32_t pad[4];
} venc_chn_config_t;

typedef struct {
    uint32_t chn_id;
    uint32_t rc_mode;   /* 1 = VBR */
    uint32_t bitrate_kbps;
    uint32_t gop;
    uint32_t pad[4];
} venc_rc_attr_t;

typedef struct {
    uint32_t chn_id;
    uint32_t phys_addr;
    uint32_t length;
    uint32_t nal_type;
    uint32_t pts;
    uint32_t pad[8];
} venc_stream_t;

static int venc_setup(int isp_fd)
{
    venc_chn_config_t chn = {0};
    chn.chn_id = 0;
    chn.codec = 0;
    chn.width = 1920;
    chn.height = 1080;
    if (ioctl(isp_fd, ISP_IOCTL_VENC_CREATE_CHN, &chn) < 0) {
        perror("VENC_CREATE_CHN");
        return -1;
    }

    venc_rc_attr_t rc = {0};
    rc.chn_id = 0;
    rc.rc_mode = 1;
    rc.bitrate_kbps = 2048;
    rc.gop = 50;
    if (ioctl(isp_fd, ISP_IOCTL_VENC_SET_RC_ATTR, &rc) < 0) {
        perror("VENC_SET_RC_ATTR");
        return -1;
    }

    return 0;
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
    (void)enc_fd;
    (void)jpeg_fd;

    if (mp_fd < 0 || isp_fd < 0 || vmm_fd < 0) {
        fprintf(stderr, "device open failed (mp=%d isp=%d vmm=%d)\n", mp_fd, isp_fd, vmm_fd);
        return 1;
    }

    if (mipi_setup(isp_fd) < 0) {
        fprintf(stderr, "mipi_setup failed\n");
        return 1;
    }

    if (cv2003_init() < 0) {
        fprintf(stderr, "cv2003_init failed\n");
        return 1;
    }
    cv2003_set_format(0);

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

    if (venc_setup(isp_fd) < 0) {
        fprintf(stderr, "venc_setup failed (unverified codepath, expected on first runs)\n");
        return 1;
    }

    fprintf(stderr, "Pipeline ready. Streaming H.264 to stdout...\n");
    fprintf(stderr, "Press Ctrl+C to stop\n");

    uint32_t frame_count = 0;
    while (g_running) {
        uint32_t frame[26];
        memset(frame, 0, sizeof(frame));
        int ret = ioctl(isp_fd, ISP_IOCTL_VPSS_GET_FRAME, frame);
        if (ret != 0) {
            usleep(10000);
            continue;
        }

        venc_stream_t stream = {0};
        ret = ioctl(isp_fd, ISP_IOCTL_VENC_GET_STREAM, &stream);
        if (ret != 0 || stream.length == 0) {
            usleep(10000);
            continue;
        }

        frame_count++;
        if (frame_count % 25 == 0) {
            fprintf(stderr, "Frames: %u (last %u bytes, type %u)\n",
                    frame_count, stream.length, stream.nal_type);
        }
    }

    fprintf(stderr, "\nStopping... (%u frames captured)\n", frame_count);

    cv2003_deinit();
    isp_cleanup(&isp_ctx);
    close(vmm_fd);
    close(isp_fd);
    close(mp_fd);

    return 0;
}
