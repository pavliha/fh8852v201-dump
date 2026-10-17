#include <assert.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "../lib/cv2003_driver.h"

static const char VENDOR_SENSOR_CFG_1080P25_HEX[] =
    "003303002234bf000134000040340100423400000638000008395f00093900002939010058310100"
    "593101005a3101005b310100b3351500483164003130000018310100193106007036000079360200"
    "303300000e32020004381000a1350600a8350600a9350600aa350600ab350600ac350600ad350600"
    "ae350700af3507003b3301003833080039330000443120003030010020308c0021300a002430d000"
    "2530020038300600393000003a3080003b30070034300400353000003630380037300400083951000a39020000300000";

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

static char g_log[128][48];
static int g_log_len;

static void log_op(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g_log[g_log_len++], sizeof(g_log[0]), fmt, ap);
    va_end(ap);
}

static uint8_t fake_sensor_reg(uint16_t reg)
{
    switch (reg) {
    case 0x307a: return 0x02;
    case 0x3138: return 0x03;
    case 0x3021: return 0x0a;
    case 0x3020: return 0x8c;
    default: return 0x00;
    }
}

int open(const char *path, int flags, ...)
{
    log_op("open %s %#x", path, flags);
    return 3;
}

int close(int fd)
{
    (void)fd;
    return 0;
}

int usleep(useconds_t us)
{
    log_op("usleep %u", (unsigned)us);
    return 0;
}

int ioctl(int fd, unsigned long req, ...)
{
    (void)fd;
    va_list ap;
    va_start(ap, req);
    void *arg = va_arg(ap, void *);
    va_end(ap);

    if (req == 0x0707) {
        struct i2c_rdwr_ioctl_data *x = arg;
        uint8_t *w = x->msgs[0].buf;
        uint16_t reg = (uint16_t)(w[0] << 8 | w[1]);
        if (x->nmsgs == 1) {
            log_op("w %02x %04x %02x", x->msgs[0].addr, reg, w[2]);
        } else {
            x->msgs[1].buf[0] = fake_sensor_reg(reg);
            log_op("r %02x %04x", x->msgs[0].addr, reg);
        }
        return 0;
    }
    if (req == 0x0704 || req == 0x0706) {
        log_op("ioctl %#lx %#lx", req, (unsigned long)arg);
        return 0;
    }
    const uint8_t *b = arg;
    size_t n = (req >> 16) & 0x3fff;
    char hex[32] = "";
    for (size_t i = 0; i < n; i++)
        sprintf(hex + i * 2, "%02x", b[i]);
    log_op("ioctl %#lx %s", req, hex);
    return 0;
}

int main(void)
{
    char expected[128][48];
    int n = 0;
    const char *const head[] = {
        "open /dev/i2c-0 0x802", "ioctl 0x704 0", "ioctl 0x706 0x35",
        "r 35 307a", "r 35 3138",
        "open /dev/i2c-0 0x802", "ioctl 0x704 0", "ioctl 0x706 0x35",
        "open /dev/i2c-0 0x802", "ioctl 0x704 0", "ioctl 0x706 0x35",
        "open /dev/isp 0", "ioctl 0x40066955 000000010000", "ioctl 0x40036954 000901",
    };
    for (size_t i = 0; i < sizeof(head) / sizeof(head[0]); i++)
        strcpy(expected[n++], head[i]);
    for (size_t i = 0; i < strlen(VENDOR_SENSOR_CFG_1080P25_HEX); i += 8) {
        unsigned b0, b1, b2;
        sscanf(VENDOR_SENSOR_CFG_1080P25_HEX + i, "%2x%2x%2x", &b0, &b1, &b2);
        sprintf(expected[n++], "w 35 %04x %02x", b1 << 8 | b0, b2);
    }
    const char *const tail[] = {
        "usleep 10000", "w 35 3141 01", "r 35 3021", "r 35 3020",
        "w 35 304a 00", "w 35 3049 00", "w 35 3048 0a",
        "w 35 3804 10", "w 35 3154 00", "w 35 314d 00", "w 35 314c 40",
    };
    for (size_t i = 0; i < sizeof(tail) / sizeof(tail[0]); i++)
        strcpy(expected[n++], tail[i]);

    assert(cv2003_init() == 0);
    assert(cv2003_set_format() == 0);
    cv2003_deinit();

    for (int i = 0; i < n || i < g_log_len; i++) {
        if (i >= n || i >= g_log_len || strcmp(expected[i], g_log[i]) != 0) {
            fprintf(stderr, "op %d: expected '%s', got '%s'\n", i,
                    i < n ? expected[i] : "(end)", i < g_log_len ? g_log[i] : "(end)");
            return 1;
        }
    }
    printf("cv2003 bus sequence matches vendor: %d ops\n", g_log_len);
    return 0;
}
