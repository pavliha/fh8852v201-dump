/**
 * CV2003 (GC4201AC) Sensor Driver — Register Init Tables
 * Extracted from ipcam binary via Ghidra + live memory analysis
 *
 * Sensor: CV2003 / GC4201AC (Galaxycore rebranded)
 * Interface: MIPI CSI, 1 lane, 800-999 Mbps
 * Resolution: 1920x1080
 * I2C: slave 0x1A (7-bit), /dev/i2c-0, 16-bit addr, 8-bit data
 * Sensor clock: 24 MHz (cis_clk_out)
 * Driver version: "mipi version: V1.1.1 17.0.0(build 000)(gc4201ac)"
 *
 * Init sequence (from decompiled FUN_0030de78):
 *   1. SensorDevice_Init(0x1A, mode=2)  // open I2C, set 16-bit addr
 *   2. Write register table (52 entries for selected fps)
 *   3. usleep(10000)  // 10ms settle time
 *   4. Sensor_Write(0x3141, 0x01)  // enable streaming
 *   5. v_cycle = (Sensor_Read(0x3021) << 8) | Sensor_Read(0x3020)
 *
 * IOCTL for I2C access:
 *   ioctl(fd, 0x704, 0)     — set address mode
 *   ioctl(fd, 0x706, addr)  — set slave address
 *   ioctl(fd, 0x707, &msg)  — read/write transfer
 */

#ifndef __CV2003_SENSOR_H__
#define __CV2003_SENSOR_H__

#include "fh_common.h"

#define CV2003_I2C_ADDR     0x35    /* 7-bit I2C address (confirmed by bus scan) */
#define CV2003_I2C_ADDR_RAW 0x6A    /* 8-bit address (0x35 << 1) */
#define CV2003_ADDR_MODE    2       /* 16-bit register addr, 8-bit data */
#define CV2003_SETTLE_US    10000   /* 10ms after register init */
#define CV2003_SENSOR_CLK   24000000 /* 24 MHz input clock */
#define CV2003_MIPI_LANES   1       /* Single MIPI lane */

/* Key registers */
#define CV2003_REG_STREAM_EN    0x3141  /* Write 1 to start streaming */
#define CV2003_REG_VCYCLE_LO    0x3020  /* V-cycle count low byte */
#define CV2003_REG_VCYCLE_HI    0x3021  /* V-cycle count high byte */
#define CV2003_REG_STANDBY      0x3000  /* 0=running, 1=standby */
#define CV2003_REG_PLL_DIV      0x3031  /* PLL divider (fps control) */
#define CV2003_REG_HCYCLE_LO    0x3024  /* H-cycle low */
#define CV2003_REG_HCYCLE_HI    0x3025  /* H-cycle high */

/* Format IDs (from config table) */
#define CV2003_FMT_1080P15  0x201
#define CV2003_FMT_1080P20  0x202
#define CV2003_FMT_1080P25  0x203
#define CV2003_FMT_1080P30  0x204

/* Sensor format for ISP: 0x203 (Bayer) */
#define CV2003_ISP_FORMAT   0x203

typedef struct {
    FH_UINT16 reg;
    FH_UINT16 val;
} cv2003_reg_t;

/**
 * Register differences between frame rates:
 *
 * 15fps: reg 0x3031=0x01, 0x3020=0xCA, 0x3021=0x08 (v_cycle=0x08CA=2250)
 *        0x3024=0xC0, 0x3025=0x03 (h_cycle=0x03C0=960)
 *        Uses 0x301C, 0x303C-303F regs
 *
 * 20fps: reg 0x3031=0x01, 0x3020=0xCA, 0x3021=0x08 (v_cycle=2250)
 *        0x3024=0xD0, 0x3025=0x02 (h_cycle=0x02D0=720)
 *        Uses 0x301C, 0x303C-303F regs
 *
 * 25fps: reg 0x3031=0x00, 0x3020=0x8C, 0x3021=0x0A (v_cycle=0x0A8C=2700)
 *        0x3024=0xD0, 0x3025=0x02 (h_cycle=720)
 *        Uses 0x3030, 0x3034-3037 regs (different PLL path)
 *
 * 30fps: reg 0x3031=0x00, 0x3020=0xCA, 0x3021=0x08 (v_cycle=2250)
 *        0x3024=0xD0, 0x3025=0x02 (h_cycle=720)
 *        Uses 0x3030, 0x3034-3037 regs
 *
 * Common first 36 registers are identical across all modes.
 * Only registers 36-51 differ (timing/PLL configuration).
 */

/* Live V-cycle from running camera: 5400 (= 2700 * 2) */
/* ISP reports 12.5 fps with sensor at v_cycle=2700, confirming 25fps mode */

#endif /* __CV2003_SENSOR_H__ */
