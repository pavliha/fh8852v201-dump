/**
 * CV2003 (GC4201AC) MIPI Sensor Driver for OpenIPC/Majestic
 *
 * Built to match the exact format of libgc4653_mipi.so from
 * fullhan-osdrv-fh8852v200.
 *
 * Sensor: CV2003 / GC4201AC (Galaxycore rebranded)
 * Interface: MIPI CSI, 1 lane
 * Resolution: 1920x1080
 * I2C: slave 0x35 (7-bit) on /dev/i2c-0, 16-bit addr, 8-bit data
 * Verified live on FH8852V201 camera (2026-04-07)
 *
 * Compile (with OpenIPC/Buildroot toolchain):
 *   arm-fullhanv3-linux-uclibcgnueabi-gcc -shared -fPIC -O2 \
 *     -o libcv2003_mipi.so libcv2003_mipi.c
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>

/* ================================================================
 * I2C definitions (matching ipcam binary constants)
 * ================================================================ */

#define I2C_TENBIT  0x0704
#define I2C_SLAVE   0x0706
#define I2C_RDWR    0x0707
#define I2C_M_RD    0x0001

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

/* ================================================================
 * Sensor configuration
 * ================================================================ */

#define CV2003_DEFAULT_ADDR   0x35
#define CV2003_ALT_ADDR       0x36
#define CV2003_I2C_DEV        "/dev/i2c-0"

/* Chip ID registers */
#define REG_PID1    0x307a  /* expect 0x02 or 0x20 */
#define REG_PID2    0x3138  /* expect 0x03 */

/* Key registers */
#define REG_STANDBY     0x3000
#define REG_VCYC_LO     0x3020
#define REG_VCYC_HI     0x3021
#define REG_HCYC_LO     0x3024
#define REG_HCYC_HI     0x3025
#define REG_FLIP_MIRROR  0x3028
#define REG_PLL_DIV      0x3031
#define REG_STREAM_EN    0x3141
#define REG_EXP_LO       0x3048
#define REG_EXP_MID      0x3049
#define REG_EXP_HI       0x304a
#define REG_GAIN_IDX     0x3154
#define REG_GAIN_LO      0x314c
#define REG_GAIN_HI      0x314d
#define REG_GAIN_MODE     0x3804

/* ================================================================
 * Global state
 * ================================================================ */

static int      g_fd = -1;          /* I2C file descriptor */
static uint16_t g_slave_addr = 0;   /* active slave address */
static uint8_t  g_addr_mode = 2;    /* 16-bit reg, 8-bit data */
static char    *g_dev_path = NULL;  /* device path string */

static uint32_t g_v_cycle = 2700;
static uint32_t g_exposure = 0;
static uint32_t g_gain = 0x40;
static uint32_t g_format = 0;

/* Sensor version string */
static char sensor_version[32] = "cv2003_mipi V1.0.0";

/* ================================================================
 * I2C low-level (matches ipcam SensorDevice_Init / I2CSensor_*)
 * ================================================================ */

void SensorDevice_Close(void)
{
    if (g_fd >= 0) {
        close(g_fd);
        g_fd = -1;
    }
}

int SensorDevice_Init(uint16_t slave_addr, uint8_t addr_mode)
{
    if (g_fd >= 0) {
        perror("Error: Sensor device opened !");
        return -1;
    }

    g_addr_mode = addr_mode;
    g_slave_addr = slave_addr;

    if (addr_mode < 4) {
        g_dev_path = CV2003_I2C_DEV;
    } else {
        g_dev_path = "/dev/spi1";
    }

    g_fd = open(g_dev_path, O_RDWR | O_NONBLOCK);
    if (g_fd < 0) {
        perror("ERROR: Unable to open sensor!");
        return -1;
    }

    if (ioctl(g_fd, I2C_TENBIT, 0) < 0) {
        perror("Error: Unable to set address mode!");
        return -1;
    }

    if (ioctl(g_fd, I2C_SLAVE, (unsigned long)slave_addr) < 0) {
        perror("Error: Unable to set slave address!");
        return -1;
    }

    return 0;
}

/* I2C write: 16-bit register address, 8-bit data (mode=2) */
void I2CSensor_WriteEx(uint16_t reg, uint16_t val, int suppress_err)
{
    uint8_t buf[3];
    struct i2c_msg msg;
    struct i2c_rdwr_ioctl_data rdwr;

    buf[0] = (reg >> 8) & 0xFF;
    buf[1] = reg & 0xFF;
    buf[2] = val & 0xFF;

    msg.addr  = g_slave_addr;
    msg.flags = 0;
    msg.len   = 3;
    msg.buf   = buf;

    rdwr.msgs  = &msg;
    rdwr.nmsgs = 1;

    int ret = ioctl(g_fd, I2C_RDWR, &rdwr);
    if (ret < 0 && !suppress_err) {
        perror("ERROR: Unable to write sensor register!");
        printf("addr: 0x%x, data: 0x%x\n", reg, val);
    }
}

void I2CSensor_Write(uint16_t reg, uint16_t val)
{
    I2CSensor_WriteEx(reg, val, 0);
}

void I2CSensor_WriteEx_Multi(uint16_t *regs, int count, int suppress_err)
{
    for (int i = 0; i < count; i++) {
        I2CSensor_WriteEx(regs[i * 2], regs[i * 2 + 1], suppress_err);
    }
}

void I2CSensor_Write_Multi(uint16_t *regs, int count)
{
    I2CSensor_WriteEx_Multi(regs, count, 0);
}

/* I2C read: 16-bit register address, 8-bit data (mode=2) */
uint16_t I2CSensor_Read(uint16_t reg)
{
    uint8_t wb[2], rb[1] = {0};
    struct i2c_msg msgs[2];
    struct i2c_rdwr_ioctl_data rdwr;

    wb[0] = (reg >> 8) & 0xFF;
    wb[1] = reg & 0xFF;

    msgs[0].addr  = g_slave_addr;
    msgs[0].flags = 0;
    msgs[0].len   = 2;
    msgs[0].buf   = wb;

    msgs[1].addr  = g_slave_addr;
    msgs[1].flags = I2C_M_RD;
    msgs[1].len   = 1;
    msgs[1].buf   = rb;

    rdwr.msgs  = msgs;
    rdwr.nmsgs = 2;

    if (ioctl(g_fd, I2C_RDWR, &rdwr) < 0) {
        perror("ERROR: Unable to read sensor register!");
        return 0xFFFF;
    }

    return rb[0];
}

/* High-level wrappers (I2C only, no SPI) */
uint16_t Sensor_Read(uint16_t reg)
{
    return I2CSensor_Read(reg);
}

void Sensor_Write(uint16_t reg, uint16_t val)
{
    I2CSensor_Write(reg, val);
}

void Sensor_Write_Multi(uint16_t *regs, int count)
{
    I2CSensor_Write_Multi(regs, count);
}

void Sensor_WriteEx(uint16_t reg, uint16_t val, int suppress)
{
    I2CSensor_WriteEx(reg, val, suppress);
}

/* Not used for CV2003 (MIPI only) but exported for API compatibility */
void SPISensor_Write(uint16_t reg, uint16_t val) { }
uint16_t SPISensor_Read(uint16_t reg) { return 0; }

/* ================================================================
 * Register init tables (extracted from ipcam via Ghidra)
 * ================================================================ */

/* 1080P25 — verified working on live camera */
static const uint16_t regs_1080p25[][2] = {
    {0x3300,0x03},{0x3422,0xbf},{0x3401,0x00},{0x3440,0x01},
    {0x3442,0x00},{0x3806,0x00},{0x3908,0x5f},{0x3909,0x00},
    {0x3929,0x01},{0x3158,0x01},{0x3159,0x01},{0x315a,0x01},
    {0x315b,0x01},{0x35b3,0x15},{0x3148,0x64},{0x3031,0x00},
    {0x3118,0x01},{0x3119,0x06},{0x3670,0x00},{0x3679,0x02},
    {0x3330,0x00},{0x320e,0x02},{0x3804,0x10},{0x35a1,0x06},
    {0x35a8,0x06},{0x35a9,0x06},{0x35aa,0x06},{0x35ab,0x06},
    {0x35ac,0x06},{0x35ad,0x06},{0x35ae,0x07},{0x35af,0x07},
    {0x333b,0x01},{0x3338,0x08},{0x3339,0x00},{0x3144,0x20},
    /* PLL/timing for 25fps */
    {0x3030,0x01},{0x3020,0x8c},{0x3021,0x0a},{0x3024,0xd0},
    {0x3025,0x02},{0x3038,0x06},{0x3039,0x00},{0x303a,0x80},
    {0x303b,0x07},{0x3034,0x04},{0x3035,0x00},{0x3036,0x38},
    {0x3037,0x04},{0x3908,0x51},{0x390a,0x02},{0x3000,0x00},
};

/* 1080P30 — timing regs differ */
static const uint16_t regs_1080p30[][2] = {
    {0x3300,0x03},{0x3422,0xbf},{0x3401,0x00},{0x3440,0x01},
    {0x3442,0x00},{0x3806,0x00},{0x3908,0x5f},{0x3909,0x00},
    {0x3929,0x01},{0x3158,0x01},{0x3159,0x01},{0x315a,0x01},
    {0x315b,0x01},{0x35b3,0x15},{0x3148,0x64},{0x3031,0x00},
    {0x3118,0x01},{0x3119,0x06},{0x3670,0x00},{0x3679,0x02},
    {0x3330,0x00},{0x320e,0x02},{0x3804,0x10},{0x35a1,0x06},
    {0x35a8,0x06},{0x35a9,0x06},{0x35aa,0x06},{0x35ab,0x06},
    {0x35ac,0x06},{0x35ad,0x06},{0x35ae,0x07},{0x35af,0x07},
    {0x333b,0x01},{0x3338,0x08},{0x3339,0x00},{0x3144,0x20},
    /* PLL/timing for 30fps */
    {0x3030,0x01},{0x3020,0xca},{0x3021,0x08},{0x3024,0xd0},
    {0x3025,0x02},{0x3038,0x06},{0x3039,0x00},{0x303a,0x80},
    {0x303b,0x07},{0x3034,0x04},{0x3035,0x00},{0x3036,0x38},
    {0x3037,0x04},{0x3908,0x51},{0x390a,0x02},{0x3000,0x00},
};

/* Sensor config table entries (matches Sensor_Cfg_* format from GC4653) */
typedef struct {
    uint32_t format_id;
    uint32_t reg_count;
    const uint16_t (*regs)[2];
    uint16_t width;
    uint16_t height;
    uint16_t fps;
    uint16_t v_cycle;
    uint16_t h_cycle;
    uint8_t  mipi_lanes;
    uint8_t  bayer_format;  /* 0x203 for CV2003 */
} sensor_cfg_t;

const sensor_cfg_t Sensor_Cfg_CV2003_MIPI_1080P25 = {
    .format_id = 0x203,
    .reg_count = sizeof(regs_1080p25) / sizeof(regs_1080p25[0]),
    .regs = regs_1080p25,
    .width = 1920, .height = 1080,
    .fps = 25, .v_cycle = 2700, .h_cycle = 720,
    .mipi_lanes = 1, .bayer_format = 0x03,
};

const sensor_cfg_t Sensor_Cfg_CV2003_MIPI_1080P30 = {
    .format_id = 0x204,
    .reg_count = sizeof(regs_1080p30) / sizeof(regs_1080p30[0]),
    .regs = regs_1080p30,
    .width = 1920, .height = 1080,
    .fps = 30, .v_cycle = 2250, .h_cycle = 720,
    .mipi_lanes = 1, .bayer_format = 0x03,
};

/* ================================================================
 * Sensor vtable callbacks (31 entries, 124 bytes)
 * Matches gc4653_mipi_sensor_if layout exactly
 * ================================================================ */

/* Gain lookup table (256 entries, 1x=64 to 16x=1024) */
static const uint16_t gain_lut[] = {
     64, 68, 72, 76, 80, 84, 88, 92, 96,100,104,108,112,116,120,124,
    128,132,136,140,144,148,152,156,160,168,176,184,192,200,208,216,
    224,232,240,248,256,272,288,304,320,336,352,368,384,400,416,432,
    448,464,480,496,512,544,576,608,640,672,704,736,768,800,832,864,
    896,928,960,992,1024,
};
#define GAIN_LUT_SIZE (sizeof(gain_lut)/sizeof(gain_lut[0]))

/* AWB gain storage */
uint32_t AwbGain[4] = {0};
uint32_t contrast = 0;
uint32_t saturation = 0;
uint32_t sharpness = 0;
uint32_t ImageParam[4] = {0};
uint32_t MirrorFlipBayerFormat[4] = {
    0x03, /* normal */
    0x03, /* mirror */
    0x03, /* flip */
    0x03, /* mirror+flip */
};

/* LTM mapping tables (required by Majestic, 128 entries each) */
uint32_t LtmMapping00[128] = {0};
uint32_t LtmMapping01[128] = {0};
uint32_t LtmMapping02[128] = {0};
uint32_t LtmMapping03[128] = {0};
uint32_t LtmMapping04[128] = {0};
uint32_t LtmMapping05[128] = {0};
uint32_t LtmMapping06[128] = {0};
uint32_t LtmMapping07[128] = {0};
uint32_t LtmMapping08[128] = {0};
uint32_t LtmMapping09[128] = {0};
uint32_t LtmMapping10[128] = {0};

/* [0] name — returned as first field of sensor_if */
static const char cv2003_name[] = "cv2003_mipi";

/* [1] GetSensorViAttr */
static int GetSensorViAttr(void *attr)
{
    if (!attr) return -1;
    uint32_t *p = (uint32_t *)attr;
    p[0] = 1920;       /* width */
    p[1] = 1080;       /* height */
    p[2] = 1;          /* MIPI lanes */
    p[3] = 0x203;      /* bayer format */
    return 0;
}

/* [2] SetSensorFlipMirror */
static int SetSensorFlipMirror(uint32_t mode)
{
    uint8_t val = Sensor_Read(REG_FLIP_MIRROR) & 0xFC;
    val |= ((mode & 1) << 1) | ((mode >> 1) & 1);
    Sensor_Write(REG_FLIP_MIRROR, val);
    return 0;
}

/* [3] GetSensorFlipMirror */
static int GetSensorFlipMirror(uint32_t *mode)
{
    if (!mode) return -1;
    uint8_t val = Sensor_Read(REG_FLIP_MIRROR);
    *mode = ((val & 1) << 1) | ((val >> 1) & 1);
    return 0;
}

/* [4] SetSensorIris — not applicable for CV2003 */
static int SetSensorIris(uint32_t val) { return 0; }

/* [5] Sensor_Init — called during ISP sensor init */
static int Sensor_Init(int format)
{
    const sensor_cfg_t *cfg = &Sensor_Cfg_CV2003_MIPI_1080P25;

    if (format == 0x204) {
        cfg = &Sensor_Cfg_CV2003_MIPI_1080P30;
    }

    /* Write register init table */
    for (uint32_t i = 0; i < cfg->reg_count; i++) {
        Sensor_Write(cfg->regs[i][0], cfg->regs[i][1]);
    }
    usleep(10000); /* 10ms settle */

    g_v_cycle = cfg->v_cycle;
    g_format = format;

    return 0;
}

/* [6] SensorReset — GPIO13 reset pulse */
static int SensorReset(void)
{
    /* GPIO reset handled by load_fullhan script, not by sensor driver */
    return 0;
}

/* [7] Sensor_DeInit */
static int Sensor_DeInit(void)
{
    Sensor_Write(REG_STANDBY, 0x01);
    SensorDevice_Close();
    return 0;
}

/* [8] SetSensorFmt — main init, write regs + start streaming */
static int SetSensorFmt(int format)
{
    SensorDevice_Close();
    SensorDevice_Init(CV2003_DEFAULT_ADDR, 2);

    Sensor_Init(format);

    /* Enable streaming */
    Sensor_Write(REG_STREAM_EN, 0x01);

    /* Read back v_cycle */
    uint8_t vl = Sensor_Read(REG_VCYC_LO);
    uint8_t vh = Sensor_Read(REG_VCYC_HI);
    g_v_cycle = ((uint32_t)vh << 8) | vl;

    printf("cv2003: format=0x%x v_cycle=%u\n", format, g_v_cycle);
    return 0;
}

/* [9] Sensor_Kick — start streaming */
static int Sensor_Kick(void)
{
    Sensor_Write(REG_STREAM_EN, 0x01);
    return 0;
}

/* [10] SetSensorReg — write arbitrary register */
static int SetSensorReg(uint32_t reg, uint32_t val)
{
    Sensor_Write((uint16_t)reg, (uint16_t)val);
    return 0;
}

/* [11] SetExposureRatio — not used for single exposure */
static int SetExposureRatio(uint32_t ratio) { return 0; }

/* [12] GetExposureRatio */
static int GetExposureRatio(uint32_t *ratio)
{
    if (ratio) *ratio = 64;
    return 0;
}

/* [13] GetSensorAttribute */
static int GetSensorAttribute(void *attr)
{
    if (!attr) return -1;
    uint32_t *p = (uint32_t *)attr;
    p[0] = 1920;
    p[1] = 1080;
    p[2] = g_v_cycle;
    p[3] = 720;  /* h_cycle */
    return 0;
}

/* [14] SetLaneNumMax — MIPI lane config */
static int SetLaneNumMax(uint32_t lanes) { return 0; }

/* [15] GetSensorReg */
static int GetSensorReg(uint32_t reg, uint32_t *val)
{
    if (!val) return -1;
    *val = Sensor_Read((uint16_t)reg);
    return 0;
}

/* [16] GetSensorAwbGain */
static int GetSensorAwbGain(uint32_t *gain)
{
    if (gain) memcpy(gain, AwbGain, sizeof(AwbGain));
    return 0;
}

/* [17] SetSensorAwbGain */
static int SetSensorAwbGain(uint32_t *gain)
{
    if (gain) memcpy(AwbGain, gain, sizeof(AwbGain));
    return 0;
}

/* [19] SensorCommonIf — returns sensor name */
static const char *SensorCommonIf(void)
{
    return cv2003_name;
}

/* [20] GetAEDefault */
static int GetAEDefault(void *ae_default)
{
    if (!ae_default) return -1;
    uint32_t *p = (uint32_t *)ae_default;
    p[0] = 0;      /* min exposure */
    p[1] = g_v_cycle - 4; /* max exposure */
    p[2] = 0x40;   /* min gain (1x) */
    p[3] = 0x400;  /* max gain (16x) */
    return 0;
}

/* [21] GetAEInfo */
static int GetAEInfo(void *ae_info)
{
    if (!ae_info) return -1;
    uint32_t *p = (uint32_t *)ae_info;
    p[0] = g_exposure;
    p[1] = g_gain;
    p[2] = g_v_cycle;
    return 0;
}

/* [22] SetIntt — set integration time (exposure) */
static int SetIntt(uint32_t intt)
{
    uint32_t val = g_v_cycle - intt;
    if (val < 8) val = 8;
    if (val > g_v_cycle - 4) val = g_v_cycle - 4;

    g_exposure = intt;

    Sensor_Write(REG_EXP_HI,  (val >> 16) & 0xFF);
    Sensor_Write(REG_EXP_MID, (val >> 8) & 0xFF);
    Sensor_Write(REG_EXP_LO,  val & 0xFF);
    return 0;
}

/* [23] CalcSnsValidIntt */
static int CalcSnsValidIntt(uint32_t *intt)
{
    if (!intt) return -1;
    if (*intt > g_v_cycle - 4) *intt = g_v_cycle - 4;
    return 0;
}

/* [24] SetGain */
static int SetGain(uint32_t gain)
{
    g_gain = gain;

    /* Calculate register gain value */
    uint32_t reg_gain = gain >> 5;
    if (reg_gain < 0x40) reg_gain = 0x40;
    if (reg_gain > 0x3FF) reg_gain = 0x3FF;

    /* Set gain mode */
    if (gain < 0x401) {
        Sensor_Write(REG_GAIN_MODE, 0x10);
    } else if (gain > 0x7FF) {
        Sensor_Write(REG_GAIN_MODE, 0x15);
    }

    /* Find LUT index */
    uint32_t idx = 0;
    for (idx = 0; idx < GAIN_LUT_SIZE - 1; idx++) {
        if (gain <= gain_lut[idx]) break;
    }

    Sensor_Write(REG_GAIN_IDX, idx & 0xFF);
    Sensor_Write(REG_GAIN_HI, (reg_gain >> 8) & 0xFF);
    Sensor_Write(REG_GAIN_LO, reg_gain & 0xFF);
    return 0;
}

/* [25] CalcSnsValidGain */
static int CalcSnsValidGain(uint32_t *gain)
{
    if (!gain) return -1;
    if (*gain < 0x40) *gain = 0x40;
    if (*gain > 0x400) *gain = 0x400;
    return 0;
}

/* [26] SetSnsFrameH — set v_cycle for frame rate control */
static int SetSnsFrameH(uint32_t frame_h)
{
    if (frame_h < 1125) frame_h = 1125;
    g_v_cycle = frame_h;
    Sensor_Write(REG_VCYC_HI, (frame_h >> 8) & 0xFF);
    Sensor_Write(REG_VCYC_LO, frame_h & 0xFF);
    return 0;
}

/* [27] GetMirrorFlipBayerFormat */
static int GetMirrorFlipBayerFormat(uint32_t mode, uint32_t *fmt)
{
    if (!fmt) return -1;
    if (mode < 4) *fmt = MirrorFlipBayerFormat[mode];
    else *fmt = 0x03;
    return 0;
}

/* [28] GetUserSensorAwbGain */
static int GetUserSensorAwbGain(uint32_t *gain)
{
    if (gain) memcpy(gain, AwbGain, sizeof(AwbGain));
    return 0;
}

/* [29] GetSensorLtmCurve */
static int GetSensorLtmCurve(uint32_t **curves)
{
    if (!curves) return -1;
    curves[0] = LtmMapping00;
    curves[1] = LtmMapping01;
    curves[2] = LtmMapping02;
    curves[3] = LtmMapping03;
    curves[4] = LtmMapping04;
    curves[5] = LtmMapping05;
    curves[6] = LtmMapping06;
    curves[7] = LtmMapping07;
    curves[8] = LtmMapping08;
    curves[9] = LtmMapping09;
    curves[10] = LtmMapping10;
    return 0;
}

/* [30] Sensor_Isconnect — detect/probe the sensor */
static int Sensor_Isconnect(void)
{
    uint16_t addrs[] = { CV2003_DEFAULT_ADDR, CV2003_ALT_ADDR };

    for (int i = 0; i < 2; i++) {
        SensorDevice_Init(addrs[i], 2);

        uint16_t pid1 = Sensor_Read(REG_PID1);
        uint16_t pid2 = Sensor_Read(REG_PID2);

        printf("cv2003: addr=0x%x pid1=0x%x pid2=0x%x\n", addrs[i], pid1, pid2);

        SensorDevice_Close();

        if ((pid1 == 0x02 || pid1 == 0x20) && pid2 == 0x03) {
            g_slave_addr = addrs[i];
            return 1; /* detected */
        }
    }
    return 0; /* not found */
}

/* ================================================================
 * Sensor interface struct — 31 entries, 124 bytes
 * Matches gc4653_mipi_sensor_if layout exactly
 * ================================================================ */

uint32_t cv2003_mipi_sensor_if[31] = {0};

/* ================================================================
 * Public API — Sensor_Create / Sensor_Destroy / get_sensor_version
 * ================================================================ */

void get_sensor_version(void)
{
    printf("mipi version: V1.1.1 17.0.0(build 000)(cv2003_mipi)\n");
    snprintf(sensor_version, sizeof(sensor_version), "cv2003_mipi V1.0.0");
}

void *Sensor_Create(void)
{
    memset(cv2003_mipi_sensor_if, 0, sizeof(cv2003_mipi_sensor_if));

    cv2003_mipi_sensor_if[0]  = (uint32_t)(uintptr_t)cv2003_name;
    cv2003_mipi_sensor_if[1]  = (uint32_t)(uintptr_t)GetSensorViAttr;
    cv2003_mipi_sensor_if[2]  = (uint32_t)(uintptr_t)SetSensorFlipMirror;
    cv2003_mipi_sensor_if[3]  = (uint32_t)(uintptr_t)GetSensorFlipMirror;
    cv2003_mipi_sensor_if[4]  = (uint32_t)(uintptr_t)SetSensorIris;
    cv2003_mipi_sensor_if[5]  = (uint32_t)(uintptr_t)Sensor_Init;
    cv2003_mipi_sensor_if[6]  = (uint32_t)(uintptr_t)SensorReset;
    cv2003_mipi_sensor_if[7]  = (uint32_t)(uintptr_t)Sensor_DeInit;
    cv2003_mipi_sensor_if[8]  = (uint32_t)(uintptr_t)SetSensorFmt;
    cv2003_mipi_sensor_if[9]  = (uint32_t)(uintptr_t)Sensor_Kick;
    cv2003_mipi_sensor_if[10] = (uint32_t)(uintptr_t)SetSensorReg;
    cv2003_mipi_sensor_if[11] = (uint32_t)(uintptr_t)SetExposureRatio;
    cv2003_mipi_sensor_if[12] = (uint32_t)(uintptr_t)GetExposureRatio;
    cv2003_mipi_sensor_if[13] = (uint32_t)(uintptr_t)GetSensorAttribute;
    cv2003_mipi_sensor_if[14] = (uint32_t)(uintptr_t)SetLaneNumMax;
    cv2003_mipi_sensor_if[15] = (uint32_t)(uintptr_t)GetSensorReg;
    cv2003_mipi_sensor_if[16] = (uint32_t)(uintptr_t)GetSensorAwbGain;
    cv2003_mipi_sensor_if[17] = (uint32_t)(uintptr_t)SetSensorAwbGain;
    cv2003_mipi_sensor_if[18] = 0; /* reserved */
    cv2003_mipi_sensor_if[19] = (uint32_t)(uintptr_t)SensorCommonIf;
    cv2003_mipi_sensor_if[20] = (uint32_t)(uintptr_t)GetAEDefault;
    cv2003_mipi_sensor_if[21] = (uint32_t)(uintptr_t)GetAEInfo;
    cv2003_mipi_sensor_if[22] = (uint32_t)(uintptr_t)SetIntt;
    cv2003_mipi_sensor_if[23] = (uint32_t)(uintptr_t)CalcSnsValidIntt;
    cv2003_mipi_sensor_if[24] = (uint32_t)(uintptr_t)SetGain;
    cv2003_mipi_sensor_if[25] = (uint32_t)(uintptr_t)CalcSnsValidGain;
    cv2003_mipi_sensor_if[26] = (uint32_t)(uintptr_t)SetSnsFrameH;
    cv2003_mipi_sensor_if[27] = (uint32_t)(uintptr_t)GetMirrorFlipBayerFormat;
    cv2003_mipi_sensor_if[28] = (uint32_t)(uintptr_t)GetUserSensorAwbGain;
    cv2003_mipi_sensor_if[29] = (uint32_t)(uintptr_t)GetSensorLtmCurve;
    cv2003_mipi_sensor_if[30] = (uint32_t)(uintptr_t)Sensor_Isconnect;

    return cv2003_mipi_sensor_if;
}

void Sensor_Destroy(void)
{
    memset(cv2003_mipi_sensor_if, 0, sizeof(cv2003_mipi_sensor_if));
}

/* Clock control (called by ISP during init) */
int set_clk_rate(uint32_t rate)
{
    printf("cv2003: set_clk_rate %u\n", rate);
    return 0;
}

int set_clk_enable(uint32_t enable)
{
    printf("cv2003: set_clk_enable %u\n", enable);
    return 0;
}

/* MIPI init callback (imported from SDK, called by sensor during init) */
extern int mipi_init(void *cfg);
