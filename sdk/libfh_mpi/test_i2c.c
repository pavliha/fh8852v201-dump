/**
 * Minimal I2C test — uses standard Linux i2c-dev API.
 * Tests reading CV2003 sensor registers.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>

#define CV2003_ADDR 0x1A

int main(void)
{
    int fd, ret;

    printf("=== CV2003 I2C Register Test ===\n\n");

    fd = open("/dev/i2c-0", O_RDWR);
    if (fd < 0) {
        perror("open /dev/i2c-0");
        return 1;
    }
    printf("Opened /dev/i2c-0 (fd=%d)\n", fd);

    /* Set slave address */
    ret = ioctl(fd, I2C_SLAVE, CV2003_ADDR);
    printf("I2C_SLAVE(0x%02x) = %d (errno=%d)\n", CV2003_ADDR, ret, ret<0?errno:0);

    /* Try simple read via I2C_SLAVE + read() */
    /* First write register address */
    uint8_t reg_addr[2] = { 0x30, 0x00 };  /* reg 0x3000 */
    ret = write(fd, reg_addr, 2);
    printf("write reg addr 0x3000: ret=%d errno=%d\n", ret, ret<0?errno:0);

    uint8_t val = 0xFF;
    ret = read(fd, &val, 1);
    printf("read data: ret=%d val=0x%02x errno=%d\n", ret, val, ret<0?errno:0);

    /* Try I2C_RDWR */
    printf("\nTrying I2C_RDWR...\n");

    struct i2c_msg msgs[2];
    struct i2c_rdwr_ioctl_data xfer;

    uint8_t wbuf[2] = { 0x30, 0x20 };  /* reg 0x3020 */
    uint8_t rbuf[1] = { 0 };

    msgs[0].addr = CV2003_ADDR;
    msgs[0].flags = 0;
    msgs[0].len = 2;
    msgs[0].buf = wbuf;

    msgs[1].addr = CV2003_ADDR;
    msgs[1].flags = I2C_M_RD;
    msgs[1].len = 1;
    msgs[1].buf = rbuf;

    xfer.msgs = msgs;
    xfer.nmsgs = 2;

    ret = ioctl(fd, I2C_RDWR, &xfer);
    printf("I2C_RDWR reg 0x3020: ret=%d val=0x%02x errno=%d\n",
           ret, rbuf[0], ret<0?errno:0);

    /* Try a few more registers */
    uint16_t regs[] = { 0x3021, 0x3000, 0x3141 };
    for (int i = 0; i < 3; i++) {
        wbuf[0] = (regs[i] >> 8) & 0xFF;
        wbuf[1] = regs[i] & 0xFF;
        rbuf[0] = 0;

        msgs[0].buf = wbuf;
        msgs[1].buf = rbuf;
        xfer.msgs = msgs;
        xfer.nmsgs = 2;

        ret = ioctl(fd, I2C_RDWR, &xfer);
        printf("I2C_RDWR reg 0x%04x: ret=%d val=0x%02x errno=%d\n",
               regs[i], ret, rbuf[0], ret<0?errno:0);
    }

    /* Print sizeof struct for debugging */
    printf("\nsizeof(struct i2c_msg) = %zu\n", sizeof(struct i2c_msg));
    printf("sizeof(struct i2c_rdwr_ioctl_data) = %zu\n", sizeof(struct i2c_rdwr_ioctl_data));

    close(fd);
    printf("\n=== Done ===\n");
    return 0;
}
