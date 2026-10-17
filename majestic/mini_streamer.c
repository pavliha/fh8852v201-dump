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
#include "majestic_bitrate.h"

#define VMM_IOCTL_ALLOC 0xC0506D0A

static int vmm_alloc(int vmm_fd, uint32_t size, const char *name, uint32_t *phys_out)
{

    uint8_t req[80];
    memset(req, 0, sizeof(req));
    *(uint32_t *)(req + 8) = 0x1000;
    *(uint32_t *)(req + 12) = size;
    strncpy((char *)(req + 0x1C), name, 15);
    int r = ioctl(vmm_fd, VMM_IOCTL_ALLOC, req);
    *phys_out = (r >= 0) ? *(uint32_t *)req : 0;
    return r;
}

#define ISP_IOCTL_VPSS_GET_SYS_SIZE   0xC0046966
#define ISP_IOCTL_VPSS_SET_SYS_ADDR   0xC00C6964
#define ISP_IOCTL_VPSS_GET_CHN_SIZE   0xC0106967
#define ISP_IOCTL_VPSS_SET_CHN_ADDR   0xC0186968
#define ISP_IOCTL_VPSS_SET_VI_ATTR    0xC0F4696A
#define ISP_IOCTL_VPSS_ENABLE         0xC0046971
#define ISP_IOCTL_VPSS_SET_CHN_ATTR   0xC00C696C
#define ISP_IOCTL_VPSS_OPEN_CHN       0xC0046973
#define ISP_IOCTL_VPSS_SET_VO_MODE    0xC00869A4

#define ISP_IOCTL_VENC_SYS_GET_SIZE   0xC0085401
#define ISP_IOCTL_VENC_SYS_SET_ADDR   0xC00C5400
#define ISP_IOCTL_VENC_QUERY_CHN_MEM  0xC0145402
#define ISP_IOCTL_VENC_CREATE_CHN     0xC01C5403
#define ISP_IOCTL_VENC_SET_RC_ATTR    0xC06C540C
#define ISP_IOCTL_VENC_START_RECV_PIC 0xC0045407
#define ISP_IOCTL_VENC_GET_STREAM     0xC1A04D05
#define ISP_IOCTL_VENC_RELEASE_STREAM 0xC004540A
#define ISP_IOCTL_VENC_SET_CHN_CONFIG 0xC0485405
#define ISP_IOCTL_VENC_REQUEST_IDR    0xC004540B
#define MEDIA_IOCTL_BIND              0xC0104D00

#define MEDIA_OBJECT_VPU 2
#define MEDIA_OBJECT_ENC 4
#define VENC_STREAM_H264 4
#define VENC_CODEC_H264 4
#define VENC_PROFILE_MAIN 77
#define VENC_INIT_QP 35
#define VENC_FPS 25
#define VENC_GOP_FRAMES 25
#define VENC_STREAM_WORDS (0x1A0 / 4)
#define VENC_STREAM_NALU_WORD 12
#define VENC_STREAM_NALU_MAX ((0x170 - 0x30) / 16)

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

static int isp_setup(isp_ctx_t *isp_ctx, int isp_fd, int vmm_fd, uint32_t width, uint32_t height)
{
    uint32_t zero2[2] = {0, 0};
    if (ioctl(isp_fd, 0x80086929, zero2) < 0) return -1;

    uint32_t scratch12[12] = {0};
    ioctl(isp_fd, 0x80306926, scratch12);

    uint8_t stat_info[120] = {0};
    if (ioctl(isp_fd, 0x80786905, stat_info) < 0) return -1;

    uint32_t one = 1;
    if (ioctl(isp_fd, 0x40046908, &one) < 0) return -1;
    if (ioctl(isp_fd, 0x40046909, &one) < 0) return -1;

    uint32_t handshake = 0;
    if (ioctl(isp_fd, 0x690A, &handshake) < 0) return -1;

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

typedef struct {
    uint32_t chan;
    uint32_t buf_phys;
    uint32_t buf_virt;
    uint32_t buf_size;
    uint32_t width;
    uint32_t height;
    uint32_t codec;
} venc_create_chn_t;

typedef struct {
    uint32_t chan;
    uint32_t hw_rc_type;
    uint32_t reserved0;
    uint32_t bitrate_a;
    uint32_t param2_a;
    uint32_t bitrate_b;
    uint32_t bitrate_c;
    uint32_t bitrate_d;
    uint32_t param2_b;
    uint32_t param2_c;
    uint32_t size_marker;
    uint32_t max_bitrate_param;
    uint32_t zero0;
    uint32_t gop;
    uint32_t enable;
    uint32_t zero1;
    uint32_t qp_delta_max;
    uint32_t qp_min_a;
    uint32_t qp_max_a;
    uint32_t zero2;
    uint32_t qp_step;
    uint32_t zero3;
    uint32_t qp_max_b;
    uint32_t qp_min_b;
    uint32_t bitrate_floor;
    uint32_t bitrate_ceil;
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


static int venc_sys_init(int enc_fd, int vmm_fd)
{
    uint32_t sys_query[2] = {0, 0};
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
    uint32_t chn_mem_req[5] = {chan, 0, width, height, 0};
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

    uint32_t config[18] = {chan, VENC_CODEC_H264, width, height, width, height, VENC_GOP_FRAMES,
                           VENC_PROFILE_MAIN, VENC_INIT_QP, VENC_FPS | (1u << 16)};
    if (ioctl(enc_fd, ISP_IOCTL_VENC_SET_CHN_CONFIG, config) < 0) {
        perror("VENC_SET_CHN_CONFIG");
        return -1;
    }

    if (venc_set_rc_attr(enc_fd, chan, bitrate_kbps) < 0) {
        perror("VENC_SET_RC_ATTR");
        return -1;
    }

    *buf_phys_out = chn_buf_phys;
    *buf_size_out = chn_buf_size;
    return 0;
}

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

static int venc_bind_and_start(int mp_fd, int enc_fd, uint32_t chan)
{
    uint32_t bind[4] = {MEDIA_OBJECT_VPU, chan, MEDIA_OBJECT_ENC, chan};
    if (ioctl(mp_fd, MEDIA_IOCTL_BIND, bind) < 0) {
        perror("MEDIA_BIND vpu->enc");
        return -1;
    }
    if (ioctl(enc_fd, ISP_IOCTL_VENC_START_RECV_PIC, &chan) < 0) {
        perror("VENC_START_RECV_PIC");
        return -1;
    }
    return 0;
}

static void venc_request_idr(void *ctx, int stream_id)
{
    uint32_t chan = (uint32_t)stream_id;
    ioctl(*(int *)ctx, ISP_IOCTL_VENC_REQUEST_IDR, &chan);
}

static int venc_get_stream(int mp_fd, int enc_fd, int mem_fd, rtsp_server_t *rtsp)
{
    uint32_t st[VENC_STREAM_WORDS];
    memset(st, 0, sizeof(st));
    st[0] = VENC_STREAM_H264;
    if (ioctl(mp_fd, ISP_IOCTL_VENC_GET_STREAM, st) != 0 || st[1] != VENC_STREAM_H264)
        return 0;

    uint32_t chan = st[3];
    uint32_t nalu_count = st[4] < VENC_STREAM_NALU_MAX ? st[4] : VENC_STREAM_NALU_MAX;
    uint64_t pts_us = (uint64_t)st[11] << 32 | st[10];
    for (uint32_t i = 0; i < nalu_count; i++) {
        const uint32_t *nalu = &st[VENC_STREAM_NALU_WORD + i * 4];
        uint32_t phys = nalu[1];
        uint32_t len = nalu[3];
        if (len == 0)
            continue;
        uint32_t page_off = phys & 0xFFF;
        void *map = mmap(NULL, len + page_off, PROT_READ, MAP_SHARED, mem_fd, phys & ~0xFFFUL);
        if (map == MAP_FAILED)
            continue;
        const uint8_t *bytes = (uint8_t *)map + page_off;
        if (chan == 0)
            write(STDOUT_FILENO, bytes, len);
        const uint8_t *nal;
        size_t nal_len;
        strip_start_code(bytes, len, &nal, &nal_len);
        rtsp_server_push_nal(rtsp, (int)chan, nal, nal_len, pts_us, i + 1 == nalu_count);
        munmap(map, len + page_off);
    }

    ioctl(enc_fd, ISP_IOCTL_VENC_RELEASE_STREAM, &chan);
    return 1;
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
        uint32_t bitrate_kbps = majestic_auto_bitrate_kbps(d->width, d->height, VENC_FPS);
        if (venc_chn_setup(enc_fd, vmm_fd, d->chan, d->width, d->height, bitrate_kbps,
                            &buf_phys, &buf_size) < 0) {
            fprintf(stderr, "venc_chn_setup failed for channel %u\n", d->chan);
            return 1;
        }
        if (venc_bind_and_start(mp_fd, enc_fd, d->chan) < 0)
            return 1;
    }

    rtsp_server_t rtsp;
    if (rtsp_server_init(&rtsp, 554) < 0) {
        fprintf(stderr, "rtsp_server_init failed\n");
        return 1;
    }
    for (size_t i = 0; i < VPSS_CHANNEL_COUNT; i++)
        rtsp_server_enable_stream(&rtsp, (int)VPSS_CHANNELS[i].chan, VENC_FPS);
    rtsp_server_on_idr_request(&rtsp, venc_request_idr, &enc_fd);
    const char *rtsp_user = getenv("MINI_STREAMER_RTSP_USER");
    const char *rtsp_pass = getenv("MINI_STREAMER_RTSP_PASS");
    if (rtsp_user && rtsp_pass) {
        rtsp_server_set_credentials(&rtsp, rtsp_user, rtsp_pass);
        fprintf(stderr, "RTSP auth: user %s from the environment\n", rtsp_user);
    } else {
        rtsp_server_use_shadow(&rtsp, "/etc/shadow");
        fprintf(stderr, "RTSP auth: system accounts from /etc/shadow, like majestic\n");
    }

    fprintf(stderr, "Pipeline ready: rtsp://0.0.0.0:554/stream=0 (main), "
                     "rtsp://0.0.0.0:554/stream=1 (sub). Raw H.264 for stream=0 also on stdout.\n");
    fprintf(stderr, "Press Ctrl+C to stop\n");

    uint32_t frame_count = 0;
    while (g_running) {
        rtsp_server_poll(&rtsp);
        if (!venc_get_stream(mp_fd, enc_fd, isp_ctx.mem_fd, &rtsp)) {
            usleep(2000);
            continue;
        }
        if (++frame_count % 50 == 0)
            fprintf(stderr, "Frames: %u\n", frame_count);
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
