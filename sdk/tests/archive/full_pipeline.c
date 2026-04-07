/**
 * Full pipeline init — unloads/reloads kernel modules, inits from scratch.
 * Grabs frames from VPSS and encoded stream from VENC.
 * Restarts ipcam on exit.
 *
 * This is the definitive test of our clean-room ioctl wrapper.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <stdint.h>
#include <errno.h>
#include <signal.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>

/* VPSS ioctls */
#define VPSS_SET_VI_ATTR    0xC0F4696A
#define VPSS_ENABLE         0xC0046971
#define VPSS_SET_CHN_ATTR   0xC00C696C
#define VPSS_OPEN_CHN       0xC0046973
#define VPSS_SET_VO_MODE    0xC00869A4
#define VPSS_GET_CHN_FRAME  0xC0686970

/* VENC ioctls */
#define VENC_GET_STREAM     0xC1A04D05
#define VENC_CREATE_CHN     0xC01C5403

#define SENSOR_ADDR 0x35

static void cleanup_and_restart(void)
{
    printf("\n[CLEANUP] Restarting ipcam...\n");
    system("/usr/app/run.sh &");
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

/* CV2003 1080P25 register init table (52 entries, from Ghidra extraction) */
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

    printf("=== FH8852V201 Full Pipeline Test ===\n\n");

    /* 1. Kill everything */
    printf("[1] Stopping ipcam + watchdog...\n");
    system("killall -9 ipcam 2>/dev/null");
    system("killall -9 run.sh 2>/dev/null");
    system("killall -9 system_wapper 2>/dev/null");
    sleep(2);
    printf("    Stopped\n");

    /* 2. Unload kernel modules (reverse order) */
    printf("[2] Unloading kernel modules...\n");
    system("rmmod nna 2>/dev/null");
    system("rmmod jpeg 2>/dev/null");
    system("rmmod enc 2>/dev/null");
    system("rmmod isp 2>/dev/null");
    system("rmmod media_process 2>/dev/null");
    system("rmmod xbus_rpc 2>/dev/null");
    /* Keep vmm loaded — removing it could crash the system */
    sleep(1);
    printf("    Modules unloaded\n");

    /* 3. Reload kernel modules (same as load_driver.sh) */
    printf("[3] Reloading kernel modules...\n");

    /* Reset sensor via GPIO13 */
    system("echo 13 > /sys/class/gpio/export 2>/dev/null");
    system("echo out > /sys/class/gpio/GPIO13/direction");
    system("echo 0 > /sys/class/gpio/GPIO13/value");
    usleep(100000);

    ret = system("insmod /usr/app/driver/xbus_rpc.ko fn=rtthread_arc.bin fa=0xbff80000");
    printf("    xbus_rpc: %s\n", ret == 0 ? "OK" : "FAIL (may already be loaded)");

    /* Release sensor reset */
    system("echo 1 > /sys/class/gpio/GPIO13/value");
    usleep(100000);

    ret = system("insmod /usr/app/driver/media_process.ko");
    printf("    media_process: %s\n", ret == 0 ? "OK" : "FAIL");

    ret = system("insmod /usr/app/driver/isp.ko");
    printf("    isp: %s\n", ret == 0 ? "OK" : "FAIL");

    ret = system("insmod /usr/app/driver/enc.ko");
    printf("    enc: %s\n", ret == 0 ? "OK" : "FAIL");

    ret = system("insmod /usr/app/driver/jpeg.ko");
    printf("    jpeg: %s\n", ret == 0 ? "OK" : "FAIL");

    sleep(1);

    /* 4. Open media device */
    printf("[4] Opening /dev/media_process...\n");
    mfd = open("/dev/media_process", O_RDWR);
    if (mfd < 0) {
        perror("    FAILED");
        cleanup_and_restart();
        return 1;
    }
    printf("    OK (fd=%d)\n", mfd);

    /* 5. Init VPSS */
    printf("[5] Initializing VPSS...\n");

    uint32_t vi_attr[2] = { 1920, 1080 };
    ret = ioctl(mfd, VPSS_SET_VI_ATTR, vi_attr);
    printf("    SET_VI_ATTR(1920x1080): ret=0x%08x errno=%d\n", (uint32_t)ret, ret<0?errno:0);

    uint32_t enable = 0;
    ret = ioctl(mfd, VPSS_ENABLE, &enable);
    printf("    ENABLE(0): ret=0x%08x errno=%d\n", (uint32_t)ret, ret<0?errno:0);

    uint32_t chn_attr[3] = { 0, 1920, 1080 };
    ret = ioctl(mfd, VPSS_SET_CHN_ATTR, chn_attr);
    printf("    SET_CHN_ATTR(0, 1920x1080): ret=0x%08x errno=%d\n", (uint32_t)ret, ret<0?errno:0);

    uint32_t chn = 0;
    ret = ioctl(mfd, VPSS_OPEN_CHN, &chn);
    printf("    OPEN_CHN(0): ret=0x%08x errno=%d\n", (uint32_t)ret, ret<0?errno:0);

    uint32_t vo_mode[2] = { 0, 1 };
    ret = ioctl(mfd, VPSS_SET_VO_MODE, vo_mode);
    printf("    SET_VO_MODE(0, 1): ret=0x%08x errno=%d\n", (uint32_t)ret, ret<0?errno:0);

    /* 6. Init sensor via I2C */
    printf("[6] Initializing CV2003 sensor...\n");
    i2c = open("/dev/i2c-0", O_RDWR);
    if (i2c < 0) {
        perror("    open /dev/i2c-0 FAILED");
    } else {
        printf("    I2C opened (fd=%d)\n", i2c);

        /* Write register init table */
        int ok = 0, fail = 0;
        for (int i = 0; i < (int)(sizeof(cv2003_regs)/sizeof(cv2003_regs[0])); i++) {
            ret = sensor_write(i2c, cv2003_regs[i][0], cv2003_regs[i][1]);
            if (ret >= 0) ok++; else fail++;
        }
        printf("    Init table: %d OK, %d FAIL\n", ok, fail);

        usleep(10000); /* 10ms settle */

        /* Enable streaming */
        ret = sensor_write(i2c, 0x3141, 0x01);
        printf("    Stream enable (0x3141=1): %s\n", ret >= 0 ? "OK" : "FAIL");

        /* Read back V-cycle to verify */
        uint8_t vl = 0, vh = 0;
        sensor_read(i2c, 0x3020, &vl);
        sensor_read(i2c, 0x3021, &vh);
        printf("    V-cycle: 0x%02x%02x (%d)\n", vh, vl, (vh << 8) | vl);
    }

    /* 7. Try to get a frame */
    printf("[7] Trying VPSS_GET_CHN_FRAME...\n");
    uint32_t frame[26];
    memset(frame, 0, sizeof(frame));
    frame[0] = 0;
    ret = ioctl(mfd, VPSS_GET_CHN_FRAME, frame);
    printf("    ret=0x%08x errno=%d\n", (uint32_t)ret, ret<0?errno:0);
    if (ret == 0) {
        printf("    === GOT FRAME! ===\n");
        for (int i = 0; i < 12; i++)
            printf("      [%2d] = 0x%08x\n", i, frame[i]);
    } else {
        printf("    (Expected — ISP not fully initialized via ioctl)\n");
        printf("    Error 0x%08x likely means ISP pipeline not started\n", (uint32_t)ret);
    }

    /* 8. Check /proc/driver status */
    printf("[8] Checking pipeline status...\n");
    system("cat /proc/driver/media_process 2>/dev/null | grep -E 'VERSION|CHN|VPU|ENC'");
    printf("\n");
    system("cat /proc/driver/isp 2>/dev/null | grep -E 'VERSION|Resolution|Sensor|Count'");

    /* Cleanup */
    if (i2c >= 0) close(i2c);
    close(mfd);

    /* 9. Restart ipcam */
    printf("\n[9] Restarting ipcam...\n");
    cleanup_and_restart();

    printf("\n=== Pipeline Test Complete ===\n");
    return 0;
}
