/**
 * Minimal H.264 streamer for FH8852V201 + CV2003
 *
 * Uses the Fullhan SDK libraries (from OpenIPC OSDRV) linked directly
 * at compile time. Kill ipcam first (after disabling watchdog), then
 * run this.
 *
 * Pipeline: CV2003 sensor -> ISP -> VPSS -> VENC -> stdout (raw H.264)
 *
 * Usage:
 *   # Disable watchdog first, then kill ipcam:
 *   echo V > /dev/watchdog   # magic close, disables watchdog
 *   killall ipcam
 *   ./mini_streamer > /tmp/stream.h264
 *   # Or pipe to ffplay:
 *   ./mini_streamer | ffplay -f h264 -
 *
 * Compile (cross-compile with Buildroot toolchain):
 *   arm-buildroot-linux-uclibcgnueabihf-gcc -O2 --sysroot=$SYSROOT \
 *       -o mini_streamer mini_streamer.c \
 *       -L/tmp/fh_libs -ldsp -lisp -lispcore -ladvapi -ladvapi_isp \
 *       -ladvapi_smartir -lmipi -lvmm -lcv2003_mipi -lpthread
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>

/* ================================================================
 * Fullhan SDK type definitions (from Ghidra decompilation)
 * ================================================================ */

typedef unsigned int FH_UINT32;
typedef unsigned short FH_UINT16;
typedef int FH_SINT32;
typedef void FH_VOID;

/* VPSS VI attributes */
typedef struct {
    FH_UINT32 vi_size_w;
    FH_UINT32 vi_size_h;
} FH_VPU_SIZE;

/* VPSS channel attributes */
typedef struct {
    FH_UINT32 vpu_chn_size_w;
    FH_UINT32 vpu_chn_size_h;
} FH_VPU_CHN_CONFIG;

/* VENC channel config */
typedef struct {
    FH_UINT32 chn_type;      /* 0=H264, 1=H265, 2=MJPEG */
    FH_UINT32 width;
    FH_UINT32 height;
    FH_UINT32 bps;           /* bitrate */
    FH_UINT32 flags;
    FH_UINT32 pad[2];
} FH_VENC_CHN_CONFIG;

/* VENC rate control */
typedef struct {
    FH_UINT32 rc_type;       /* 0=CBR, 1=VBR */
    FH_UINT32 init_qp;
    FH_UINT32 min_qp;
    FH_UINT32 max_qp;
    FH_UINT32 i_bit_ratio;
    FH_UINT32 bps;
    FH_UINT32 pad[20];
} FH_VENC_RC_ATTR;

/* VENC stream packet */
typedef struct {
    FH_UINT32 addr;          /* physical address */
    void     *vaddr;         /* virtual address */
    FH_UINT32 length;        /* data length */
    FH_UINT32 type;          /* NAL type */
    FH_UINT32 pts;
    FH_UINT32 seq;
    FH_UINT32 pad[20];
} FH_VENC_STREAM;

/* LDC config */
typedef struct {
    FH_UINT32 center_x;
    FH_UINT32 center_y;
    FH_UINT32 ratio;
} FH_LDC_CFG;

/* ================================================================
 * Fullhan SDK extern declarations (linked at compile time)
 * ================================================================ */

/* System */
extern FH_SINT32 FH_SYS_Init(void);
extern FH_SINT32 FH_SYS_Exit(void);
extern FH_SINT32 FH_SYS_BindVpu2Enc(FH_UINT32, FH_UINT32, FH_UINT32, FH_UINT32);

/* VPSS */
extern FH_SINT32 FH_VPSS_SetViAttr(FH_VPU_SIZE *);
extern FH_SINT32 FH_VPSS_Enable(FH_UINT32);
extern FH_SINT32 FH_VPSS_Disable(FH_UINT32);
extern FH_SINT32 FH_VPSS_SetChnAttr(FH_UINT32, FH_VPU_CHN_CONFIG *);
extern FH_SINT32 FH_VPSS_OpenChn(FH_UINT32);
extern FH_SINT32 FH_VPSS_CloseChn(FH_UINT32);
extern FH_SINT32 FH_VPSS_SetVOMode(FH_UINT32, FH_UINT32);

/* ISP */
extern FH_SINT32 API_ISP_MemInit(FH_UINT32, FH_UINT32);
extern FH_SINT32 API_ISP_Init(void);
extern FH_SINT32 API_ISP_Run(void);
extern FH_SINT32 API_ISP_Exit(void);
extern FH_SINT32 API_ISP_SensorRegCb(FH_UINT32, void *);
extern FH_SINT32 API_ISP_SensorInit(void);
extern FH_SINT32 API_ISP_SensorKick(void);
extern FH_SINT32 API_ISP_SetSensorFmt(FH_UINT32);
extern FH_SINT32 API_ISP_LoadIspParam(const char *);

/* MIPI */
extern FH_SINT32 mipi_init(void *);

/* VENC */
extern FH_SINT32 FH_VENC_CreateChn(FH_UINT32, FH_VENC_CHN_CONFIG *);
extern FH_SINT32 FH_VENC_StartRecvPic(FH_UINT32);
extern FH_SINT32 FH_VENC_StopRecvPic(FH_UINT32);
extern FH_SINT32 FH_VENC_GetStream_Block(FH_UINT32, FH_VENC_STREAM *);
extern FH_SINT32 FH_VENC_ReleaseStream(FH_UINT32);
extern FH_SINT32 FH_VENC_SetRCAttr(FH_UINT32, FH_VENC_RC_ATTR *);
extern FH_SINT32 FH_VENC_RequestIDR(FH_UINT32);

/* Sensor */
extern void *Sensor_Create(void);

/* ================================================================
 * Pipeline setup (from decompiled p_platform_sdk_init)
 * ================================================================ */

static volatile int g_running = 1;

static void sig_handler(int sig)
{
    (void)sig;
    g_running = 0;
}

static int init_pipeline(void)
{
    int ret;

    fprintf(stderr, "[1/8] FH_SYS_Init...\n");
    ret = FH_SYS_Init();
    if (ret) { fprintf(stderr, "FH_SYS_Init failed: 0x%x\n", ret); return -1; }

    fprintf(stderr, "[2/8] VPSS setup (1920x1080)...\n");
    FH_VPU_SIZE vi = { .vi_size_w = 1920, .vi_size_h = 1080 };
    ret = FH_VPSS_SetViAttr(&vi);
    if (ret) { fprintf(stderr, "FH_VPSS_SetViAttr failed: 0x%x\n", ret); return -1; }

    ret = FH_VPSS_Enable(0);
    if (ret) { fprintf(stderr, "FH_VPSS_Enable failed: 0x%x\n", ret); return -1; }

    /* Channel 0: main stream 1920x1080 */
    FH_VPU_CHN_CONFIG chn0 = { .vpu_chn_size_w = 1920, .vpu_chn_size_h = 1080 };
    ret = FH_VPSS_SetChnAttr(0, &chn0);
    if (ret) { fprintf(stderr, "FH_VPSS_SetChnAttr(0) failed: 0x%x\n", ret); return -1; }

    ret = FH_VPSS_OpenChn(0);
    if (ret) { fprintf(stderr, "FH_VPSS_OpenChn(0) failed: 0x%x\n", ret); return -1; }

    ret = FH_VPSS_SetVOMode(0, 1);
    if (ret) { fprintf(stderr, "FH_VPSS_SetVOMode(0) failed: 0x%x\n", ret); }

    fprintf(stderr, "[3/8] ISP init...\n");
    /* Get sensor control struct */
    void *sensor_ctrl = Sensor_Create();
    if (!sensor_ctrl) { fprintf(stderr, "Sensor_Create failed\n"); return -1; }

    ret = API_ISP_MemInit(1920, 1080);
    if (ret) { fprintf(stderr, "API_ISP_MemInit failed: 0x%x\n", ret); return -1; }

    ret = API_ISP_SensorRegCb(0, sensor_ctrl);
    if (ret) { fprintf(stderr, "API_ISP_SensorRegCb failed: 0x%x\n", ret); return -1; }

    ret = API_ISP_SensorInit();
    if (ret) { fprintf(stderr, "API_ISP_SensorInit failed: 0x%x\n", ret); return -1; }

    /* Set sensor format -- 0x203 = 1080p25 Bayer */
    ret = API_ISP_SetSensorFmt(0x203);
    if (ret) { fprintf(stderr, "API_ISP_SetSensorFmt failed: 0x%x\n", ret); return -1; }

    fprintf(stderr, "[4/8] ISP start...\n");
    ret = API_ISP_Init();
    if (ret) { fprintf(stderr, "API_ISP_Init failed: 0x%x\n", ret); return -1; }

    ret = API_ISP_SensorKick();
    if (ret) { fprintf(stderr, "API_ISP_SensorKick failed: 0x%x\n", ret); return -1; }

    /* Load ISP tuning params if available */
    API_ISP_LoadIspParam("/tmp/fh_libs/cv2003_mipi_attr.hex");

    fprintf(stderr, "[5/8] VENC setup (H.264 1080p)...\n");
    /* Bind VPSS channel 0 -> VENC channel 0 */
    ret = FH_SYS_BindVpu2Enc(0, 0, 0, 0);
    if (ret) { fprintf(stderr, "FH_SYS_BindVpu2Enc failed: 0x%x\n", ret); return -1; }

    /* Create H.264 encoder channel */
    FH_VENC_CHN_CONFIG venc_cfg;
    memset(&venc_cfg, 0, sizeof(venc_cfg));
    venc_cfg.chn_type = 0;      /* H.264 */
    venc_cfg.width = 1920;
    venc_cfg.height = 1080;
    venc_cfg.bps = 2048;        /* 2 Mbps */

    ret = FH_VENC_CreateChn(0, &venc_cfg);
    if (ret) { fprintf(stderr, "FH_VENC_CreateChn failed: 0x%x\n", ret); return -1; }

    /* Set rate control */
    FH_VENC_RC_ATTR rc;
    memset(&rc, 0, sizeof(rc));
    rc.rc_type = 1;     /* VBR */
    rc.init_qp = 30;
    rc.min_qp = 20;
    rc.max_qp = 45;
    rc.bps = 2048;
    FH_VENC_SetRCAttr(0, &rc);

    fprintf(stderr, "[6/8] Start encoding...\n");
    ret = FH_VENC_StartRecvPic(0);
    if (ret) { fprintf(stderr, "FH_VENC_StartRecvPic failed: 0x%x\n", ret); return -1; }

    /* Start ISP processing thread */
    fprintf(stderr, "[7/8] ISP run...\n");
    ret = API_ISP_Run();
    if (ret) { fprintf(stderr, "API_ISP_Run failed: 0x%x\n", ret); return -1; }

    fprintf(stderr, "[8/8] Pipeline ready! Streaming H.264 to stdout...\n");
    fprintf(stderr, "Press Ctrl+C to stop\n");

    return 0;
}

/* ================================================================
 * Main loop -- get encoded frames and write to stdout
 * ================================================================ */

int main(int argc, char **argv)
{
    int ret;
    (void)argc;
    (void)argv;

    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);
    signal(SIGPIPE, SIG_IGN);

    fprintf(stderr, "=== FH8852V201 Mini Streamer ===\n");
    fprintf(stderr, "Sensor: CV2003 @ /dev/i2c-0:0x35\n");
    fprintf(stderr, "Output: raw H.264 NALUs on stdout\n\n");

    ret = init_pipeline();
    if (ret) {
        fprintf(stderr, "Pipeline init failed\n");
        return 1;
    }

    /* Request initial IDR frame */
    FH_VENC_RequestIDR(0);

    FH_VENC_STREAM stream;
    uint32_t frame_count = 0;

    while (g_running) {
        memset(&stream, 0, sizeof(stream));
        ret = FH_VENC_GetStream_Block(0, &stream);
        if (ret) {
            usleep(10000);
            continue;
        }

        if (stream.vaddr && stream.length > 0) {
            /* Write raw H.264 data to stdout */
            size_t written = fwrite(stream.vaddr, 1, stream.length, stdout);
            if (written != stream.length) {
                fprintf(stderr, "Write failed (pipe closed?)\n");
                g_running = 0;
            }
            fflush(stdout);

            frame_count++;
            if (frame_count % 25 == 0) {
                fprintf(stderr, "Frames: %u (last %u bytes, type %u)\n",
                        frame_count, stream.length, stream.type);
            }
        }

        FH_VENC_ReleaseStream(0);
    }

    fprintf(stderr, "\nStopping... (%u frames captured)\n", frame_count);

    FH_VENC_StopRecvPic(0);
    API_ISP_Exit();
    FH_VPSS_CloseChn(0);
    FH_VPSS_Disable(0);
    FH_SYS_Exit();

    return 0;
}
