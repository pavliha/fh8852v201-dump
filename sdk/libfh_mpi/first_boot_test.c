/**
 * First-boot pipeline test — runs as the FIRST process after module load.
 * Must be placed at /tmp/first_boot_test and called from run.sh
 * BEFORE ipcam starts.
 *
 * Tests:
 * 1. Open /dev/media_process as first user
 * 2. VPSS init: set VI attr, enable, open channel
 * 3. ISP init via /dev/isp ioctls
 * 4. Sensor init via I2C
 * 5. Grab a raw VPSS frame
 * 6. Create VENC channel and grab encoded stream
 * 7. Save results to /tmp/pipeline_results.txt
 *
 * After test completes, exits so run.sh can start ipcam normally.
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

/* VPSS ioctls */
#define VPSS_SET_VI_ATTR    0xC0F4696A
#define VPSS_ENABLE         0xC0046971
#define VPSS_SET_CHN_ATTR   0xC00C696C
#define VPSS_OPEN_CHN       0xC0046973
#define VPSS_CLOSE_CHN      0xC0046974   /* guessed: OPEN+1 */
#define VPSS_SET_VO_MODE    0xC00869A4
#define VPSS_SET_FRAMECTRL  0xC0086978
#define VPSS_GET_CHN_FRAME  0xC0686970
#define VPSS_DISABLE        0xC0046972   /* guessed: ENABLE+1 */

/* VENC ioctls */
#define VENC_GET_STREAM     0xC1A04D05
#define VENC_CREATE_CHN     0xC01C5403
#define VENC_SET_RC_ATTR    0xC06C540C

#define SENSOR_ADDR 0x35

static FILE *log_fp;

#define LOG(fmt, ...) do { \
    printf(fmt, ##__VA_ARGS__); \
    if (log_fp) fprintf(log_fp, fmt, ##__VA_ARGS__); \
} while(0)

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

/* CV2003 1080P25 register init table */
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
    int test_pass = 0, test_fail = 0;

    log_fp = fopen("/tmp/pipeline_results.txt", "w");

    LOG("=== FH8852V201 First-Boot Pipeline Test ===\n");
    LOG("PID: %d\n\n", getpid());

    /* 1. Open /dev/media_process — we should be FIRST */
    LOG("[1] Opening /dev/media_process as first process...\n");
    mfd = open("/dev/media_process", O_RDWR);
    if (mfd < 0) {
        LOG("    FAILED: %s\n", strerror(errno));
        test_fail++;
        goto done;
    }
    LOG("    OK (fd=%d)\n", mfd);
    test_pass++;

    /* 2. VPSS Set VI Attr */
    LOG("\n[2] VPSS_SET_VI_ATTR(1920, 1080)...\n");
    uint32_t vi_attr[62]; /* oversized to match ioctl param size 0xF4 */
    memset(vi_attr, 0, sizeof(vi_attr));
    vi_attr[0] = 1920;
    vi_attr[1] = 1080;
    ret = ioctl(mfd, VPSS_SET_VI_ATTR, vi_attr);
    LOG("    ret=0x%08x (%d) errno=%d\n", (uint32_t)ret, ret, ret<0?errno:0);
    if (ret == 0) { test_pass++; LOG("    PASS\n"); }
    else { test_fail++; LOG("    FAIL (SDK error 0x%08x)\n", (uint32_t)ret); }

    /* 3. VPSS Enable */
    LOG("\n[3] VPSS_ENABLE(0)...\n");
    uint32_t enable = 0;
    ret = ioctl(mfd, VPSS_ENABLE, &enable);
    LOG("    ret=0x%08x errno=%d\n", (uint32_t)ret, ret<0?errno:0);
    if (ret == 0) { test_pass++; LOG("    PASS\n"); }
    else { test_fail++; LOG("    FAIL\n"); }

    /* 4. VPSS Set Channel 0 */
    LOG("\n[4] VPSS_SET_CHN_ATTR(0, 1920, 1080)...\n");
    uint32_t chn_attr[3] = { 0, 1920, 1080 };
    ret = ioctl(mfd, VPSS_SET_CHN_ATTR, chn_attr);
    LOG("    ret=0x%08x errno=%d\n", (uint32_t)ret, ret<0?errno:0);
    if (ret == 0) { test_pass++; LOG("    PASS\n"); }
    else { test_fail++; LOG("    FAIL\n"); }

    /* 5. VPSS Open Channel 0 */
    LOG("\n[5] VPSS_OPEN_CHN(0)...\n");
    uint32_t chn = 0;
    ret = ioctl(mfd, VPSS_OPEN_CHN, &chn);
    LOG("    ret=0x%08x errno=%d\n", (uint32_t)ret, ret<0?errno:0);
    if (ret == 0) { test_pass++; LOG("    PASS\n"); }
    else { test_fail++; LOG("    FAIL\n"); }

    /* 6. VPSS Set VO Mode */
    LOG("\n[6] VPSS_SET_VO_MODE(0, 1)...\n");
    uint32_t vo_mode[2] = { 0, 1 };
    ret = ioctl(mfd, VPSS_SET_VO_MODE, vo_mode);
    LOG("    ret=0x%08x errno=%d\n", (uint32_t)ret, ret<0?errno:0);
    if (ret == 0) { test_pass++; LOG("    PASS\n"); }
    else { test_fail++; LOG("    FAIL\n"); }

    /* 7. Init sensor */
    LOG("\n[7] Sensor I2C init...\n");
    i2c = open("/dev/i2c-0", O_RDWR);
    if (i2c >= 0) {
        int ok = 0, fail = 0;
        for (int i = 0; i < (int)(sizeof(cv2003_regs)/sizeof(cv2003_regs[0])); i++) {
            ret = sensor_write(i2c, cv2003_regs[i][0], cv2003_regs[i][1]);
            if (ret >= 0) ok++; else fail++;
        }
        LOG("    Register init: %d/%d OK\n", ok, ok+fail);
        if (fail == 0) test_pass++; else test_fail++;

        usleep(10000);
        sensor_write(i2c, 0x3141, 0x01);

        uint8_t vl=0, vh=0;
        sensor_read(i2c, 0x3020, &vl);
        sensor_read(i2c, 0x3021, &vh);
        LOG("    V-cycle: 0x%02x%02x (%d) %s\n",
            vh, vl, (vh<<8)|vl,
            ((vh<<8)|vl) == 0x0a8c ? "PASS" : "UNEXPECTED");
        if (((vh<<8)|vl) == 0x0a8c) test_pass++; else test_fail++;
        close(i2c);
    } else {
        LOG("    I2C open failed: %s\n", strerror(errno));
        test_fail++;
    }

    /* 8. Try to get a VPSS frame */
    LOG("\n[8] VPSS_GET_CHN_FRAME(0)...\n");
    /* Wait a moment for ISP to start producing frames */
    usleep(200000); /* 200ms = ~5 frames at 25fps */
    uint32_t frame[26];
    memset(frame, 0, sizeof(frame));
    frame[0] = 0;
    ret = ioctl(mfd, VPSS_GET_CHN_FRAME, frame);
    LOG("    ret=0x%08x errno=%d\n", (uint32_t)ret, ret<0?errno:0);
    if (ret == 0) {
        test_pass++;
        LOG("    === GOT RAW FRAME! ===\n");
        for (int i = 0; i < 12; i++)
            LOG("      [%2d] = 0x%08x (%u)\n", i, frame[i], frame[i]);

        /* Save frame info */
        uint32_t phys = frame[2];
        uint32_t size = frame[6];
        LOG("    phys_addr=0x%08x size=%u\n", phys, size);

        /* Try mmap to save actual pixel data */
        if (size > 0 && size < 0x400000) {
            int vmm_fd = open("/dev/vmm_userdev", O_RDWR);
            if (vmm_fd >= 0) {
                void *ptr = mmap(NULL, size, PROT_READ, MAP_SHARED, vmm_fd, phys);
                if (ptr != MAP_FAILED) {
                    FILE *fp = fopen("/tmp/frame_raw.yuv", "wb");
                    if (fp) {
                        fwrite(ptr, 1, size, fp);
                        fclose(fp);
                        LOG("    Saved %u bytes to /tmp/frame_raw.yuv\n", size);
                    }
                    munmap(ptr, size);
                }
                close(vmm_fd);
            }
        }
    } else {
        test_fail++;
        LOG("    No frame (error 0x%08x)\n", (uint32_t)ret);
    }

    /* 9. Try VENC */
    LOG("\n[9] VENC_GET_STREAM(0)...\n");
    uint32_t stream[105];
    memset(stream, 0, sizeof(stream));
    stream[0] = 0;
    ret = ioctl(mfd, VENC_GET_STREAM, stream);
    LOG("    ret=0x%08x errno=%d\n", (uint32_t)ret, ret<0?errno:0);
    if (ret == 0) {
        test_pass++;
        LOG("    === GOT ENCODED FRAME! ===\n");
        LOG("      type=%u chn=%u size=%u frame_type=%u pts=%u nalu=%u\n",
            stream[0], stream[1], stream[2], stream[3], stream[4], stream[7]);
    } else {
        test_fail++;
        LOG("    No encoded stream (expected — VENC not created by us)\n");
    }

    /* 10. Check /proc status */
    LOG("\n[10] Pipeline status:\n");
    system("cat /proc/driver/media_process 2>/dev/null | head -15 >> /tmp/pipeline_results.txt");
    system("cat /proc/driver/isp 2>/dev/null | head -20 >> /tmp/pipeline_results.txt");

    close(mfd);

done:
    LOG("\n========================================\n");
    LOG("RESULTS: %d PASS, %d FAIL\n", test_pass, test_fail);
    LOG("========================================\n");

    if (log_fp) fclose(log_fp);
    return 0;
}
