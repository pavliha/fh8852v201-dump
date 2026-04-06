#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>
#include <stdint.h>
#include <errno.h>

int main(void)
{
    int fd, ret, i;
    uint8_t wb[2], rb[1];
    struct i2c_msg m[2];
    struct i2c_rdwr_ioctl_data x;

    fd = open("/dev/i2c-0", O_RDWR);
    if (fd < 0) { perror("open"); return 1; }

    printf("Scanning I2C bus 0 for sensor...\n");
    for (i = 0x03; i < 0x78; i++) {
        wb[0] = 0x30; wb[1] = 0x00; rb[0] = 0;
        m[0].addr = i; m[0].flags = 0; m[0].len = 2; m[0].buf = wb;
        m[1].addr = i; m[1].flags = I2C_M_RD; m[1].len = 1; m[1].buf = rb;
        x.msgs = m; x.nmsgs = 2;
        ret = ioctl(fd, I2C_RDWR, &x);
        if (ret >= 0) {
            printf("  FOUND device at 0x%02x! reg 0x3000 = 0x%02x\n", i, rb[0]);
        }
    }

    /* Also try write-only probe (just address, no data) */
    printf("\nWrite-probe scan...\n");
    for (i = 0x03; i < 0x78; i++) {
        ret = ioctl(fd, I2C_SLAVE, i);
        if (ret < 0) continue;
        uint8_t dummy = 0;
        ret = write(fd, &dummy, 0);
        if (ret >= 0) {
            printf("  ACK at 0x%02x\n", i);
        }
    }

    close(fd);
    return 0;
}
