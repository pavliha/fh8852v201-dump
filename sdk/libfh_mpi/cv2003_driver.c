/**
 * CV2003 (GC4201AC) Sensor Driver — Clean-Room Implementation
 *
 * Reimplements the sensor vtable callbacks based on Ghidra analysis
 * of the register addresses and I2C protocol. No proprietary code.
 *
 * Each function performs simple I2C register reads/writes to control
 * the sensor's exposure, gain, mirror/flip, and streaming state.
 *
 * I2C: slave 0x35 (7-bit), 16-bit register address, 8-bit data
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>

#define I2C_RDWR 0x0707
#define I2C_M_RD 0x0001

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

#define SENSOR_ADDR 0x35

/* ================================================================
 * I2C helpers
 * ================================================================ */

static int g_i2c_fd = -1;

static int sns_write(uint16_t reg, uint8_t val)
{
    uint8_t wb[3] = { (reg >> 8) & 0xFF, reg & 0xFF, val };
    struct i2c_msg m = { .addr = SENSOR_ADDR, .flags = 0, .len = 3, .buf = wb };
    struct i2c_rdwr_ioctl_data x = { .msgs = &m, .nmsgs = 1 };
    return ioctl(g_i2c_fd, I2C_RDWR, &x);
}

static uint8_t sns_read(uint16_t reg)
{
    uint8_t wb[2] = { (reg >> 8) & 0xFF, reg & 0xFF };
    uint8_t rb[1] = { 0 };
    struct i2c_msg m[2] = {
        { .addr = SENSOR_ADDR, .flags = 0,        .len = 2, .buf = wb },
        { .addr = SENSOR_ADDR, .flags = I2C_M_RD, .len = 1, .buf = rb },
    };
    struct i2c_rdwr_ioctl_data x = { .msgs = m, .nmsgs = 2 };
    if (ioctl(g_i2c_fd, I2C_RDWR, &x) < 0) return 0;
    return rb[0];
}

/* ================================================================
 * Sensor state
 * ================================================================ */

static uint32_t g_v_cycle = 2700;   /* from 25fps mode */
static uint32_t g_exposure = 0;
static uint32_t g_gain = 0;
static int g_format_index = -1;     /* active config index */

/* Gain lookup table — maps gain index to register value.
 * 250 entries, derived from the sensor's analog gain curve. */
static const uint16_t gain_table[250] = {
    /* Simplified: linear ramp from 64 (1x) to 1024 (16x) */
    /* The real table from the binary has 250 entries but
     * a linear approximation works for basic functionality */
     64,  68,  72,  76,  80,  84,  88,  92,  96, 100,
    104, 108, 112, 116, 120, 124, 128, 132, 136, 140,
    144, 148, 152, 156, 160, 164, 168, 172, 176, 180,
    184, 188, 192, 196, 200, 204, 208, 212, 216, 220,
    224, 228, 232, 236, 240, 244, 248, 252, 256, 260,
    /* ... continue to 250 entries ... */
    264, 272, 280, 288, 296, 304, 312, 320, 328, 336,
    344, 352, 360, 368, 376, 384, 392, 400, 408, 416,
    424, 432, 440, 448, 456, 464, 472, 480, 488, 496,
    504, 512, 520, 528, 536, 544, 552, 560, 568, 576,
    584, 592, 600, 608, 616, 624, 632, 640, 648, 656,
    /* Fill remaining with max */
    664, 672, 680, 688, 696, 704, 712, 720, 728, 736,
    744, 752, 760, 768, 776, 784, 792, 800, 808, 816,
    824, 832, 840, 848, 856, 864, 872, 880, 888, 896,
    904, 912, 920, 928, 936, 944, 952, 960, 968, 976,
    984, 992,1000,1008,1016,1024,1024,1024,1024,1024,
    1024,1024,1024,1024,1024,1024,1024,1024,1024,1024,
    1024,1024,1024,1024,1024,1024,1024,1024,1024,1024,
    1024,1024,1024,1024,1024,1024,1024,1024,1024,1024,
    1024,1024,1024,1024,1024,1024,1024,1024,1024,1024,
    1024,1024,1024,1024,1024,1024,1024,1024,1024,1024,
    1024,1024,1024,1024,1024,1024,1024,1024,1024,1024,
    1024,1024,1024,1024,1024,1024,1024,1024,1024,1024,
    1024,1024,1024,1024,1024,1024,1024,1024,1024,1024,
    1024,1024,1024,1024,1024,1024,1024,1024,1024,1024,
    1024,1024,1024,1024,1024,1024,1024,1024,1024,1024,
};

/* 1080P25 register init table */
static const uint16_t init_regs_25fps[][2] = {
    {0x3300,0x03},{0x3422,0xbf},{0x3401,0x00},{0x3440,0x01},
    {0x3442,0x00},{0x3806,0x00},{0x3908,0x5f},{0x3909,0x00},
    {0x3929,0x01},{0x3158,0x01},{0x3159,0x01},{0x315a,0x01},
    {0x315b,0x01},{0x35b3,0x15},{0x3148,0x64},{0x3031,0x00},
    {0x3118,0x01},{0x3119,0x06},{0x3670,0x00},{0x3679,0x02},
    {0x3330,0x00},{0x320e,0x02},{0x3804,0x10},{0x35a1,0x06},
    {0x35a8,0x06},{0x35a9,0x06},{0x35aa,0x06},{0x35ab,0x06},
    {0x35ac,0x06},{0x35ad,0x06},{0x35ae,0x07},{0x35af,0x07},
    {0x333b,0x01},{0x3338,0x08},{0x3339,0x00},{0x3144,0x20},
    {0x3030,0x01},{0x3020,0x8c},{0x3021,0x0a},{0x3024,0xd0},
    {0x3025,0x02},{0x3038,0x06},{0x3039,0x00},{0x303a,0x80},
    {0x303b,0x07},{0x3034,0x04},{0x3035,0x00},{0x3036,0x38},
    {0x3037,0x04},{0x3908,0x51},{0x390a,0x02},{0x3000,0x00},
};
#define INIT_REG_COUNT (sizeof(init_regs_25fps) / sizeof(init_regs_25fps[0]))

/* ================================================================
 * Vtable callback implementations
 * ================================================================ */

/* [1] Get sensor info — returns resolution and timing info */
static int cv2003_get_info(uint16_t *info)
{
    if (!info) return -1;
    /* From decompilation: copies 8 uint16 + 1 uint32 from config table */
    info[0] = 1920;    /* width */
    info[1] = 1080;    /* height */
    info[2] = 1920;    /* active width */
    info[3] = 1080;    /* active height */
    info[4] = 0;       /* x offset */
    info[5] = 0;       /* y offset */
    info[6] = 1920;    /* total width */
    info[7] = 1080;    /* total height */
    /* info[8..9] = uint32 pixel clock */
    *(uint32_t *)(info + 10) = 74250000; /* ~74.25 MHz */
    return 0;
}

/* [2] Set mirror/flip — reg 0x3028 bits [1:0] */
static int cv2003_set_mirror_flip(uint32_t mode)
{
    uint8_t val = sns_read(0x3028);
    val = (val & 0xFC) | ((mode & 1) << 1) | ((mode >> 1) & 1);
    sns_write(0x3028, val);
    return 0;
}

/* [3] Get mirror/flip */
static int cv2003_get_mirror_flip(uint32_t *mode)
{
    uint8_t val = sns_read(0x3028);
    *mode = ((val & 1) << 1) | ((val >> 1) & 1);
    return 0;
}

/* [8] Set format and init registers — the main init function */
int cv2003_set_format(int format)
{
    /* Write register init table */
    for (int i = 0; i < (int)INIT_REG_COUNT; i++) {
        sns_write(init_regs_25fps[i][0], init_regs_25fps[i][1]);
    }
    usleep(10000); /* 10ms settle */

    /* Enable streaming */
    sns_write(0x3141, 0x01);

    /* Read back V-cycle */
    uint8_t vl = sns_read(0x3020);
    uint8_t vh = sns_read(0x3021);
    g_v_cycle = ((uint32_t)vh << 8) | vl;

    g_format_index = 0;
    return 0;
}

/* [11] Set gain (alternative entry) */
static int cv2003_set_gain_alt(uint32_t gain)
{
    return 0; /* stub — main gain is in [24] */
}

/* [22] Set exposure — regs 0x3048/3049/304A (24-bit) */
static int cv2003_set_exposure(int exposure)
{
    uint32_t val = g_v_cycle - exposure * 2;
    if (val < 8) val = 8;
    if (val > g_v_cycle - 4) val = g_v_cycle - 4;

    g_exposure = exposure;
    sns_write(0x304a, (val >> 16) & 0xFF);
    sns_write(0x3049, (val >> 8) & 0xFF);
    sns_write(0x3048, val & 0xFF);
    return 0;
}

/* [23] Get exposure */
static int cv2003_get_exposure(int *exposure)
{
    *exposure = g_exposure;
    return 0;
}

/* [24] Set analog gain — reg 0x3154 (table index) + 0x314C/D (10-bit) */
static int cv2003_set_gain(uint32_t gain)
{
    /* Find gain table index */
    uint32_t idx = 0;
    for (idx = 0; idx < 249; idx++) {
        if (gain >= gain_table[idx] && gain <= gain_table[idx + 1])
            break;
    }

    /* Calculate register value */
    uint32_t reg_gain = gain >> 5;
    if (reg_gain < 0x40) reg_gain = 0x40;
    if (reg_gain > 0x3FF) reg_gain = 0x400;

    g_gain = gain;

    /* Set gain mode based on threshold */
    if (gain < 0x401) {
        sns_write(0x3804, 0x10);
    }
    if (gain > 0x7FF) {
        sns_write(0x3804, 0x15);
    }

    /* Write gain registers */
    sns_write(0x3154, idx & 0xFF);
    sns_write(0x314d, (reg_gain >> 8) & 0xFF);
    sns_write(0x314c, reg_gain & 0xFF);
    return 0;
}

/* [25] Get gain */
static int cv2003_get_gain(uint32_t *gain)
{
    *gain = g_gain;
    return 0;
}

/* [9] Sensor kick — start streaming (called by ISP) */
static int cv2003_kick(void)
{
    sns_write(0x3141, 0x01);
    return 0;
}

/* [30] Init helper — called during ISP init */
static int cv2003_init_helper(void)
{
    /* Just return success — real init done in set_format */
    return 0;
}

/* [6] Pre-init — called before format set */
static int cv2003_pre_init(void)
{
    return 0;
}

/* ================================================================
 * Vtable construction
 * ================================================================ */

static const char sensor_name[] = "cv2003_mipi";

/**
 * Build the sensor vtable for ISP registration.
 * The vtable is 31 uint32 entries (124 bytes).
 * Each entry is either a function pointer or NULL.
 *
 * The ISP copies this to its internal struct at offset 0x162C
 * and calls the function pointers during frame processing.
 */
void cv2003_build_vtable(uint32_t vtable[31])
{
    memset(vtable, 0, 124);

    vtable[0]  = (uint32_t)(uintptr_t)sensor_name;
    vtable[1]  = (uint32_t)(uintptr_t)cv2003_get_info;
    vtable[2]  = (uint32_t)(uintptr_t)cv2003_set_mirror_flip;
    vtable[3]  = (uint32_t)(uintptr_t)cv2003_get_mirror_flip;
    /* [4] get ISP default config — NULL OK, ISP uses defaults */
    vtable[5]  = 0;
    vtable[6]  = (uint32_t)(uintptr_t)cv2003_pre_init;
    vtable[7]  = 0;
    vtable[8]  = (uint32_t)(uintptr_t)cv2003_set_format;
    vtable[9]  = (uint32_t)(uintptr_t)cv2003_kick;
    vtable[10] = 0;
    vtable[11] = (uint32_t)(uintptr_t)cv2003_set_gain_alt;
    vtable[12] = (uint32_t)(uintptr_t)cv2003_set_gain_alt;
    /* [13-21] optional callbacks — NULL */
    vtable[22] = (uint32_t)(uintptr_t)cv2003_set_exposure;
    vtable[23] = (uint32_t)(uintptr_t)cv2003_get_exposure;
    vtable[24] = (uint32_t)(uintptr_t)cv2003_set_gain;
    vtable[25] = (uint32_t)(uintptr_t)cv2003_get_gain;
    /* [26-29] optional */
    vtable[30] = (uint32_t)(uintptr_t)cv2003_init_helper;
}

/**
 * Initialize the sensor driver.
 * Call this before registering the vtable with the ISP.
 */
int cv2003_init(void)
{
    g_i2c_fd = open("/dev/i2c-0", O_RDWR);
    if (g_i2c_fd < 0) {
        perror("cv2003_init: open /dev/i2c-0");
        return -1;
    }
    return 0;
}

void cv2003_deinit(void)
{
    if (g_i2c_fd >= 0) {
        close(g_i2c_fd);
        g_i2c_fd = -1;
    }
}
