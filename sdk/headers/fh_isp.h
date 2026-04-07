/**
 * Fullhan FH8852V201 SDK — ISP & Sensor API
 * Reverse-engineered from ipcam binary via Ghidra
 *
 * Two API layers:
 *   API_ISP_*  — Low-level ISP hardware control (100+ functions)
 *   FHAdv_*    — High-level convenience wrappers
 *
 * ISP pipeline: Sensor → MIPI CSI → ISP → VPSS
 * Sensor I2C bus: i2c-0 at 0xF0200000 (IRQ 43)
 * ISP interrupt: IRQ 39 (ispp)
 */

#ifndef __FH_ISP_H__
#define __FH_ISP_H__

#include "fh_common.h"

/* ================================================================
 * ISP Register Map (from isp_core_get_param_addr offsets)
 *
 * ISP parameters are stored in a memory-mapped register block.
 * Each API_ISP_Get*Cfg function reads from specific offsets.
 * These offsets define the ISP hardware register layout:
 *
 *   0x0010-0x0011  Global flags (color mode, mirror, enable bits)
 *   0x002C-0x00FF  Auto-exposure (AE) configuration
 *   0x03C8-0x044F  3D noise reduction (NR3D)
 *   0x03E0-0x03FF  NR3D filter params
 *   0x03FE-0x0410  NR3D LUT tables
 *   0x04C0-0x04C8  Contrast configuration
 *   0x04E0-0x0590  Saturation configuration
 *   0x0590-0x06DC  Gamma curves (2 tables × 80 entries × 2 uint16)
 *   0x0A9C-0x0AF9  Sharpness (APC) configuration
 *   0x0F1B-0x0F37  NR3D advanced params
 * ================================================================ */

/* ================================================================
 * ISP Structures (from Ghidra decompilation of Get functions)
 * ================================================================ */

/**
 * Contrast configuration.
 * From API_ISP_GetContrastCfg: reads from ISP offset 0x4C0.
 * Size: ~14 bytes (3.5 uint32s)
 */
typedef struct {
    FH_UINT32 auto_enable;   /* [0]  Bit from global flags 0x11 */
    FH_UINT32 enable;        /* [1]  Contrast enable (offset 0x4C0 bit 0) */
    FH_UINT8  strength;      /* [8]  Contrast strength (offset 0x4C4) */
    FH_UINT8  offset;        /* [9]  Contrast offset (offset 0x4C6) */
    FH_UINT8  lut[4];        /* [10] Contrast LUT (offset 0x4C8, 4 bytes) */
} FH_ISP_CONTRAST_CFG;

/**
 * Gamma configuration.
 * From API_ISP_GetGammaCfg: reads from ISP offset 0x590.
 * Contains two gamma curves of 80 entries each (10-bit values).
 *
 * Size: 0x158 bytes (86 uint32s)
 */
typedef struct {
    FH_UINT32 auto_enable;   /* [0]  Bit from global flags 0x11 */
    FH_UINT32 mode;          /* [1]  Gamma mode (bits from offset 0x591) */
    FH_UINT32 curve_sel;     /* [2]  Curve selection (1 or 2) */
    FH_UINT32 strength;      /* [3]  Upper nibble of offset 0x590 */
    FH_UINT16 curve1[160];   /* [4]  Gamma curve 1: 80 pairs of 10-bit values (offset 0x6D8+) */
    FH_UINT32 curve2_sel;    /* [0x54] Curve 2 selection */
    FH_UINT32 curve2_mode;   /* [0x55] Curve 2 mode */
    FH_UINT16 curve2[160];   /* [0x56] Gamma curve 2: 80 pairs (offset 0x598+) */
} FH_ISP_GAMMA_CFG;

/**
 * Auto-exposure default configuration.
 * From API_ISP_GetAeDefaultCfg: reads from ISP offset 0x2C.
 * Very large struct — packed bitfields from registers.
 *
 * Size: 0x1A0 bytes (104 uint32s)
 *
 * Key fields:
 *   [0..14]  AE mode flags (15 individual bits from offsets 0x2C-0x2D)
 *   [0x14]   AE target (offset 0x30)
 *   [0x19]   AE speed params (offset 0x66)
 *   [0x1A..0x3F] AE weight table (64 entries, 4-bit pairs from offset 0x68)
 *   [0x5A..0x5B] AE luma compensation (offset 0xE8-0xE9)
 *   [0x5C]   AE integration time max (offset 0xEC, 18-bit)
 *   [0x5D]   AE gain limits (offset 0xF0)
 *   [0x5E]   AE anti-flicker (offset 0xF8)
 *   [0x5F]   AE frequency (offset 0xF8, bits 4+)
 *   [0x60..0x62] AE exposure limits (offset 0x38-0x42)
 *   [0x64]   AE ISP digital gain max (offset 0x54, 20-bit)
 *   [0x67]   AE frame rate (offset 0xF4)
 */
typedef struct {
    FH_UINT32 flags[15];         /* [0x00]  AE mode flags */
    FH_UINT8  reserved1[12];     /* padding */
    FH_UINT8  target;            /* [0x50]  AE target luminance */
    FH_UINT8  speed_up;          /* [0x51]  AE convergence speed up */
    FH_UINT8  speed_down;        /* [0x52]  AE convergence speed down */
    FH_UINT8  tolerance;         /* [0x53]  AE tolerance */
    FH_UINT8  ae_params[12];     /* [0x54]  AE algorithm params */
    FH_UINT8  speed;             /* [0x5C]  AE speed */
    FH_UINT8  compensation;      /* [0x5D]  AE compensation */
    FH_UINT8  flicker_mode;      /* [0x5E]  Anti-flicker (0=50Hz, 1=60Hz) */
    FH_UINT8  flicker_freq;      /* [0x5F]  Flicker frequency */
    FH_UINT8  exposure_min;      /* [0x60]  Min exposure */
    FH_UINT8  exposure_max_h;    /* [0x61]  Max exposure high byte */
    FH_UINT8  gain_limit[2];     /* [0x62]  Gain limits */
    FH_UINT16 integration_max;   /* [0x64]  Max integration time */
    FH_UINT8  weight_table[128]; /* [0x66]  64 weight entries (4-bit each × 2) */
    FH_UINT8  luma_comp[16];     /* [0x166] Luma compensation table */
    FH_UINT32 isp_dgain_max;     /* [0x190] ISP digital gain max (20-bit) */
    FH_UINT16 gain_range[4];     /* [0x194] Gain range limits */
    FH_UINT16 frame_rate;        /* [0x19C] Target frame rate */
    FH_UINT8  hist_offset[2];    /* [0x19E] Histogram offset */
} FH_ISP_AE_DEFAULT_CFG;

/**
 * 3D Noise Reduction configuration.
 * From API_ISP_GetNr3dCfg: reads from ISP offset 0x3C8.
 *
 * Size: 0xCA bytes (~50 uint32s)
 */
typedef struct {
    FH_UINT32 auto_enable;   /* [0]  Global enable (bit 7 of offset 0x10) */
    FH_UINT32 enable;        /* [1]  NR3D enable (bit 0 of offset 0x3C8) */
    FH_UINT32 mode1;         /* [2]  Mode flag 1 (offset 0x3C9) */
    FH_UINT32 mode2;         /* [3]  Mode flag 2 */
    FH_UINT32 spatial_en;    /* [4]  Spatial filter enable */
    FH_UINT32 temporal_en;   /* [5]  Temporal filter enable */
    FH_UINT8  spatial_str;   /* [6]  Spatial filter strength (3-bit) */
    FH_UINT32 ref_en;        /* [7]  Reference frame enable */
    FH_UINT8  motion_str;    /* [8]  Motion threshold (5-bit) */
    FH_UINT32 adaptive;      /* [9]  Adaptive mode */
    FH_UINT32 blend_mode;    /* [10] Blend mode */
    FH_UINT16 max_strength;  /* [11] Max filter strength (10-bit) */
    FH_UINT8  filter_params[21]; /* Spatial/temporal filter LUT */
    FH_UINT16 motion_lut[9];    /* Motion detection LUT */
    /* ... additional packed params from offsets 0xF1B-0xF37 */
} FH_ISP_NR3D_CFG;

/**
 * Sharpness (APC - Automatic Picture Control) configuration.
 * From API_ISP_GetApcCfg: reads from ISP offset 0xA9C.
 *
 * Size: 0x8E bytes (~36 uint32s)
 */
typedef struct {
    FH_UINT32 auto_enable;    /* [0]  Global enable (bit from offset 0x11) */
    FH_UINT32 enable;         /* [1]  APC enable */
    FH_UINT32 mode_flags[6];  /* [2-7]  6 mode flags */
    FH_UINT8  edge_params[4]; /* [8]  Edge detection params (offset 0xAA0) */
    FH_UINT16 thresholds[4];  /* [0x26] 4 threshold values (10-bit, offset 0xAA4) */
    FH_UINT8  strength_lut[48]; /* Strength LUT per brightness zone */
    FH_UINT8  detail_params[36]; /* Detail enhancement params */
} FH_ISP_APC_CFG;

/**
 * Saturation configuration.
 * From API_ISP_GetSaturation: reads from ISP offset 0x4E0.
 *
 * Size: ~0xC0 bytes
 */
typedef struct {
    FH_UINT32 auto_enable;   /* [0]  Global enable */
    FH_UINT32 enable;        /* [1]  Saturation enable */
    FH_UINT8  global_sat;    /* [8]  Global saturation (7-bit signed) */
    FH_UINT8  offset;        /* [9]  Saturation offset */
    FH_UINT8  range_min;     /* [10] Min saturation */
    FH_UINT8  range_max;     /* [11] Max saturation */
    FH_UINT32 cb_mode;       /* [3]  Cb adjustment mode */
    FH_UINT32 cr_mode;       /* [4]  Cr adjustment mode */
    FH_UINT8  cb_gain;       /* [5]  Cb gain */
    FH_UINT32 hue_enable;    /* [6]  Hue adjustment enable */
    FH_UINT32 hue_mode;      /* [7]  Hue mode */
    FH_UINT8  hue_offset;    /* [0x20] Hue offset */
    FH_UINT8  sat_lut[48];   /* Saturation LUT per brightness zone */
    FH_UINT8  hue_lut[128];  /* Hue LUT (32 entries × 4 bytes) */
} FH_ISP_SATURATION_CFG;

/* ================================================================
 * High-Level API (FHAdv_*)
 * ================================================================ */

/* ---- Sensor Management (FHAdv_MS_*) ---- */

/**
 * Probe I2C bus for sensor and create sensor control structure.
 * @param name  Sensor name to probe (e.g. "cv2003_mipi"), or NULL to auto-detect
 * @return Pointer to sensor descriptor, or NULL if not found
 *
 * Probes up to 5 sensor slots. For each slot:
 *   1. Calls sensor create function (via function pointer table)
 *   2. Sensor verifies I2C chip ID
 *   3. If match, returns sensor descriptor
 *
 * Sensor descriptor layout (offset → field):
 *   [0]  = sensor name string pointer
 *   [4]  = create function pointer
 *   [8]  = sensor control struct pointer (passed to API_ISP_SensorRegCb)
 *   [0x78] = I2C probe function pointer
 */
void *FHAdv_MS_sensor_ProbeAndCreate(const char *name);

/**
 * Get current sensor control structure.
 * @return Pointer to active sensor ctrl, or NULL
 */
void *FHAdv_MS_GetCurSensorCtrl(void);

/**
 * Get current sensor name string.
 */
const char *FHAdv_MS_GetCursensorName(void);

/**
 * Set path for sensor parameter files (.hex tuning files).
 * Default: /usr/app/sensor/
 */
FH_SINT32 FHAdv_MS_SetSensorParamPath(const char *path);

/**
 * Get sensor parameters.
 */
FH_SINT32 FHAdv_MS_GetSensorParam(/* params */);

/* ---- ISP Control (FHAdv_Isp_*) ---- */

/**
 * Initialize ISP with default parameters.
 * Reads contrast, saturation, APC, AE defaults from hardware.
 */
FH_SINT32 FHAdv_Isp_Init(void);

/**
 * Initialize sensor via I2C (using sensor control struct).
 * @param sensor_ctrl  From FHAdv_MS_sensor_ProbeAndCreate()[8]
 */
FH_SINT32 FHAdv_Isp_SensorInit(void *sensor_ctrl);

/**
 * Set color mode.
 * @param mode  0=B&W (night/IR), 1=color (day)
 *
 * Called by p_image_set_day_night_mode:
 *   day_night_mode=0 (auto day) → SetColorMode(1)
 *   day_night_mode=1 (force day) → SetColorMode(0)  [inverted!]
 *   day_night_mode=2+ → SetColorMode(1)
 */
FH_SINT32 FHAdv_Isp_SetColorMode(FH_UINT32 mode);
FH_SINT32 FHAdv_Isp_GetColorMode(FH_UINT32 *mode);

/**
 * Image quality adjustments (0-255 range).
 */
FH_SINT32 FHAdv_Isp_SetBrightness(FH_UINT32 value);
FH_SINT32 FHAdv_Isp_GetBrightness(FH_UINT32 *value);
FH_SINT32 FHAdv_Isp_SetContrast(FH_UINT32 value);
FH_SINT32 FHAdv_Isp_GetContrast(FH_UINT32 *value);
FH_SINT32 FHAdv_Isp_SetSaturation(FH_UINT32 value);
FH_SINT32 FHAdv_Isp_GetSaturation(FH_UINT32 *value);
FH_SINT32 FHAdv_Isp_SetSharpeness(FH_UINT32 value);
FH_SINT32 FHAdv_Isp_GetSharpeness(FH_UINT32 *value);
FH_SINT32 FHAdv_Isp_SetChroma(FH_UINT32 value);
FH_SINT32 FHAdv_Isp_GetChroma(FH_UINT32 *value);

/**
 * Mirror and flip.
 */
FH_SINT32 FHAdv_Isp_SetMirrorAndflip(FH_UINT32 mirror, FH_UINT32 flip);
FH_SINT32 FHAdv_Isp_GetMirrorAndflip(FH_UINT32 *mirror, FH_UINT32 *flip);

/**
 * Auto-exposure mode.
 */
FH_SINT32 FHAdv_Isp_SetAEMode(FH_UINT32 mode);
FH_SINT32 FHAdv_Isp_GetAEMode(FH_UINT32 *mode);

/**
 * AWB (Auto White Balance) gain.
 */
FH_SINT32 FHAdv_Isp_SetAwbGain(/* params */);
FH_SINT32 FHAdv_Isp_GetAwbGain(/* params */);

/* ---- Smart IR (FHAdv_SmartIR_*) ---- */

/**
 * Initialize Smart IR day/night switching.
 * Default thresholds (from decompilation):
 *   day_threshold:   0x1000 (4096)
 *   night_threshold: 0x500  (1280)
 *   switch_delay:    0x140  (320)
 *   hysteresis:      0x708  (1800)
 *   min_delay:       0x3C   (60)
 *   max_delay:       1000
 */
FH_SINT32 FHAdv_SmartIR_Init(void);
FH_SINT32 FHAdv_SmartIR_UnInit(void);

/**
 * Get current day/night status.
 * @return 0=day, 1=night
 */
FH_SINT32 FHAdv_SmartIR_GetDayNightStatus(/* status */);

FH_SINT32 FHAdv_SmartIR_SetCfg(/* cfg */);
FH_SINT32 FHAdv_SmartIR_GetCfg(/* cfg */);
FH_SINT32 FHAdv_SmartIR_SetDebugLevel(FH_UINT32 level);

/* ---- Motion Detection (FHAdv_MD_*) ---- */

FH_SINT32 FHAdv_MD_Init(/* params */);
FH_SINT32 FHAdv_MD_SetConfig(/* params */);
FH_SINT32 FHAdv_MD_GetConfig(/* params */);
FH_SINT32 FHAdv_MD_GetResult(/* result */);
FH_SINT32 FHAdv_MD_SetStableTime(FH_UINT32 time);
FH_SINT32 FHAdv_MD_SetWaggleCheck(/* params */);
FH_SINT32 FHAdv_MD_GetYmean(/* params */);
FH_SINT32 FHAdv_MD_Set_Check_Mode(FH_UINT32 mode);

/* Extended motion detection */
FH_SINT32 FHAdv_MD_Ex_Init(/* params */);
FH_SINT32 FHAdv_MD_Ex_SetConfig(/* params */);
FH_SINT32 FHAdv_MD_Ex_GetConfig(/* params */);
FH_SINT32 FHAdv_MD_Ex_GetResult(/* result */);
FH_SINT32 FHAdv_MD_Ex_SetMbNum(/* params */);
FH_SINT32 FHAdv_MD_Ex_SetWaggleCheck(/* params */);

/* Combined MD/CD check */
FH_SINT32 FHAdv_MD_CD_Check(/* params */);
FH_SINT32 FHAdv_MD_CD_SetViWidthHeightMax(/* params */);

/* ---- Camera Tamper / Cover Detection (FHAdv_CD_*) ---- */

FH_SINT32 FHAdv_CD_Init(/* params */);
FH_SINT32 FHAdv_CD_SetConfig(/* params */);
FH_SINT32 FHAdv_CD_GetConfig(/* params */);
FH_SINT32 FHAdv_CD_GetResult(/* result */);
FH_SINT32 FHAdv_CD_SetBkgChangeFilter(/* params */);
FH_SINT32 FHAdv_CD_SetCoverSense(/* params */);

/* ---- OSD Overlay (FHAdv_Osd_*) ---- */

/**
 * Initialize OSD system.
 * Must call FHAdv_Osd_LoadFontLib first with font.bin.
 */
FH_SINT32 FHAdv_Osd_Init(/* params */);
FH_SINT32 FHAdv_Osd_UnInit(void);

FH_SINT32 FHAdv_Osd_LoadFontLib(const char *path);
FH_SINT32 FHAdv_Osd_UnLoadFontLib(void);
FH_SINT32 FHAdv_Osd_InvalidateFont(void);

FH_SINT32 FHAdv_Osd_SetText(/* params */);
FH_SINT32 FHAdv_Osd_SetTextLineDisp(/* params */);
FH_SINT32 FHAdv_Osd_Muti_SetText(/* params */);
FH_SINT32 FHAdv_Osd_Muti_SetTextLine(/* params */);
FH_SINT32 FHAdv_Osd_Muti_SetMaxframe(/* params */);

FH_SINT32 FHAdv_Osd_SetLogo(/* params */);
FH_SINT32 FHAdv_Osd_SetLogo_Invert(/* params */);
FH_SINT32 FHAdv_Osd_SetMask(/* params */);
FH_SINT32 FHAdv_Osd_SetGbox(/* params */);
FH_SINT32 FHAdv_Osd_SetPolygon(/* params */);
FH_SINT32 FHAdv_Osd_SetTagCallback(/* params */);
FH_SINT32 FHAdv_Osd_Config_TwinkleTime(/* params */);
FH_SINT32 FHAdv_Osd_Config_GboxPixel(/* params */);

FH_SINT32 FHAdv_Osd_Font_RegisterCallback(/* params */);

/* Extended OSD (per-channel) */
FH_SINT32 FHAdv_Osd_Ex_SetText(/* params */);
FH_SINT32 FHAdv_Osd_Ex_SetTextLine(/* params */);
FH_SINT32 FHAdv_Osd_Ex_Muti_SetTextLine(/* params */);
FH_SINT32 FHAdv_Osd_Ex_SetLogo(/* params */);
FH_SINT32 FHAdv_Osd_Ex_SetLogo_Invert(/* params */);
FH_SINT32 FHAdv_Osd_Ex_SetMask(/* params */);
FH_SINT32 FHAdv_Osd_Ex_SetGbox(/* params */);
FH_SINT32 FHAdv_Osd_Ex_SetPolygon(/* params */);
FH_SINT32 FHAdv_Osd_Ex_Font_RegisterCallback(/* params */);
FH_SINT32 FHAdv_Osd_Ex_Circle_Cover(/* params */);
FH_SINT32 FHAdv_Osd_Ex_Print_User_Config(/* params */);

/* ---- Smart Dimming (FHAdv_SmartDim_*) ---- */

FH_SINT32 FHAdv_SmartDim_SetConfig(/* params */);
FH_SINT32 FHAdv_SmartDim_GetConfig(/* params */);
FH_SINT32 FHAdv_SmartDim_Run(/* params */);
FH_SINT32 FHAdv_SmartDim_Debug(/* params */);

/* ================================================================
 * Low-Level ISP API (API_ISP_*)
 * ================================================================ */

/**
 * Allocate ISP memory.
 * @param width   Sensor width
 * @param height  Sensor height
 */
FH_SINT32 API_ISP_MemInit(FH_UINT32 width, FH_UINT32 height);

/**
 * Register sensor callback (sensor control struct from ProbeAndCreate).
 * @param index        Sensor index (0)
 * @param sensor_ctrl  Sensor control struct
 */
FH_SINT32 API_ISP_SensorRegCb(FH_UINT32 index, void *sensor_ctrl);
FH_SINT32 API_ISP_SensorUnRegCb(FH_UINT32 index);

/**
 * Initialize sensor via I2C.
 */
FH_SINT32 API_ISP_SensorInit(void);

/**
 * Start sensor streaming (kick the sensor to output frames).
 */
FH_SINT32 API_ISP_SensorKick(void);

/**
 * Set sensor Bayer format.
 * @param fmt  0x203 for CV2003/GC sensors
 */
FH_SINT32 API_ISP_SetSensorFmt(FH_UINT32 fmt);

/**
 * Initialize ISP pipeline.
 */
FH_SINT32 API_ISP_Init(void);

/**
 * Shutdown ISP.
 */
FH_SINT32 API_ISP_Exit(void);

/**
 * Run AE/AWB algorithms (called periodically).
 */
FH_SINT32 API_ISP_AE_AWB_Run(void);

/**
 * Pause/resume ISP processing.
 */
FH_SINT32 API_ISP_Pause(void);
FH_SINT32 API_ISP_Resume(void);

/**
 * Start ISP processing.
 */
FH_SINT32 API_ISP_KickStart(void);

/**
 * Load ISP parameter file (tuning hex files).
 */
FH_SINT32 API_ISP_LoadIspParam(/* params */);

/**
 * Get ISP version.
 */
FH_SINT32 API_ISP_GetVersion(/* version */);

/**
 * Mirror enable.
 */
FH_SINT32 API_ISP_MirrorEnable(/* params */);

/**
 * Get mirror/flip status.
 */
FH_SINT32 API_ISP_GetMirrorAndflip(/* params */);

/* ---- Auto Exposure ---- */
FH_SINT32 API_ISP_SetAeDefaultCfg(/* params */);
FH_SINT32 API_ISP_GetAeDefaultCfg(/* params */);
FH_SINT32 API_ISP_SetAeInfo(/* params */);
FH_SINT32 API_ISP_GetAeInfo(/* params */);
FH_SINT32 API_ISP_GetAeStatus(/* params */);
FH_SINT32 API_ISP_SetAeUserStatus(/* params */);
FH_SINT32 API_ISP_SetAeRouteCfg(/* params */);
FH_SINT32 API_ISP_GetAeRouteCfg(/* params */);
FH_SINT32 API_ISP_MapInttGain(/* params */);

/* ---- Auto White Balance ---- */
FH_SINT32 API_ISP_SetAwbDefaultCfg(/* params */);  /* not found, inferred */
FH_SINT32 API_ISP_GetAwbDefaultCfg(/* params */);
FH_SINT32 API_ISP_GetAwbGain(/* params */);
FH_SINT32 API_ISP_GetAwbStatus(/* params */);
FH_SINT32 API_ISP_GetAwbStat(/* params */);
FH_SINT32 API_ISP_GetAwbStatCfg(/* params */);
FH_SINT32 API_ISP_GetAwbAdvStat(/* params */);
FH_SINT32 API_ISP_GetAwbAdvStatCfg(/* params */);

/* ---- Image Quality ---- */
FH_SINT32 API_ISP_SetContrastCfg(/* params */);   /* inferred */
FH_SINT32 API_ISP_GetContrastCfg(/* params */);
FH_SINT32 API_ISP_SetBrightnessCfg(/* params */); /* inferred */
FH_SINT32 API_ISP_GetBrightnessCfg(/* params */);
FH_SINT32 API_ISP_SetSaturation(/* params */);     /* inferred */
FH_SINT32 API_ISP_GetSaturation(/* params */);
FH_SINT32 API_ISP_SetApcCfg(/* params */);
FH_SINT32 API_ISP_GetApcCfg(/* params */);
FH_SINT32 API_ISP_SetApcInitCfg(/* params */);
FH_SINT32 API_ISP_SetApcMtCfg(/* params */);
FH_SINT32 API_ISP_GetApcMtCfg(/* params */);

/* ---- Noise Reduction ---- */
FH_SINT32 API_ISP_SetNr2dCfg(/* params */);   /* inferred */
FH_SINT32 API_ISP_GetNr2dCfg(/* params */);
FH_SINT32 API_ISP_SetNr3dCfg(/* params */);   /* inferred */
FH_SINT32 API_ISP_GetNr3dCfg(/* params */);
FH_SINT32 API_ISP_SetYnrCfg(/* params */);    /* inferred */
FH_SINT32 API_ISP_GetYnrCfg(/* params */);
FH_SINT32 API_ISP_SetYnrMtCfg(/* params */);  /* inferred */
FH_SINT32 API_ISP_GetYnrMtCfg(/* params */);
FH_SINT32 API_ISP_SetCnrCfg(/* params */);    /* inferred */
FH_SINT32 API_ISP_GetCnrCfg(/* params */);
FH_SINT32 API_ISP_SetCnrMtCfg(/* params */);  /* inferred */
FH_SINT32 API_ISP_GetCnrMtCfg(/* params */);

/* ---- Color Correction ---- */
FH_SINT32 API_ISP_SetCcmCfg(/* params */);    /* inferred */
FH_SINT32 API_ISP_GetCcmCfg(/* params */);
FH_SINT32 API_ISP_GetCcmHwCfg(/* params */);
FH_SINT32 API_ISP_GetCcm2HwCfg(/* params */);

/* ---- Gamma ---- */
FH_SINT32 API_ISP_SetGammaCfg(/* params */);  /* inferred */
FH_SINT32 API_ISP_GetGammaCfg(/* params */);

/* ---- DRC (Dynamic Range Compression) ---- */
FH_SINT32 API_ISP_SetDrcCfg(/* params */);    /* inferred */
FH_SINT32 API_ISP_GetDrcCfg(/* params */);

/* ---- HLR (Highlight Recovery) ---- */
FH_SINT32 API_ISP_SetHlrCfg(/* params */);    /* inferred */
FH_SINT32 API_ISP_GetHlrCfg(/* params */);

/* ---- Dead Pixel Correction ---- */
FH_SINT32 API_ISP_SetDpcCfg(/* params */);    /* inferred */
FH_SINT32 API_ISP_GetDpcCfg(/* params */);
FH_SINT32 API_ISP_GetStaticDpc(/* params */);

/* ---- Black Level Correction ---- */
FH_SINT32 API_ISP_SetBlcAttr(/* params */);   /* inferred */
FH_SINT32 API_ISP_GetBlcAttr(/* params */);

/* ---- CFA (Color Filter Array) Interpolation ---- */
FH_SINT32 API_ISP_SetCfaCfg(/* params */);    /* inferred */
FH_SINT32 API_ISP_GetCfaCfg(/* params */);

/* ---- ACR (Auto Chroma Reduction) ---- */
FH_SINT32 API_ISP_SetAcrCfg(/* params */);
FH_SINT32 API_ISP_GetAcrCfg(/* params */);

/* ---- Green Balance ---- */
FH_SINT32 API_ISP_SetGbCfg(/* params */);     /* inferred */
FH_SINT32 API_ISP_GetGbCfg(/* params */);

/* ---- Anti-Purple Fringing ---- */
FH_SINT32 API_ISP_SetAntiPurpleBoundary(/* params */);
FH_SINT32 API_ISP_GetAntiPurpleBoundary(/* params */);

/* ---- Smart IR ---- */
FH_SINT32 API_ISP_GetSmartIrCfg(/* params */);

/* ---- Statistics ---- */
FH_SINT32 API_ISP_GetGlobeStat(/* params */);
FH_SINT32 API_ISP_GetGlobeStatCfg(/* params */);
FH_SINT32 API_ISP_GetGlobeStatInfo(/* params */);
FH_SINT32 API_ISP_GetSharpenStat(/* params */);
FH_SINT32 API_ISP_GetSharpenStatCfg(/* params */);
FH_SINT32 API_ISP_CheckStatReady(void);

/* ---- Raw Capture ---- */
FH_SINT32 API_ISP_GetRaw(/* params */);
FH_SINT32 API_ISP_GetRawSize(/* params */);
FH_SINT32 API_ISP_GetRawPre(/* params */);
FH_SINT32 API_ISP_GetRawPost(/* params */);
FH_SINT32 API_ISP_GetRawCopy(/* params */);

/* ---- Debug ---- */
FH_SINT32 API_ISP_Dump_Param(/* params */);
FH_SINT32 API_ISP_GetDebugInfo(/* params */);
FH_SINT32 API_ISP_GetDebugDumpSize(/* params */);
FH_SINT32 API_ISP_GetIspReg(/* params */);
FH_SINT32 API_ISP_GetSensorReg(/* params */);

/* ---- Algorithm Control ---- */
FH_SINT32 API_ISP_SetAlgCtrl(/* params */);
FH_SINT32 API_ISP_GetAlgCtrl(/* params */);
FH_SINT32 API_ISP_AlgorithmReg(/* params */);
FH_SINT32 API_ISP_AlgorithmUnReg(/* params */);

/* ---- VI (Video Input) ---- */
FH_SINT32 API_ISP_SetViAttr(/* params */);    /* inferred */
FH_SINT32 API_ISP_GetViAttr(/* params */);
FH_SINT32 API_ISP_GetVIState(/* params */);
FH_SINT32 API_ISP_DetectPicSize(/* params */);

/* ---- Callbacks ---- */
FH_SINT32 API_ISP_RegisterPicStartCallback(/* callback */);
FH_SINT32 API_ISP_RegisterPicEndCallback(/* callback */);
FH_SINT32 API_ISP_RegisterIspInitCfgCallback(/* callback */);

/* ---- Circular Buffer ---- */
FH_SINT32 API_ISP_EnableCircularErrStopFlow(void);
FH_SINT32 API_ISP_DisableCircularErrStopFlow(void);
FH_SINT32 API_ISP_CLEAN_CIRCLUAR_BUFF(void);

/* ---- Misc ---- */
FH_SINT32 API_ISP_CisClkEn(/* params */);
FH_SINT32 API_ISP_GetBinAddr(/* params */);
FH_SINT32 API_ISP_GetBuffSize(/* params */);
FH_SINT32 API_ISP_Get_HWmodule_cfg(/* params */);
FH_SINT32 API_ISP_Get_Determined_HWmodule(/* params */);
FH_SINT32 API_ISP_ImportMallocedMem(/* params */);
FH_SINT32 API_ISP_ExportMallocedMem(/* params */);
FH_SINT32 API_ISP_ReadMallocedMem(/* params */);
FH_SINT32 API_ISP_GetMallocedMemBase(/* params */);

/* ---- Version ---- */
FH_SINT32 FH_ISP_Version(/* params */);
FH_SINT32 FH_ISPCORE_Version(/* params */);

#endif /* __FH_ISP_H__ */
