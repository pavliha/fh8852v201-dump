/**
 * Fullhan FH8852V201 — Minimal Media Processing Interface
 * Clean-room implementation based on reverse-engineered ioctl codes.
 *
 * This library talks directly to the kernel modules via ioctl(),
 * bypassing the proprietary userspace SDK. All ioctl codes and
 * parameter layouts were determined from Ghidra decompilation.
 *
 * Build: arm-linux-gnueabihf-gcc -shared -fPIC -o libfh_mpi.so fh_mpi.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <stdint.h>

/* ================================================================
 * Device file descriptors
 * ================================================================ */

static int g_media_fd = -1;    /* /dev/media_process */
static int g_audio_fd = -1;    /* /dev/fh_audio (or similar) */
int g_i2c_fd   = -1;    /* /dev/i2c-0 — NOT static, exposed for testing */
static int g_wdt_fd   = -1;    /* /dev/watchdog */

/* ================================================================
 * IOCTL codes (from Ghidra decompilation)
 * ================================================================ */

/* VPSS ioctls — device: /dev/media_process, type 'i' (0x69) */
#define VPSS_SET_VI_ATTR        0xC0F4696A
#define VPSS_ENABLE             0xC0046971
#define VPSS_SET_CHN_ATTR       0xC00C696C
#define VPSS_OPEN_CHN           0xC0046973
#define VPSS_SET_VO_MODE        0xC00869A4
#define VPSS_SET_FRAMECTRL      0xC0086978
#define VPSS_GET_CHN_FRAME      0xC0686970

/* VENC ioctls — device: /dev/media_process, type 'M' or 'T' */
#define VENC_GET_STREAM         0xC1A04D05
#define VENC_CREATE_CHN         0xC01C5403
#define VENC_SET_RC_ATTR        0xC06C540C

/* Audio ioctls */
#define AC_RESET                0x40000000
#define AC_COMMAND              0x20000000

/* I2C ioctls */
#define I2C_SET_ADDR_MODE       0x0704
#define I2C_SET_SLAVE           0x0706
#define I2C_RDWR                0x0707

/* Watchdog ioctls */
#define WDIOC_SETTIMEOUT        0xC0045706
#define WDIOC_SETOPTIONS        0x80045704
#define WDIOC_KEEPALIVE         0x80045705

/* ================================================================
 * System Init
 * ================================================================ */

int fh_sys_init(void)
{
    if (g_media_fd >= 0)
        return 0; /* already open */

    g_media_fd = open("/dev/media_process", O_RDWR);
    if (g_media_fd < 0) {
        perror("fh_sys_init: open /dev/media_process");
        return -1;
    }
    return 0;
}

void fh_sys_exit(void)
{
    if (g_media_fd >= 0) {
        close(g_media_fd);
        g_media_fd = -1;
    }
}

/* ================================================================
 * VPSS — Video Processing Subsystem
 * ================================================================ */

int fh_vpss_set_vi_attr(uint32_t width, uint32_t height)
{
    uint32_t attr[2] = { width, height };
    return ioctl(g_media_fd, VPSS_SET_VI_ATTR, attr);
}

int fh_vpss_enable(uint32_t group)
{
    return ioctl(g_media_fd, VPSS_ENABLE, &group);
}

int fh_vpss_set_chn_attr(uint32_t chn, uint32_t width, uint32_t height)
{
    uint32_t param[3] = { chn, width, height };
    return ioctl(g_media_fd, VPSS_SET_CHN_ATTR, param);
}

int fh_vpss_open_chn(uint32_t chn)
{
    return ioctl(g_media_fd, VPSS_OPEN_CHN, &chn);
}

int fh_vpss_set_vo_mode(uint32_t chn, uint32_t mode)
{
    uint32_t param[2] = { chn, mode };
    return ioctl(g_media_fd, VPSS_SET_VO_MODE, param);
}

int fh_vpss_set_framectrl(uint32_t chn, uint16_t src_rate, uint16_t dst_rate)
{
    uint32_t param[2];
    param[0] = chn;
    param[1] = (uint32_t)dst_rate << 16 | src_rate;
    return ioctl(g_media_fd, VPSS_SET_FRAMECTRL, param);
}

/**
 * Get a processed video frame from VPSS channel.
 *
 * frame_out layout (12 uint32s):
 *   [0]  phys_addr_y  — Y plane physical address
 *   [1]  format       — pixel format
 *   [2]  width
 *   [3]  height
 *   [4]  stride_y     — Y plane stride
 *   [5]  stride_c     — C plane stride
 *   [6]  size         — total frame size
 *   [7]  phys_addr_c  — C plane physical address
 *   [8]  virt_addr_y  — Y plane virtual address (mmap'd)
 *   [9]  virt_addr_c  — C plane virtual address
 *   [10] pts_lo       — timestamp low
 *   [11] pts_hi       — timestamp high
 */
int fh_vpss_get_frame(uint32_t chn, uint32_t *frame_out, uint32_t timeout_ms)
{
    /* ioctl param: [chn, ...padding..., timeout, ...] = 0x68 bytes total */
    uint32_t param[26];
    int ret;

    memset(param, 0, sizeof(param));
    param[0] = chn;
    param[24] = timeout_ms; /* approximate offset for timeout */

    ret = ioctl(g_media_fd, VPSS_GET_CHN_FRAME, param);
    if (ret == 0 && frame_out) {
        /* Map from ioctl output to frame struct
         * Based on FH_VPSS_GetChnFrame_Ex decompilation */
        frame_out[0]  = param[2];   /* phys_addr_y */
        frame_out[1]  = param[3];   /* format */
        frame_out[2]  = param[1];   /* width (from param[3] in raw) */
        frame_out[3]  = param[4];   /* height */
        frame_out[4]  = param[5];   /* stride_y */
        frame_out[5]  = param[6];   /* stride_c */
        frame_out[6]  = param[7];   /* size */
        frame_out[7]  = param[8];   /* phys_addr_c */
        frame_out[8]  = param[9];   /* virt_addr_y */
        frame_out[9]  = param[10];  /* virt_addr_c */
        frame_out[10] = param[16];  /* pts_lo */
        frame_out[11] = param[17];  /* pts_hi */
    }
    return ret;
}

/* ================================================================
 * VENC — Video Encoder
 * ================================================================ */

/**
 * Get an encoded stream packet.
 *
 * stream_out layout matches FH_VENC_STREAM from fh_venc.h:
 *   [0] type (4=H.264, 8=H.265, 1=JPEG, 2=MJPEG)
 *   [1] chn
 *   [2] total_size
 *   [3] frame_type
 *   [4] pts_lo
 *   ...
 */
int fh_venc_get_stream(uint32_t chn, uint32_t *stream_out)
{
    /* ioctl uses a 105-uint32 buffer (420 bytes) */
    uint32_t buf[105];
    int ret;

    memset(buf, 0, sizeof(buf));
    buf[0] = chn;

    ret = ioctl(g_media_fd, VENC_GET_STREAM, buf);
    if (ret == 0 && stream_out) {
        memcpy(stream_out, buf, sizeof(buf));
    }
    return ret;
}

int fh_venc_create_chn(uint32_t chn, uint32_t width, uint32_t height,
                       uint32_t codec_type)
{
    uint32_t param[7];
    param[0] = chn;
    param[1] = width;
    param[2] = height;
    param[3] = codec_type; /* 0=H.264, 1=H.265 */
    param[4] = 0;
    param[5] = 0;
    param[6] = 0;
    return ioctl(g_media_fd, VENC_CREATE_CHN, param);
}

/* ================================================================
 * Sensor I2C
 * ================================================================ */

int fh_sensor_init(uint8_t slave_addr_7bit, int addr_mode)
{
    g_i2c_fd = open("/dev/i2c-0", O_RDWR | O_NONBLOCK);
    if (g_i2c_fd < 0) {
        perror("fh_sensor_init: open /dev/i2c-0");
        return -1;
    }

    if (ioctl(g_i2c_fd, I2C_SET_ADDR_MODE, 0) < 0) {
        perror("fh_sensor_init: set addr mode");
        return -1;
    }

    if (ioctl(g_i2c_fd, I2C_SET_SLAVE, (int)slave_addr_7bit) < 0) {
        perror("fh_sensor_init: set slave address");
        return -1;
    }

    return 0;
}

/**
 * Write a sensor register via I2C.
 * @param reg  16-bit register address
 * @param val  8-bit register value
 *
 * Protocol: send [reg_hi, reg_lo, val] as 3-byte write.
 * ioctl 0x707 with I2C message struct.
 */
/**
 * Fullhan I2C message struct (from Ghidra decompilation of I2CSensor_Read/WriteEx).
 *
 * The Fullhan I2C driver uses a single message struct with separate
 * addr_len and data_len fields, NOT the standard Linux i2c_msg array.
 *
 * ioctl(fd, 0x707, &xfer) where xfer = { &msg, nmsgs }
 */
struct fh_i2c_msg {
    uint16_t slave_addr;    /* [0x00] I2C slave address (raw, e.g. 0x35) */
    uint16_t _pad1;         /* [0x02] */
    uint16_t addr_len;      /* [0x04] Register address bytes (1 or 2) */
    uint16_t _pad2;         /* [0x06] */
    void    *buf;           /* [0x08] Buffer: [addr bytes...][data bytes...] */
    uint32_t _pad3;         /* [0x0C] */
    uint16_t data_len;      /* [0x10] Data bytes to read/write (1 or 2) */
    uint16_t _pad4;         /* [0x12] */
};

struct fh_i2c_xfer {
    struct fh_i2c_msg *msgs;
    uint32_t nmsgs;
};

int fh_sensor_write(uint16_t reg, uint8_t val)
{
    /* Mode 2: 16-bit addr, 8-bit data → buf = [reg_hi, reg_lo, val] */
    uint8_t buf[4] = { (reg >> 8) & 0xFF, reg & 0xFF, val, 0 };
    struct fh_i2c_msg msg = {
        .slave_addr = 0x35,     /* CV2003 raw addr */
        .addr_len   = 2,        /* 16-bit register address */
        .buf        = buf,
        .data_len   = 3,        /* total bytes: addr(2) + data(1) */
    };
    struct fh_i2c_xfer xfer = { .msgs = &msg, .nmsgs = 1 };

    return ioctl(g_i2c_fd, I2C_RDWR, &xfer);
}

int fh_sensor_read(uint16_t reg, uint8_t *val)
{
    /* Mode 2: 16-bit addr, 8-bit data
     * buf = [reg_hi, reg_lo, read_result]
     * addr_len=2, data_len=1 */
    uint8_t buf[4] = { (reg >> 8) & 0xFF, reg & 0xFF, 0, 0 };
    struct fh_i2c_msg msg = {
        .slave_addr = 0x35,
        .addr_len   = 2,
        .buf        = buf,
        .data_len   = 1,
    };
    struct fh_i2c_xfer xfer = { .msgs = &msg, .nmsgs = 2 };

    int ret = ioctl(g_i2c_fd, I2C_RDWR, &xfer);
    if (ret >= 0 && val)
        *val = buf[2];  /* read data follows addr bytes */
    return ret;
}

void fh_sensor_close(void)
{
    if (g_i2c_fd >= 0) {
        close(g_i2c_fd);
        g_i2c_fd = -1;
    }
}

/* ================================================================
 * Watchdog
 * ================================================================ */

int fh_wdt_open(void)
{
    uint32_t timeout = 30;
    uint32_t options = 2;

    g_wdt_fd = open("/dev/watchdog", O_RDWR);
    if (g_wdt_fd < 0)
        return -1;

    ioctl(g_wdt_fd, WDIOC_SETTIMEOUT, &timeout);
    ioctl(g_wdt_fd, WDIOC_SETOPTIONS, &options);
    return 0;
}

int fh_wdt_feed(void)
{
    if (g_wdt_fd < 0)
        return -1;
    return ioctl(g_wdt_fd, WDIOC_KEEPALIVE, 0);
}

void fh_wdt_close(void)
{
    if (g_wdt_fd >= 0) {
        close(g_wdt_fd);
        g_wdt_fd = -1;
    }
}

/* ================================================================
 * GPIO helpers
 * ================================================================ */

int fh_gpio_export(int gpio)
{
    char buf[16];
    int fd = open("/sys/class/gpio/export", O_WRONLY);
    if (fd < 0) return -1;
    int len = snprintf(buf, sizeof(buf), "%d", gpio);
    write(fd, buf, len);
    close(fd);
    return 0;
}

int fh_gpio_set_direction(int gpio, int output)
{
    char path[64];
    snprintf(path, sizeof(path), "/sys/class/gpio/GPIO%d/direction", gpio);
    int fd = open(path, O_WRONLY);
    if (fd < 0) return -1;
    write(fd, output ? "out" : "in", output ? 3 : 2);
    close(fd);
    return 0;
}

int fh_gpio_write(int gpio, int value)
{
    char path[64];
    snprintf(path, sizeof(path), "/sys/class/gpio/GPIO%d/value", gpio);
    int fd = open(path, O_WRONLY);
    if (fd < 0) return -1;
    write(fd, value ? "1" : "0", 1);
    close(fd);
    return 0;
}

int fh_gpio_read(int gpio)
{
    char path[64], val;
    snprintf(path, sizeof(path), "/sys/class/gpio/GPIO%d/value", gpio);
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;
    read(fd, &val, 1);
    close(fd);
    return val - '0';
}

/**
 * Reset sensor via GPIO13 (pulse low then high).
 */
int fh_sensor_reset(void)
{
    fh_gpio_export(13);
    fh_gpio_set_direction(13, 1);
    fh_gpio_write(13, 1);
    usleep(100000);
    fh_gpio_write(13, 0);
    usleep(100000);
    fh_gpio_write(13, 1);
    return 0;
}
