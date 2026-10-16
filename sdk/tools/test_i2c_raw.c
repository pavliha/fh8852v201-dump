// Compile: arm-linux-gnueabihf-gcc -static -nostdlib -O2 -march=armv7-a -o test_i2c_raw test_i2c_raw.c
// Or with any ARM gcc that can target armv7.
//
// This uses raw Linux syscalls to avoid glibc/uclibc version issues.
// Mimics exact ipcam I2C sequence from Ghidra decompilation.

// ARM syscall numbers
#define __NR_exit    1
#define __NR_read    3
#define __NR_write   4
#define __NR_open    5
#define __NR_close   6
#define __NR_ioctl  54

// open flags
#define O_RDWR    2
#define O_NONBLOCK 2048

// I2C ioctl numbers (from ipcam binary)
#define I2C_TENBIT 0x0704
#define I2C_SLAVE  0x0706
#define I2C_RDWR   0x0707

// I2C message flags
#define I2C_M_RD   0x0001

typedef unsigned char  uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int   uint32_t;

struct i2c_msg {
    uint16_t addr;
    uint16_t flags;
    uint16_t len;
    uint8_t *buf;
};

struct i2c_rdwr_ioctl_data {
    struct i2c_msg *msgs;
    uint32_t nmsgs;
};

static long syscall1(long nr, long a) {
    register long r7 __asm__("r7") = nr;
    register long r0 __asm__("r0") = a;
    __asm__ volatile("svc #0" : "+r"(r0) : "r"(r7) : "memory");
    return r0;
}

static long syscall2(long nr, long a, long b) {
    register long r7 __asm__("r7") = nr;
    register long r0 __asm__("r0") = a;
    register long r1 __asm__("r1") = b;
    __asm__ volatile("svc #0" : "+r"(r0) : "r"(r7), "r"(r1) : "memory");
    return r0;
}

static long syscall3(long nr, long a, long b, long c) {
    register long r7 __asm__("r7") = nr;
    register long r0 __asm__("r0") = a;
    register long r1 __asm__("r1") = b;
    register long r2 __asm__("r2") = c;
    __asm__ volatile("svc #0" : "+r"(r0) : "r"(r7), "r"(r1), "r"(r2) : "memory");
    return r0;
}

static int raw_open(const char *path, int flags) {
    return (int)syscall2(__NR_open, (long)path, (long)flags);
}

static int raw_close(int fd) {
    return (int)syscall1(__NR_close, (long)fd);
}

static int raw_ioctl(int fd, unsigned long cmd, unsigned long arg) {
    return (int)syscall3(__NR_ioctl, (long)fd, (long)cmd, (long)arg);
}

static void raw_write(int fd, const char *buf, int len) {
    syscall3(__NR_write, (long)fd, (long)buf, (long)len);
}

static void raw_exit(int code) {
    syscall1(__NR_exit, (long)code);
}

// Simple string functions
static int slen(const char *s) {
    int n = 0;
    while (s[n]) n++;
    return n;
}

static void prints(const char *s) {
    raw_write(1, s, slen(s));
}

static void print_hex8(uint8_t v) {
    char buf[5] = "0x00";
    const char *hex = "0123456789abcdef";
    buf[2] = hex[(v >> 4) & 0xf];
    buf[3] = hex[v & 0xf];
    raw_write(1, buf, 4);
}

static void print_hex16(uint16_t v) {
    char buf[7] = "0x0000";
    const char *hex = "0123456789abcdef";
    buf[2] = hex[(v >> 12) & 0xf];
    buf[3] = hex[(v >> 8) & 0xf];
    buf[4] = hex[(v >> 4) & 0xf];
    buf[5] = hex[v & 0xf];
    raw_write(1, buf, 6);
}

static void print_int(int v) {
    char buf[12];
    int i = 11;
    int neg = 0;
    if (v < 0) { neg = 1; v = -v; }
    if (v == 0) { raw_write(1, "0", 1); return; }
    buf[i] = 0;
    while (v > 0 && i > 0) {
        i--;
        buf[i] = '0' + (v % 10);
        v /= 10;
    }
    if (neg && i > 0) { i--; buf[i] = '-'; }
    raw_write(1, buf + i, 11 - i);
}

// I2C read: mode=2 (16-bit addr, 8-bit data)
static int sensor_read_reg(int fd, uint8_t addr, uint16_t reg, uint8_t *val) {
    uint8_t wb[2];
    uint8_t rb[1] = { 0 };
    struct i2c_msg msgs[2];
    struct i2c_rdwr_ioctl_data rdwr;

    wb[0] = (reg >> 8) & 0xFF;
    wb[1] = reg & 0xFF;

    msgs[0].addr  = addr;
    msgs[0].flags = 0;
    msgs[0].len   = 2;
    msgs[0].buf   = wb;

    msgs[1].addr  = addr;
    msgs[1].flags = I2C_M_RD;
    msgs[1].len   = 1;
    msgs[1].buf   = rb;

    rdwr.msgs  = msgs;
    rdwr.nmsgs = 2;

    int ret = raw_ioctl(fd, I2C_RDWR, (unsigned long)&rdwr);
    if (ret < 0) return ret;

    *val = rb[0];
    return 0;
}

// I2C write: mode=2 (16-bit addr, 8-bit data)
static int sensor_write_reg(int fd, uint8_t addr, uint16_t reg, uint8_t val) {
    uint8_t wb[3];
    struct i2c_msg msg;
    struct i2c_rdwr_ioctl_data rdwr;

    wb[0] = (reg >> 8) & 0xFF;
    wb[1] = reg & 0xFF;
    wb[2] = val;

    msg.addr  = addr;
    msg.flags = 0;
    msg.len   = 3;
    msg.buf   = wb;

    rdwr.msgs  = &msg;
    rdwr.nmsgs = 1;

    return raw_ioctl(fd, I2C_RDWR, (unsigned long)&rdwr);
}

static int try_sensor(const char *bus, uint8_t addr) {
    int fd, ret;

    prints("\n--- ");
    prints(bus);
    prints(" @ ");
    print_hex8(addr);
    prints(" ---\n");

    // Step 1: open
    prints("  [1] open ... ");
    fd = raw_open(bus, O_RDWR | O_NONBLOCK);
    if (fd < 0) {
        prints("FAIL fd=");
        print_int(fd);
        prints("\n");
        return -1;
    }
    prints("OK fd=");
    print_int(fd);
    prints("\n");

    // Step 2: I2C_TENBIT
    prints("  [2] I2C_TENBIT=0 ... ");
    ret = raw_ioctl(fd, I2C_TENBIT, 0);
    if (ret < 0) {
        prints("FAIL ret=");
        print_int(ret);
        prints("\n");
        raw_close(fd);
        return -1;
    }
    prints("OK\n");

    // Step 3: I2C_SLAVE
    prints("  [3] I2C_SLAVE=");
    print_hex8(addr);
    prints(" ... ");
    ret = raw_ioctl(fd, I2C_SLAVE, addr);
    if (ret < 0) {
        prints("FAIL ret=");
        print_int(ret);
        prints("\n");
        raw_close(fd);
        return -1;
    }
    prints("OK\n");

    // Step 4: Read PID1 (0x307a)
    uint8_t pid1 = 0;
    prints("  [4] read 0x307a (PID1) ... ");
    ret = sensor_read_reg(fd, addr, 0x307a, &pid1);
    if (ret < 0) {
        prints("FAIL ret=");
        print_int(ret);
        prints("\n");
        raw_close(fd);
        return -1;
    }
    prints("OK => ");
    print_hex8(pid1);
    if (pid1 == 0x02 || pid1 == 0x20)
        prints(" MATCH\n");
    else
        prints(" no match (want 0x02/0x20)\n");

    // Step 5: Read PID2 (0x3138)
    uint8_t pid2 = 0;
    prints("  [5] read 0x3138 (PID2) ... ");
    ret = sensor_read_reg(fd, addr, 0x3138, &pid2);
    if (ret < 0) {
        prints("FAIL ret=");
        print_int(ret);
        prints("\n");
        raw_close(fd);
        return -1;
    }
    prints("OK => ");
    print_hex8(pid2);
    if (pid2 == 0x03)
        prints(" MATCH\n");
    else
        prints(" no match (want 0x03)\n");

    int detected = ((pid1 == 0x02 || pid1 == 0x20) && pid2 == 0x03);

    if (detected) {
        prints("  >>> CV2003 DETECTED! <<<\n");

        // Read v_cycle
        uint8_t vhi = 0, vlo = 0;
        prints("  [6] read 0x3021 (v_cycle hi) ... ");
        ret = sensor_read_reg(fd, addr, 0x3021, &vhi);
        if (ret < 0) { prints("FAIL\n"); }
        else { prints("OK => "); print_hex8(vhi); prints("\n"); }

        prints("  [7] read 0x3020 (v_cycle lo) ... ");
        ret = sensor_read_reg(fd, addr, 0x3020, &vlo);
        if (ret < 0) { prints("FAIL\n"); }
        else { prints("OK => "); print_hex8(vlo); prints("\n"); }

        uint16_t vcyc = ((uint16_t)vhi << 8) | vlo;
        prints("  v_cycle = ");
        print_hex16(vcyc);
        prints(" (");
        print_int(vcyc);
        prints(" lines)\n");
    } else {
        prints("  CV2003 not detected (PID1=");
        print_hex8(pid1);
        prints(" PID2=");
        print_hex8(pid2);
        prints(")\n");
    }

    raw_close(fd);
    return detected ? 0 : -1;
}

void _start(void) {
    static const char *buses[] = { "/dev/i2c-0", "/dev/i2c-1", "/dev/i2c-2" };
    static const uint8_t addrs[] = { 0x35, 0x36 };
    int found = 0;

    prints("=== CV2003 I2C Sensor Test (raw syscalls) ===\n");
    prints("Replicating ipcam binary I2C sequence\n");

    for (int b = 0; b < 3; b++) {
        for (int a = 0; a < 2; a++) {
            if (try_sensor(buses[b], addrs[a]) == 0)
                found++;
        }
    }

    prints("\n=== Summary: found ");
    print_int(found);
    prints(" sensor(s) ===\n");

    if (!found) {
        prints("Troubleshooting:\n");
        prints("  - ls -la /dev/i2c-*\n");
        prints("  - Is ipcam holding I2C? Try: killall ipcam\n");
        prints("  - Check sensor MCLK and GPIO13 reset\n");
    }

    raw_exit(found > 0 ? 0 : 1);
}
