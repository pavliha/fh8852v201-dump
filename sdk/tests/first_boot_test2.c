/**
 * First-boot pipeline test v2.
 * Configures media pipeline via /proc/driver/* writes BEFORE ioctl.
 *
 * From Ghidra: _media_driver_config() writes config strings to:
 *   /proc/driver/vpu  — resolution, buffer config
 *   /proc/driver/isp  — WDR, circular buffer
 *   /proc/driver/enc  — stream buffer size
 *   /proc/driver/jpeg — JPEG memory
 *   /proc/driver/clock — NNA clock
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <stdint.h>
#include <errno.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>

#define VPSS_SET_VI_ATTR    0xC0F4696A
#define VPSS_ENABLE         0xC0046971
#define VPSS_SET_CHN_ATTR   0xC00C696C
#define VPSS_OPEN_CHN       0xC0046973
#define VPSS_SET_VO_MODE    0xC00869A4
#define VPSS_GET_CHN_FRAME  0xC0686970
#define VENC_GET_STREAM     0xC1A04D05

#define SENSOR_ADDR 0x35

static FILE *log_fp;
#define LOG(fmt, ...) do { \
    printf(fmt, ##__VA_ARGS__); \
    if (log_fp) { fprintf(log_fp, fmt, ##__VA_ARGS__); fflush(log_fp); } \
} while(0)

static void write_proc(const char *path, const char *value)
{
    int fd = open(path, O_WRONLY);
    if (fd >= 0) {
        write(fd, value, strlen(value));
        close(fd);
        LOG("    %s <- %s\n", path, value);
    } else {
        LOG("    %s FAILED: %s\n", path, strerror(errno));
    }
}

static int sensor_write(int fd, uint16_t reg, uint8_t val)
{
    uint8_t wb[3] = { (reg >> 8) & 0xFF, reg & 0xFF, val };
    struct i2c_msg m = { .addr = SENSOR_ADDR, .flags = 0, .len = 3, .buf = wb };
    struct i2c_rdwr_ioctl_data x = { .msgs = &m, .nmsgs = 1 };
    return ioctl(fd, I2C_RDWR, &x);
}

static int sensor_read(int fd, uint16_t reg, uint8_t *val)
{
    uint8_t wb[2] = { (reg >> 8) & 0xFF, reg & 0xFF };
    uint8_t rb[1] = { 0 };
    struct i2c_msg m[2] = {
        { .addr = SENSOR_ADDR, .flags = 0,        .len = 2, .buf = wb },
        { .addr = SENSOR_ADDR, .flags = I2C_M_RD, .len = 1, .buf = rb },
    };
    struct i2c_rdwr_ioctl_data x = { .msgs = m, .nmsgs = 2 };
    int ret = ioctl(fd, I2C_RDWR, &x);
    if (ret >= 0) *val = rb[0];
    return ret;
}

static const uint16_t cv2003_regs[][2] = {
    {0x3300, 0x03}, {0x3422, 0xbf}, {0x3401, 0x00}, {0x3440, 0x01},
    {0x3442, 0x00}, {0x3806, 0x00}, {0x3908, 0x5f}, {0x3909, 0x00},
    {0x3929, 0x01}, {0x3158, 0x01}, {0x3159, 0x01}, {0x315a, 0x01},
    {0x315b, 0x01}, {0x35b3, 0x15}, {0x3148, 0x64}, {0x3031, 0x00},
    {0x3118, 0x01}, {0x3119, 0x06}, {0x3670, 0x00}, {0x3679, 0x02},
    {0x3330, 0x00}, {0x320e, 0x02}, {0x3804, 0x10}, {0x35a1, 0x06},
    {0x35a8, 0x06}, {0x35a9, 0x06}, {0x35aa, 0x06}, {0x35ab, 0x06},
    {0x35ac, 0x06}, {0x35ad, 0x06}, {0x35ae, 0x07}, {0x35af, 0x07},
    {0x333b, 0x01}, {0x3338, 0x08}, {0x3339, 0x00}, {0x3144, 0x20},
    {0x3030, 0x01}, {0x3020, 0x8c}, {0x3021, 0x0a}, {0x3024, 0xd0},
    {0x3025, 0x02}, {0x3038, 0x06}, {0x3039, 0x00}, {0x303a, 0x80},
    {0x303b, 0x07}, {0x3034, 0x04}, {0x3035, 0x00}, {0x3036, 0x38},
    {0x3037, 0x04}, {0x3908, 0x51}, {0x390a, 0x02}, {0x3000, 0x00},
};

int main(void)
{
    int mfd, i2c, ret;
    int pass = 0, fail = 0;

    log_fp = fopen("/tmp/pipeline_results.txt", "w");

    LOG("=== FH8852V201 First-Boot Pipeline Test v2 ===\n");
    LOG("PID: %d\n\n", getpid());

    /* STEP 1: Configure media pipeline via /proc writes */
    LOG("[1] Configuring media pipeline via /proc/driver/*...\n");

    /* VPU config (from _media_driver_config) */
    write_proc("/proc/driver/vpu", "vi_1920_1080");
    write_proc("/proc/driver/vpu", "cap_0_1920_1080");
    write_proc("/proc/driver/vpu", "cap_1_800_576");
    write_proc("/proc/driver/vpu", "cap_2_512_288");
    write_proc("/proc/driver/vpu", "buf_0_2");
    write_proc("/proc/driver/vpu", "buf_1_3");
    write_proc("/proc/driver/vpu", "buf_2_1");
    write_proc("/proc/driver/vpu", "cirbuf_on");
    write_proc("/proc/driver/vpu", "cbufsize_80");

    /* ISP config */
    write_proc("/proc/driver/isp", "wdr_off");
    write_proc("/proc/driver/isp", "cir_on");

    /* Encoder config */
    write_proc("/proc/driver/enc", "stm_1572864");

    /* JPEG config */
    write_proc("/proc/driver/jpeg", "mem_1_131072");
    write_proc("/proc/driver/jpeg", "mjpg_0_0");

    /* NNA clock */
    write_proc("/proc/driver/clock", "nn_clk,enable,90000000");

    LOG("    Config written\n");

    /* STEP 2: Open /dev/media_process */
    LOG("\n[2] Opening /dev/media_process...\n");
    mfd = open("/dev/media_process", O_RDWR);
    if (mfd < 0) {
        LOG("    FAILED: %s\n", strerror(errno));
        fail++;
        goto done;
    }
    LOG("    OK (fd=%d)\n", mfd);
    pass++;

    /* STEP 3: VPSS init sequence (from decompiled FUN_00295168) */
    LOG("\n[3] VPSS init...\n");

    uint32_t vi_attr[62];
    memset(vi_attr, 0, sizeof(vi_attr));
    vi_attr[0] = 1920;
    vi_attr[1] = 1080;
    ret = ioctl(mfd, VPSS_SET_VI_ATTR, vi_attr);
    LOG("    SET_VI_ATTR: 0x%08x\n", (uint32_t)ret);
    if (ret == 0) pass++; else fail++;

    uint32_t enable = 0;
    ret = ioctl(mfd, VPSS_ENABLE, &enable);
    LOG("    ENABLE: 0x%08x\n", (uint32_t)ret);
    if (ret == 0) pass++; else fail++;

    /* Open channel 0: 1920x1080 */
    uint32_t ca[3] = { 0, 1920, 1080 };
    ret = ioctl(mfd, VPSS_SET_CHN_ATTR, ca);
    LOG("    SET_CHN_ATTR(0): 0x%08x\n", (uint32_t)ret);
    if (ret == 0) pass++; else fail++;

    uint32_t chn = 0;
    ret = ioctl(mfd, VPSS_OPEN_CHN, &chn);
    LOG("    OPEN_CHN(0): 0x%08x\n", (uint32_t)ret);
    if (ret == 0) pass++; else fail++;

    uint32_t vo[2] = { 0, 1 };
    ret = ioctl(mfd, VPSS_SET_VO_MODE, vo);
    LOG("    SET_VO_MODE(0,1): 0x%08x\n", (uint32_t)ret);
    if (ret == 0) pass++; else fail++;

    /* STEP 4: Sensor init */
    LOG("\n[4] Sensor init...\n");
    i2c = open("/dev/i2c-0", O_RDWR);
    if (i2c >= 0) {
        int ok = 0;
        for (int i = 0; i < 52; i++) {
            if (sensor_write(i2c, cv2003_regs[i][0], cv2003_regs[i][1]) >= 0) ok++;
        }
        LOG("    Regs: %d/52\n", ok);
        if (ok == 52) pass++; else fail++;

        usleep(10000);
        sensor_write(i2c, 0x3141, 0x01);

        uint8_t vl=0, vh=0;
        sensor_read(i2c, 0x3020, &vl);
        sensor_read(i2c, 0x3021, &vh);
        int vcycle = (vh << 8) | vl;
        LOG("    V-cycle: %d (expect 2700)\n", vcycle);
        if (vcycle == 0x0a8c) pass++; else fail++;
        close(i2c);
    } else {
        LOG("    I2C FAILED\n");
        fail += 2;
    }

    /* STEP 5: Wait for ISP to produce frames, then try GET_FRAME */
    LOG("\n[5] Waiting 500ms for ISP frames...\n");
    usleep(500000);

    for (int ch = 0; ch < 3; ch++) {
        uint32_t frame[26];
        memset(frame, 0, sizeof(frame));
        frame[0] = ch;
        ret = ioctl(mfd, VPSS_GET_CHN_FRAME, frame);
        LOG("    GET_FRAME(ch%d): 0x%08x", ch, (uint32_t)ret);
        if (ret == 0) {
            pass++;
            LOG(" === GOT FRAME! ===\n");
            for (int i = 0; i < 12; i++)
                LOG("      [%2d] 0x%08x\n", i, frame[i]);
        } else {
            fail++;
            LOG(" (errno=%d)\n", errno);
        }
    }

    /* STEP 6: Try VENC */
    LOG("\n[6] VENC_GET_STREAM...\n");
    uint32_t stream[105];
    memset(stream, 0, sizeof(stream));
    stream[0] = 0;
    ret = ioctl(mfd, VENC_GET_STREAM, stream);
    LOG("    ret=0x%08x\n", (uint32_t)ret);
    if (ret == 0) {
        pass++;
        LOG("    === GOT ENCODED! type=%u size=%u ===\n", stream[0], stream[2]);
    } else {
        fail++;
    }

    /* STEP 7: Read pipeline status */
    LOG("\n[7] Pipeline status after init:\n");
    system("cat /proc/driver/media_process 2>/dev/null >> /tmp/pipeline_results.txt");
    system("cat /proc/driver/isp 2>/dev/null >> /tmp/pipeline_results.txt");
    system("cat /proc/driver/vpu 2>/dev/null >> /tmp/pipeline_results.txt");

    close(mfd);

done:
    LOG("\n========================================\n");
    LOG("RESULTS: %d PASS, %d FAIL\n", pass, fail);
    LOG("========================================\n");

    if (log_fp) fclose(log_fp);

    /* Upload results via TFTP */
    system("tftp -l /tmp/pipeline_results.txt -r pipeline_results.txt -p 192.168.8.100 2>/dev/null");

    return 0;
}
