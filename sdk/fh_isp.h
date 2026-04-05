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
