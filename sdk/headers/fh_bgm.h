/**
 * Fullhan FH8852V201 SDK — Background Model (BGM) / Motion Detection
 * Reverse-engineered from ipcam binary via Ghidra
 *
 * BGM provides hardware-accelerated background subtraction for motion detection.
 * Connected to VPSS via FH_SYS_BindVpu2Bgm().
 *
 * The BGM module maintains a background model and detects foreground changes.
 * Results are used by FHAdv_MD_* (motion detection) and FHAdv_CD_* (tamper detection).
 *
 * All functions take group=0 internally (single BGM instance).
 */

#ifndef __FH_BGM_H__
#define __FH_BGM_H__

#include "fh_common.h"

/* ---- Core API ---- */

/**
 * Query BGM memory requirements.
 */
FH_SINT32 FH_BGM_QueryMem(/* params */);

/**
 * Initialize BGM memory.
 */
FH_SINT32 FH_BGM_InitMem(/* params */);

/**
 * Set BGM capability/mode.
 */
FH_SINT32 FH_BGM_SetCapality(/* params */);

/**
 * Set BGM configuration.
 * Internally calls fh_bgm_drv_setconfig(0, config).
 */
FH_SINT32 FH_BGM_SetConfig(/* config */);

/**
 * Get BGM configuration.
 */
FH_SINT32 FH_BGM_GetConfig(/* config */);

/**
 * Enable BGM processing.
 * Internally calls fh_bgm_drv_enable(0).
 */
FH_SINT32 FH_BGM_Enable(void);

/**
 * Disable BGM processing.
 */
FH_SINT32 FH_BGM_Disable(void);

/**
 * Submit a video frame for background modeling.
 * Frame comes from VPSS via the VPU→BGM binding.
 * Internally calls fh_bgm_drv_submitframe(0, frame).
 */
FH_SINT32 FH_BGM_SubmitFrame(/* frame */);

/**
 * Reinitialize the background model (e.g. after scene change).
 */
FH_SINT32 FH_BGM_BkgReinit(void);

/* ---- Status / Results ---- */

/**
 * Get hardware motion detection status.
 * Returns per-macroblock motion flags.
 */
FH_SINT32 FH_BGM_GetHWStatus(/* status */);

/**
 * Get extended hardware status.
 */
FH_SINT32 FH_BGM_GetHWStatus_EXT(/* status */);

/**
 * Get software motion detection status.
 * Post-processed results with filtering applied.
 */
FH_SINT32 FH_BGM_GetSWStatus(/* status */);

/**
 * Get extended software status.
 */
FH_SINT32 FH_BGM_GetSWStatus_EXT(/* status */);

/**
 * Get run-time detection table/rect.
 */
FH_SINT32 FH_BGM_GetRunTableRect(/* params */);

/* ---- Advanced BGM Configuration (FH_BGM_ADV_*) ---- */

/**
 * Set/get background initialization attributes.
 * Controls how quickly a new background model is learned.
 */
FH_SINT32 FH_BGM_ADV_SetBkgInitAttr(/* params */);
FH_SINT32 FH_BGM_ADV_GetBkgInitAttr(/* params */);

/**
 * Set background init frame count.
 * Number of frames to build initial background model.
 */
FH_SINT32 FH_BGM_ADV_SetBkgInitCount(FH_UINT32 count);

/**
 * Set/get single Gaussian model attributes.
 */
FH_SINT32 FH_BGM_ADV_SetSigGauAttr(/* params */);
FH_SINT32 FH_BGM_ADV_GetSigGauAttr(/* params */);

/**
 * Set update count for single Gaussian model.
 */
FH_SINT32 FH_BGM_ADV_SetUpdateSigGauCount(FH_UINT32 count);

/**
 * Set/get multi-Gaussian (GMM) model attributes.
 */
FH_SINT32 FH_BGM_ADV_SetMulGauAttr(/* params */);
FH_SINT32 FH_BGM_ADV_GetMulGauAttr(/* params */);

/**
 * Set/get discrimination attributes.
 * Controls foreground/background classification thresholds.
 */
FH_SINT32 FH_BGM_ADV_SetDisAttr(/* params */);
FH_SINT32 FH_BGM_ADV_GetDisAttr(/* params */);

/**
 * Set/get contrast expansion attributes.
 */
FH_SINT32 FH_BGM_ADV_SetConExpaAttr(/* params */);
FH_SINT32 FH_BGM_ADV_GetConExpaAttr(/* params */);

/**
 * Set/get frame difference attributes.
 */
FH_SINT32 FH_BGM_ADV_SetFrmDiffAttr(/* params */);
FH_SINT32 FH_BGM_ADV_GetFrmDiffAttr(/* params */);

/**
 * Set/get frame difference consistency attributes.
 */
FH_SINT32 FH_BGM_ADV_SetFrmDiffConsistAttr(/* params */);
FH_SINT32 FH_BGM_ADV_GetFrmDiffConsistAttr(/* params */);

/**
 * Set/get global background luminance attributes.
 * Used for scene change detection.
 */
FH_SINT32 FH_BGM_ADV_SetGlbBgLumaAttr(/* params */);
FH_SINT32 FH_BGM_ADV_GetGlbBgLumaAttr(/* params */);

/**
 * Get hardware average processing time (performance metric).
 */
FH_SINT32 FH_BGM_ADV_GetHwAvgTime(/* time_us */);

/* ---- Internal Driver Functions ---- */

/* These are called by the above API functions with group=0 */
FH_SINT32 fh_bgm_init(void);
FH_SINT32 fh_bgm_close(void);
FH_SINT32 fh_bgm_drv_init(/* params */);
FH_SINT32 fh_bgm_drv_close(/* params */);
FH_SINT32 fh_bgm_drv_enable(FH_UINT32 group);
FH_SINT32 fh_bgm_drv_disable(FH_UINT32 group);
FH_SINT32 fh_bgm_drv_setconfig(FH_UINT32 group, /* config */);
FH_SINT32 fh_bgm_drv_getconfig(FH_UINT32 group, /* config */);
FH_SINT32 fh_bgm_drv_submitframe(FH_UINT32 group, /* frame */);
FH_SINT32 fh_bgm_drv_bkgreinit(FH_UINT32 group);
FH_SINT32 fh_bgm_drv_gethwstatus(FH_UINT32 group, /* status */);
FH_SINT32 fh_bgm_drv_getswstatus(FH_UINT32 group, /* status */);
FH_SINT32 fh_bgm_drv_initmem(/* params */);
FH_SINT32 fh_bgm_drv_querymem(/* params */);
FH_SINT32 fh_bgm_drv_setcapality(/* params */);
FH_SINT32 fh_bgm_drv_gethwavgtime(/* params */);
FH_SINT32 fh_bgm_drv_setgopth(/* params */);
FH_SINT32 fh_bgm_drv_getgopth(/* params */);
FH_SINT32 fh_bgm_drv_adv_set_update_sgau_cnt(/* params */);

/* Wrappers */
FH_SINT32 fh_bgm_setgopth(/* params */);
FH_SINT32 fh_bgm_getgopth(/* params */);

#endif /* __FH_BGM_H__ */
