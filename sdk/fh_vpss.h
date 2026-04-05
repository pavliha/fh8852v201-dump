/**
 * Fullhan FH8852V201 SDK — Video Processing Subsystem (VPSS)
 * Reverse-engineered from ipcam binary via Ghidra
 *
 * Device node: /dev/media_process (shared with FH_SYS)
 * IOCTL base: 0x69 ('i')
 *
 * VPSS takes raw sensor frames from the ISP and produces
 * scaled/cropped/rotated output for VENC, JPEG, BGM, and NNA.
 *
 * Max channels: 5 (0-4)
 * Typical usage:
 *   Channel 0: main stream (sensor resolution, e.g. 1920x1080)
 *   Channel 1: sub stream (half resolution, e.g. 960x540)
 *   Channel 2: NNA input (512x288, 1/5 frame rate)
 */

#ifndef __FH_VPSS_H__
#define __FH_VPSS_H__

#include "fh_common.h"

/* ---- IOCTL codes (from Ghidra decompilation) ---- */
#define VPSS_IOCTL_SET_VI_ATTR      0xC0F4696A  /* FH_VPSS_SetViAttr */
#define VPSS_IOCTL_ENABLE           0xC0046971  /* FH_VPSS_Enable */
#define VPSS_IOCTL_SET_CHN_ATTR     0xC00C696C  /* FH_VPSS_SetChnAttr */
#define VPSS_IOCTL_OPEN_CHN         0xC0046973  /* FH_VPSS_OpenChn */
#define VPSS_IOCTL_SET_VO_MODE      0xC00869A4  /* FH_VPSS_SetVOMode */
#define VPSS_IOCTL_SET_FRAMECTRL    0xC0086978  /* FH_VPSS_SetFramectrl */

/* ---- Structures (inferred from ioctl parameter sizes) ---- */

/**
 * Video input attributes — sensor input to VPSS.
 * Set via ioctl VPSS_IOCTL_SET_VI_ATTR.
 * Size: 8 bytes (2 x uint32)
 */
typedef struct {
    FH_UINT32 width;        /* Sensor output width (e.g. 1920) */
    FH_UINT32 height;       /* Sensor output height (e.g. 1080) */
} FH_VPSS_VI_ATTR;

/**
 * Channel attributes — output resolution per channel.
 * Set via ioctl VPSS_IOCTL_SET_CHN_ATTR.
 * ioctl param: {channel, width, height} = 12 bytes
 */
typedef struct {
    FH_UINT32 width;        /* Output width */
    FH_UINT32 height;       /* Output height */
} FH_VPSS_CHN_ATTR;

/**
 * Frame rate control — skip frames for NNA/sub channels.
 * e.g. {5, 1} = output 1 frame every 5 input frames
 */
typedef struct {
    FH_SINT16 src_rate;     /* Source frame rate divisor */
    FH_SINT16 dst_rate;     /* Destination frame rate multiplier */
} FH_VPSS_FRAMECTRL;

/**
 * Lens distortion correction attributes.
 * ioctl param includes: enable, center_x, center_y, strength
 */
typedef struct {
    FH_SINT32 enable;       /* 0=disable, 1=enable */
    FH_SINT32 center_x;    /* LDC center X (typically width/2) */
    FH_SINT32 center_y;    /* LDC center Y (typically height/2) */
    FH_SINT32 strength;    /* Correction strength (0-100, default 50) */
} FH_VPSS_LDC_ATTR;

/**
 * Video frame from VPSS channel.
 * Retrieved via ioctl VPSS_IOCTL_GET_FRAME (0xC0686970).
 *
 * Decompiled from FH_VPSS_GetChnFrame_Ex:
 *   ioctl returns raw buffer, then fields are mapped to output struct:
 *     param_2[0]  = local_118[2]   → phys_addr (Y plane physical address)
 *     param_2[1]  = local_104      → format (pixel format)
 *     param_2[2]  = local_118[3]   → width
 *     param_2[3]  = local_108      → height
 *     param_2[4]  = local_100      → stride_y (Y plane stride)
 *     param_2[5]  = uStack_fc      → stride_c (C plane stride)
 *     param_2[6]  = uStack_f8      → size (total frame size)
 *     param_2[7]  = local_f4       → phys_addr_c (C plane physical address)
 *     param_2[8]  = local_f0       → virt_addr (Y plane virtual address)
 *     param_2[9]  = local_ec       → virt_addr_c (C plane virtual address)
 *     param_2[10] = local_d8       → pts_lo (timestamp low)
 *     param_2[11] = local_d4       → pts_hi (timestamp high)
 */
typedef struct {
    FH_UINT32 phys_addr;    /* [0]  Y plane physical address */
    FH_UINT32 format;       /* [1]  Pixel format */
    FH_UINT32 width;        /* [2]  Frame width */
    FH_UINT32 height;       /* [3]  Frame height */
    FH_UINT32 stride_y;     /* [4]  Y plane stride (bytes per line) */
    FH_UINT32 stride_c;     /* [5]  C plane stride */
    FH_UINT32 size;         /* [6]  Total frame data size */
    FH_UINT32 phys_addr_c;  /* [7]  C plane physical address */
    FH_UINT32 virt_addr;    /* [8]  Y plane virtual address (mmap'd) */
    FH_UINT32 virt_addr_c;  /* [9]  C plane virtual address */
    FH_UINT32 pts_lo;       /* [10] Presentation timestamp (low 32) */
    FH_UINT32 pts_hi;       /* [11] Presentation timestamp (high 32) */
} FH_VPSS_FRAME;

#define VPSS_IOCTL_GET_FRAME    0xC0686970  /* FH_VPSS_GetChnFrame_Ex */

/* ---- API Functions ---- */

/**
 * Set video input attributes (sensor resolution).
 * Must be called before FH_VPSS_Enable.
 *
 * @param attr  Pointer to VI attributes {width, height}
 * @return 0 on success
 *
 * Observed usage: FH_VPSS_SetViAttr({1920, 1080})
 */
FH_SINT32 FH_VPSS_SetViAttr(const FH_VPSS_VI_ATTR *attr);
FH_SINT32 FH_VPSS_GetViAttr(FH_VPSS_VI_ATTR *attr);

/**
 * Enable VPSS group.
 * @param group  Group index (typically 0)
 */
FH_SINT32 FH_VPSS_Enable(FH_UINT32 group);
FH_SINT32 FH_VPSS_Disable(FH_UINT32 group);

/**
 * Set channel output resolution.
 * @param chn   Channel 0-4
 * @param attr  Output resolution
 */
FH_SINT32 FH_VPSS_SetChnAttr(FH_UINT32 chn, const FH_VPSS_CHN_ATTR *attr);
FH_SINT32 FH_VPSS_GetChnAttr(FH_UINT32 chn, FH_VPSS_CHN_ATTR *attr);

/**
 * Open/close a processing channel.
 */
FH_SINT32 FH_VPSS_OpenChn(FH_UINT32 chn);
FH_SINT32 FH_VPSS_CloseChn(FH_UINT32 chn);

/**
 * Set video output mode.
 * @param chn   Channel 0-4
 * @param mode  1=normal, 5=NNA downscaled
 */
FH_SINT32 FH_VPSS_SetVOMode(FH_UINT32 chn, FH_UINT32 mode);

/**
 * Set frame rate control (frame skipping).
 * @param chn   Channel 0-4
 * @param ctrl  {src_rate, dst_rate} e.g. {5, 1} = 1/5 fps
 */
FH_SINT32 FH_VPSS_SetFramectrl(FH_UINT32 chn, const FH_VPSS_FRAMECTRL *ctrl);
FH_SINT32 FH_VPSS_GetFramectrl(FH_UINT32 chn, FH_VPSS_FRAMECTRL *ctrl);

/**
 * Set lens distortion correction.
 */
FH_SINT32 FH_VPSS_SetLDCAttr(const FH_VPSS_LDC_ATTR *attr);
FH_SINT32 FH_VPSS_GetLDCAttr(FH_VPSS_LDC_ATTR *attr);

/**
 * Freeze/unfreeze video output (displays last frame).
 */
FH_SINT32 FH_VPSS_FreezeVideo(FH_UINT32 chn);
FH_SINT32 FH_VPSS_UnfreezeVideo(FH_UINT32 chn);

/**
 * Set crop region.
 */
FH_SINT32 FH_VPSS_SetCrop(/* crop_attr */);
FH_SINT32 FH_VPSS_GetCrop(/* crop_attr */);

/**
 * Set rotation (0, 90, 180, 270).
 */
FH_SINT32 FH_VPSS_SetRotate(FH_UINT32 chn, FH_UINT32 rotation);
FH_SINT32 FH_VPSS_SetVORotate(FH_UINT32 chn, FH_UINT32 rotation);

/**
 * Get a processed frame from channel.
 */
FH_SINT32 FH_VPSS_GetChnFrame(FH_UINT32 chn, /* frame_info */);
FH_SINT32 FH_VPSS_GetChnFrame_Ex(FH_UINT32 chn, /* frame_info */);
FH_SINT32 FH_VPSS_GetChnFrameAdv(FH_UINT32 chn, /* frame_info */);

/**
 * Get BGM (background model) data for motion detection.
 */
FH_SINT32 FH_VPSS_GetBGMData(/* bgm_data */);

/**
 * OSD overlay (text/logo).
 * Note: FH_VPSS_SetOsd returns 0xA0094007 (stub — use FHAdv_Osd_* instead)
 */
FH_SINT32 FH_VPSS_SetOsd(/* osd_attr */);  /* STUB */
FH_SINT32 FH_VPSS_GetOsd(/* osd_attr */);
FH_SINT32 FH_VPSS_OpenOsdtext(/* params */);
FH_SINT32 FH_VPSS_CloseOsdtext(/* params */);

/**
 * Privacy mask regions.
 */
FH_SINT32 FH_VPSS_SetMask(/* mask_attr */);
FH_SINT32 FH_VPSS_GetMask(/* mask_attr */);
FH_SINT32 FH_VPSS_ClearMask(/* params */);

/**
 * Memory management.
 */
FH_SINT32 FH_VPSS_SysInitMem(/* params */);
FH_SINT32 FH_VPSS_ChnInitMem(/* params */);
FH_SINT32 FH_VPSS_QuerySysMem(/* params */);
FH_SINT32 FH_VPSS_QueryChnMem(/* params */);

/**
 * Send a user picture (e.g. test pattern).
 */
FH_SINT32 FH_VPSS_SendUserPic(/* params */);
FH_SINT32 FH_VPSS_GetUserPicAddr(/* params */);

/**
 * Reset VPSS.
 */
FH_SINT32 FH_VPSS_Reset(void);

/**
 * Low latency mode for real-time streaming.
 */
FH_SINT32 FH_VPSS_LOW_LATENCY_Enable(void);
FH_SINT32 FH_VPSS_LOW_LATENCY_Disable(void);

/**
 * Y/C mean statistics (for auto-exposure).
 */
FH_SINT32 FH_VPSS_EnableYCmean(void);
FH_SINT32 FH_VPSS_DisableYCmean(void);
FH_SINT32 FH_VPSS_SetYCmeanMode(/* params */);
FH_SINT32 FH_VPSS_GetYCmean(/* params */);

/**
 * SAD (Sum of Absolute Differences) for motion estimation.
 */
FH_SINT32 FH_VPSS_EnableSad(void);
FH_SINT32 FH_VPSS_DisableSad(void);

/**
 * APC (Automatic Picture Control) attributes.
 */
FH_SINT32 FH_VPSS_SetChnApcAttr(/* params */);
FH_SINT32 FH_VPSS_GetChnApcAttr(/* params */);

/**
 * Graph configuration (video processing graph).
 */
FH_SINT32 FH_VPSS_SetGraph(/* params */);
FH_SINT32 FH_VPSS_GetGraph(/* params */);
FH_SINT32 FH_VPSS_SetGlbGraphV2(/* params */);
FH_SINT32 FH_VPSS_GetGlbGraphV2(/* params */);
FH_SINT32 FH_VPSS_SetChnGraphV2(/* params */);
FH_SINT32 FH_VPSS_GetChnGraphV2(/* params */);

/**
 * Frame rate query.
 */
FH_SINT32 FH_VPSS_GetFrameRate(/* params */);

/**
 * Hardware average time (performance metrics).
 */
FH_SINT32 FH_VPSS_GetHwAvgTime(/* params */);

/**
 * Channel capability query.
 */
FH_SINT32 FH_VPSS_GetChnCapality(/* params */);

/**
 * Scaler coefficient (custom downscale filter).
 */
FH_SINT32 FH_VPSS_SetScalerCoeff(/* params */);
FH_SINT32 FH_VPSS_SetDefaultScalerSize(/* params */);

/**
 * RGB pre-processing attributes.
 */
FH_SINT32 FH_VPSS_SetRGBPreAttr(/* params */);
FH_SINT32 FH_VPSS_GetRGBPreAttr(/* params */);

/**
 * Channel video input selection (for dual-sensor).
 */
FH_SINT32 FH_VPSS_SetChnViSel(/* params */);
FH_SINT32 FH_VPSS_SetChn2LogoSel(/* params */);

#endif /* __FH_VPSS_H__ */
