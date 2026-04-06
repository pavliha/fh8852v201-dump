#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>
#include <stdint.h>

#define SENSOR_ADDR 0x35

static int i2c_read(int fd, uint16_t reg, uint8_t *val)
{
    uint8_t wb[2] = { (reg >> 8) & 0xFF, reg & 0xFF };
    uint8_t rb[1] = { 0 };
    struct i2c_msg m[2] = {
        { .addr = SENSOR_ADDR, .flags = 0, .len = 2, .buf = wb },
        { .addr = SENSOR_ADDR, .flags = I2C_M_RD, .len = 1, .buf = rb },
    };
    struct i2c_rdwr_ioctl_data x = { .msgs = m, .nmsgs = 2 };
    int ret = ioctl(fd, I2C_RDWR, &x);
    if (ret >= 0) *val = rb[0];
    return ret;
}

int main(void)
{
    int fd = open("/dev/i2c-0", O_RDWR);
    if (fd < 0) { perror("open"); return 1; }

    printf("// CV2003 (GC4201AC) live register dump\n");
    printf("// I2C addr: 0x35, sensor running at 1920x1080\n\n");

    /* Dump key register ranges from the init tables */
    uint16_t regs[] = {
        /* From init table */
        0x3000, 0x3020, 0x3021, 0x3024, 0x3025, 0x3030, 0x3031,
        0x3034, 0x3035, 0x3036, 0x3037, 0x3038, 0x3039, 0x303a, 0x303b,
        0x303c, 0x303d, 0x303e, 0x303f,
        0x3118, 0x3119, 0x3141, 0x3144, 0x3148,
        0x3158, 0x3159, 0x315a, 0x315b,
        0x301c, 0x320e,
        0x3300, 0x3330, 0x3338, 0x3339, 0x333b,
        0x3401, 0x3422, 0x3440, 0x3442,
        0x3670, 0x3679,
        0x3804, 0x3806,
        0x3908, 0x3909, 0x390a, 0x3929,
        0x35a1, 0x35a8, 0x35a9, 0x35aa, 0x35ab, 0x35ac, 0x35ad, 0x35ae, 0x35af,
        0x35b3,
    };
    int n = sizeof(regs) / sizeof(regs[0]);

    printf("// Key registers from init table:\n");
    for (int i = 0; i < n; i++) {
        uint8_t val = 0;
        int ret = i2c_read(fd, regs[i], &val);
        if (ret >= 0)
            printf("{0x%04x, 0x%02x},  ", regs[i], val);
        else
            printf("{0x%04x, ERR},  ", regs[i]);
        if ((i + 1) % 6 == 0) printf("\n");
    }
    printf("\n\n");

    /* Dump continuous range 0x3000-0x30FF for full register map */
    printf("// Full register dump 0x3000-0x30FF:\n");
    for (uint16_t r = 0x3000; r <= 0x30FF; r++) {
        uint8_t val = 0;
        int ret = i2c_read(fd, r, &val);
        if (r % 16 == 0) printf("// 0x%04x:", r);
        if (ret >= 0)
            printf(" %02x", val);
        else
            printf(" --");
        if ((r + 1) % 16 == 0) printf("\n");
    }

    /* Also dump 0x3100-0x31FF and 0x3900-0x39FF */
    printf("\n// Register dump 0x3100-0x31FF:\n");
    for (uint16_t r = 0x3100; r <= 0x31FF; r++) {
        uint8_t val = 0;
        int ret = i2c_read(fd, r, &val);
        if (r % 16 == 0) printf("// 0x%04x:", r);
        if (ret >= 0) printf(" %02x", val);
        else printf(" --");
        if ((r + 1) % 16 == 0) printf("\n");
    }

    printf("\n// Register dump 0x3900-0x39FF:\n");
    for (uint16_t r = 0x3900; r <= 0x39FF; r++) {
        uint8_t val = 0;
        int ret = i2c_read(fd, r, &val);
        if (r % 16 == 0) printf("// 0x%04x:", r);
        if (ret >= 0) printf(" %02x", val);
        else printf(" --");
        if ((r + 1) % 16 == 0) printf("\n");
    }

    close(fd);
    printf("\n// Done\n");
    return 0;
}
