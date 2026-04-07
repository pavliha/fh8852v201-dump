/**
 * Quick test — verify ioctl wrapper can talk to Fullhan kernel modules.
 * Run on camera: ./test_mpi
 */
#include <stdio.h>
#include <stdint.h>
#include <errno.h>
#include <sys/ioctl.h>
#include "fh_mpi.h"

extern int g_i2c_fd;

int main(void)
{
    int ret;

    printf("=== FH8852V201 MPI Test ===\n\n");

    /* 1. Test system init (open /dev/media_process) */
    printf("[1] fh_sys_init... ");
    ret = fh_sys_init();
    printf("%s (ret=%d)\n", ret == 0 ? "OK" : "FAIL", ret);
    if (ret != 0) return 1;

    /* 2. Test sensor I2C (open /dev/i2c-0, probe CV2003) */
    printf("[2] fh_sensor_init(0x1A)... ");
    ret = fh_sensor_init(0x1A, 2);
    printf("%s (ret=%d)\n", ret == 0 ? "OK" : "FAIL", ret);

    if (ret == 0) {
        /* Try reading a known CV2003 register */
        uint8_t val = 0;

        /* Also try standard Linux i2c-dev as fallback */
        printf("    Testing standard Linux i2c-dev...\n");

        /* Standard i2c_rdwr_ioctl_data with i2c_msg */
        struct {
            uint16_t addr;
            uint16_t flags;
            uint16_t len;
            uint8_t *buf;
        } __attribute__((packed)) i2c_msgs[2];
        struct {
            void *msgs;
            uint32_t nmsgs;
        } i2c_data;

        uint8_t wbuf[2] = { 0x30, 0x00 };  /* reg 0x3000 */
        uint8_t rbuf[1] = { 0 };

        /* Write register address */
        i2c_msgs[0].addr = 0x1A;
        i2c_msgs[0].flags = 0;
        i2c_msgs[0].len = 2;
        i2c_msgs[0].buf = wbuf;
        /* Read data */
        i2c_msgs[1].addr = 0x1A;
        i2c_msgs[1].flags = 1; /* I2C_M_RD */
        i2c_msgs[1].len = 1;
        i2c_msgs[1].buf = rbuf;

        i2c_data.msgs = i2c_msgs;
        i2c_data.nmsgs = 2;

        /* Try standard ioctl 0x0707 (I2C_RDWR) */
        extern int g_i2c_fd;
        ret = ioctl(g_i2c_fd, 0x0707, &i2c_data);
        printf("    std i2c reg 0x3000 = 0x%02x (ret=%d, errno=%d)\n", rbuf[0], ret, ret<0?errno:0);

        /* Try Fullhan custom read */
        ret = fh_sensor_read(0x3000, &val);
        printf("    fh  i2c reg 0x3000 = 0x%02x (ret=%d, errno=%d)\n", val, ret, ret<0?errno:0);

        ret = fh_sensor_read(0x3020, &val);
        printf("    fh  i2c reg 0x3020 (v_cycle_lo) = 0x%02x (ret=%d)\n", val, ret);

        ret = fh_sensor_read(0x3021, &val);
        printf("    fh  i2c reg 0x3021 (v_cycle_hi) = 0x%02x (ret=%d)\n", val, ret);
    }

    /* 3. Test GPIO read (photoresistor on GPIO2) */
    printf("[3] GPIO read... ");
    int light = fh_gpio_read(2);
    printf("GPIO2 (photoresistor) = %d\n", light);

    light = fh_gpio_read(13);
    printf("    GPIO13 (sensor reset) = %d\n", light);

    /* 4. Test watchdog */
    printf("[4] fh_wdt_feed... ");
    /* Don't open watchdog (it's already running from ipcam) */
    /* Just report success */
    printf("SKIP (watchdog owned by ipcam)\n");

    /* Cleanup */
    fh_sensor_close();
    fh_sys_exit();

    printf("\n=== Done ===\n");
    return 0;
}
