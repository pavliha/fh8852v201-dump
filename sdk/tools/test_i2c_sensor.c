// Compile: arm-fullhanv3-linux-uclibcgnueabi-gcc -o test_i2c_sensor test_i2c_sensor.c
//
// Mimics the exact I2C sequence from the ipcam binary (Ghidra decompilation):
//   1. SensorDevice_Init: open("/dev/i2c-0", O_RDWR | O_NONBLOCK)  [flags=0x802]
//   2. ioctl(fd, I2C_TENBIT, 0)      — disable 10-bit addressing
//   3. ioctl(fd, I2C_SLAVE, addr)     — set 7-bit slave address
//   4. ioctl(fd, I2C_RDWR, &data)     — combined write+read transfer
//
// CV2003 sensor (addr_mode=2: 16-bit register address, 8-bit data):
//   - Detect: read reg 0x307a (expect PID1=0x02 or 0x20), reg 0x3138 (expect PID2=0x03)
//   - Streaming: write reg 0x3141 = 0x01
//   - V_cycle: (reg 0x3021 << 8) | reg 0x3020

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>

// I2C ioctl numbers (matching ipcam binary constants)
#define IPCAM_I2C_TENBIT  0x0704
#define IPCAM_I2C_SLAVE   0x0706
#define IPCAM_I2C_RDWR    0x0707

// CV2003 expected product IDs
#define CV2003_PID1_A  0x02
#define CV2003_PID1_B  0x20
#define CV2003_PID2    0x03

// CV2003 registers
#define REG_PID1    0x307a
#define REG_PID2    0x3138
#define REG_STREAM  0x3141
#define REG_VCYC_LO 0x3020
#define REG_VCYC_HI 0x3021

static const char *i2c_buses[] = {
    "/dev/i2c-0",
    "/dev/i2c-1",
    "/dev/i2c-2",
};
#define NUM_BUSES (sizeof(i2c_buses) / sizeof(i2c_buses[0]))

static const uint8_t slave_addrs[] = { 0x35, 0x36 };
#define NUM_ADDRS (sizeof(slave_addrs) / sizeof(slave_addrs[0]))

// Read one byte from a 16-bit register using I2C_RDWR (mode=2: 16-bit addr, 8-bit data)
// This is the exact transfer format the ipcam binary uses.
static int sensor_read_reg(int fd, uint8_t slave_addr, uint16_t reg, uint8_t *val)
{
    uint8_t wb[2];
    uint8_t rb[1] = { 0 };
    struct i2c_msg msgs[2];
    struct i2c_rdwr_ioctl_data rdwr_data;

    // Write phase: send 16-bit register address (big-endian)
    wb[0] = (reg >> 8) & 0xFF;
    wb[1] = reg & 0xFF;

    msgs[0].addr  = slave_addr;
    msgs[0].flags = 0;          // write
    msgs[0].len   = 2;
    msgs[0].buf   = wb;

    // Read phase: read 1 byte of data
    msgs[1].addr  = slave_addr;
    msgs[1].flags = I2C_M_RD;   // read
    msgs[1].len   = 1;
    msgs[1].buf   = rb;

    rdwr_data.msgs  = msgs;
    rdwr_data.nmsgs = 2;

    int ret = ioctl(fd, IPCAM_I2C_RDWR, &rdwr_data);
    if (ret < 0)
        return -1;

    *val = rb[0];
    return 0;
}

// Write one byte to a 16-bit register using I2C_RDWR (mode=2: 16-bit addr, 8-bit data)
static int sensor_write_reg(int fd, uint8_t slave_addr, uint16_t reg, uint8_t val)
{
    uint8_t wb[3];
    struct i2c_msg msg;
    struct i2c_rdwr_ioctl_data rdwr_data;

    // Write: 16-bit register address + 8-bit data
    wb[0] = (reg >> 8) & 0xFF;
    wb[1] = reg & 0xFF;
    wb[2] = val;

    msg.addr  = slave_addr;
    msg.flags = 0;   // write
    msg.len   = 3;
    msg.buf   = wb;

    rdwr_data.msgs  = &msg;
    rdwr_data.nmsgs = 1;

    int ret = ioctl(fd, IPCAM_I2C_RDWR, &rdwr_data);
    if (ret < 0)
        return -1;

    return 0;
}

// Perform the exact ipcam init sequence on a given bus/address
static int try_sensor(const char *bus, uint8_t slave_addr)
{
    int fd, ret;

    printf("\n--- Trying %s @ 0x%02x ---\n", bus, slave_addr);

    // Step 1: open() with O_RDWR | O_NONBLOCK (0x802), matching ipcam SensorDevice_Init
    printf("  [1] open(\"%s\", O_RDWR | O_NONBLOCK) ... ", bus);
    fd = open(bus, O_RDWR | O_NONBLOCK);
    if (fd < 0) {
        printf("FAILED: %s (errno=%d)\n", strerror(errno), errno);
        return -1;
    }
    printf("OK (fd=%d)\n", fd);

    // Step 2: ioctl(fd, I2C_TENBIT, 0) — disable 10-bit addressing
    printf("  [2] ioctl(fd, I2C_TENBIT=0x%x, 0) ... ", IPCAM_I2C_TENBIT);
    ret = ioctl(fd, IPCAM_I2C_TENBIT, 0);
    if (ret < 0) {
        printf("FAILED: %s (errno=%d)\n", strerror(errno), errno);
        close(fd);
        return -1;
    }
    printf("OK (ret=%d)\n", ret);

    // Step 3: ioctl(fd, I2C_SLAVE, slave_addr) — set 7-bit slave address
    printf("  [3] ioctl(fd, I2C_SLAVE=0x%x, 0x%02x) ... ", IPCAM_I2C_SLAVE, slave_addr);
    ret = ioctl(fd, IPCAM_I2C_SLAVE, slave_addr);
    if (ret < 0) {
        printf("FAILED: %s (errno=%d)\n", strerror(errno), errno);
        close(fd);
        return -1;
    }
    printf("OK (ret=%d)\n", ret);

    // Step 4: I2C_RDWR reads — detect CV2003 by reading PID registers

    // Read PID1 from 0x307a
    uint8_t pid1 = 0;
    printf("  [4] I2C_RDWR read reg 0x%04x (PID1) ... ", REG_PID1);
    ret = sensor_read_reg(fd, slave_addr, REG_PID1, &pid1);
    if (ret < 0) {
        printf("FAILED: %s (errno=%d)\n", strerror(errno), errno);
        close(fd);
        return -1;
    }
    printf("OK => 0x%02x", pid1);
    if (pid1 == CV2003_PID1_A || pid1 == CV2003_PID1_B)
        printf(" (MATCH: expected 0x%02x or 0x%02x)\n", CV2003_PID1_A, CV2003_PID1_B);
    else
        printf(" (NO MATCH: expected 0x%02x or 0x%02x)\n", CV2003_PID1_A, CV2003_PID1_B);

    // Read PID2 from 0x3138
    uint8_t pid2 = 0;
    printf("  [5] I2C_RDWR read reg 0x%04x (PID2) ... ", REG_PID2);
    ret = sensor_read_reg(fd, slave_addr, REG_PID2, &pid2);
    if (ret < 0) {
        printf("FAILED: %s (errno=%d)\n", strerror(errno), errno);
        close(fd);
        return -1;
    }
    printf("OK => 0x%02x", pid2);
    if (pid2 == CV2003_PID2)
        printf(" (MATCH: expected 0x%02x)\n", CV2003_PID2);
    else
        printf(" (NO MATCH: expected 0x%02x)\n", CV2003_PID2);

    // Check if CV2003 detected
    int detected = ((pid1 == CV2003_PID1_A || pid1 == CV2003_PID1_B) && pid2 == CV2003_PID2);

    if (!detected) {
        printf("  => CV2003 NOT detected (PID1=0x%02x, PID2=0x%02x)\n", pid1, pid2);
        close(fd);
        return -1;
    }

    printf("  => CV2003 DETECTED on %s @ 0x%02x (PID1=0x%02x, PID2=0x%02x)\n",
           bus, slave_addr, pid1, pid2);

    // Read v_cycle: (reg 0x3021 << 8) | reg 0x3020
    uint8_t vcyc_hi = 0, vcyc_lo = 0;
    printf("  [6] I2C_RDWR read reg 0x%04x (v_cycle hi) ... ", REG_VCYC_HI);
    ret = sensor_read_reg(fd, slave_addr, REG_VCYC_HI, &vcyc_hi);
    if (ret < 0) {
        printf("FAILED: %s (errno=%d)\n", strerror(errno), errno);
    } else {
        printf("OK => 0x%02x\n", vcyc_hi);
    }

    printf("  [7] I2C_RDWR read reg 0x%04x (v_cycle lo) ... ", REG_VCYC_LO);
    ret = sensor_read_reg(fd, slave_addr, REG_VCYC_LO, &vcyc_lo);
    if (ret < 0) {
        printf("FAILED: %s (errno=%d)\n", strerror(errno), errno);
    } else {
        printf("OK => 0x%02x\n", vcyc_lo);
    }

    uint16_t v_cycle = ((uint16_t)vcyc_hi << 8) | vcyc_lo;
    printf("  => v_cycle = 0x%04x (%u lines)\n", v_cycle, v_cycle);

    // Enable streaming: write 0x3141 = 0x01
    printf("  [8] I2C_RDWR write reg 0x%04x = 0x01 (enable streaming) ... ", REG_STREAM);
    ret = sensor_write_reg(fd, slave_addr, REG_STREAM, 0x01);
    if (ret < 0) {
        printf("FAILED: %s (errno=%d)\n", strerror(errno), errno);
    } else {
        printf("OK\n");
    }

    // Verify streaming register readback
    uint8_t stream_val = 0;
    printf("  [9] I2C_RDWR read reg 0x%04x (verify streaming) ... ", REG_STREAM);
    ret = sensor_read_reg(fd, slave_addr, REG_STREAM, &stream_val);
    if (ret < 0) {
        printf("FAILED: %s (errno=%d)\n", strerror(errno), errno);
    } else {
        printf("OK => 0x%02x %s\n", stream_val,
               stream_val == 0x01 ? "(streaming enabled)" : "(unexpected value)");
    }

    close(fd);
    return 0;
}

int main(void)
{
    int found = 0;

    printf("=== CV2003 I2C Sensor Detection ===\n");
    printf("Replicating exact ipcam binary I2C sequence:\n");
    printf("  open() flags: O_RDWR | O_NONBLOCK (0x%x)\n", O_RDWR | O_NONBLOCK);
    printf("  I2C_TENBIT:   0x%x\n", IPCAM_I2C_TENBIT);
    printf("  I2C_SLAVE:    0x%x\n", IPCAM_I2C_SLAVE);
    printf("  I2C_RDWR:     0x%x\n", IPCAM_I2C_RDWR);
    printf("  addr_mode=2:  16-bit register address, 8-bit data\n");
    printf("  Candidate buses: /dev/i2c-0, /dev/i2c-1, /dev/i2c-2\n");
    printf("  Candidate addrs: 0x35, 0x36\n");

    for (int b = 0; b < (int)NUM_BUSES; b++) {
        for (int a = 0; a < (int)NUM_ADDRS; a++) {
            int ret = try_sensor(i2c_buses[b], slave_addrs[a]);
            if (ret == 0) {
                found++;
            }
        }
    }

    printf("\n=== Summary ===\n");
    if (found > 0) {
        printf("CV2003 sensor found on %d bus/address combination(s)\n", found);
    } else {
        printf("CV2003 sensor NOT found on any bus/address\n");
        printf("Troubleshooting:\n");
        printf("  - Check if I2C bus exists: ls -la /dev/i2c-*\n");
        printf("  - Check if i2c-dev module loaded: lsmod | grep i2c\n");
        printf("  - Try loading: modprobe i2c-dev\n");
        printf("  - Verify sensor power is on\n");
        printf("  - Check MCLK is provided to sensor\n");
    }

    return found > 0 ? 0 : 1;
}
