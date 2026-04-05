/**
 * Fullhan FH8852V201 SDK — Common Types
 * Reverse-engineered from ipcam binary via Ghidra
 *
 * Device: KEVIEW H43 (P05H), Firmware V1.14.48-20240903
 * SoC: Fullhan FH8852V201, ARM Cortex-A7
 * Kernel: Linux 4.9.129
 */

#ifndef __FH_COMMON_H__
#define __FH_COMMON_H__

#include <stdint.h>

typedef int             FH_SINT32;
typedef unsigned int    FH_UINT32;
typedef short           FH_SINT16;
typedef unsigned short  FH_UINT16;
typedef char            FH_SINT8;
typedef unsigned char   FH_UINT8;
typedef void            FH_VOID;
typedef int             FH_BOOL;

#define FH_SUCCESS      0
#define FH_FAILURE      (-1)
#define FH_NULL         ((void *)0)
#define FH_TRUE         1
#define FH_FALSE        0

/* Max limits (from decompilation) */
#define FH_MAX_VENC_CHN     9   /* FH_VENC_CreateChn: param_1 < 9 */
#define FH_MAX_VPSS_CHN     5   /* FH_VPSS_SetChnAttr: param_1 < 5 */
#define FH_MAX_ACCOUNTS     10  /* m_account_check_auth: local_10 < 10 */
#define FH_ACCOUNT_SIZE     0x48 /* bytes per account entry */

/* Error codes (observed in decompilation) */
#define FH_ERR_VENC_BASE    0xA1024000
#define FH_ERR_VENC_PARAM   0xA1024003  /* invalid channel number */
#define FH_ERR_VENC_NULL    0xA102400F  /* null pointer */
#define FH_ERR_VENC_NOSUP   0xA1024007  /* not supported */
#define FH_ERR_VENC_NOATTR  0xA1024008  /* channel not configured */

#define FH_ERR_VPSS_BASE    0xA0094000
#define FH_ERR_VPSS_PARAM   0x5FF6BFFD  /* invalid parameter */
#define FH_ERR_VPSS_NULL    0x5FF6BFFA  /* null pointer */
#define FH_ERR_VPSS_NOSUP   0xA0094007  /* not supported / stub */

#define FH_ERR_AC_BASE      0x80130000
#define FH_ERR_AC_INIT      0x8013000D  /* audio init failed */

#define FH_ERR_NNA_BASE     0x5EFBC000
#define FH_ERR_NNA_NULL     0x5EFBBFFA  /* null pointer */
#define FH_ERR_NNA_PARAM    0x5EFBBFFD  /* invalid parameter */
#define FH_ERR_NNA_NOMEM    0x5EFBBFF5  /* allocation failed */

/* VENC codec types (from FH_VENC_SetChnAttr decompilation)
 * Stored in param_2[0], bit flags */
#define FH_VENC_TYPE_JPEG       0x01  /* JPEG snapshot */
#define FH_VENC_TYPE_MJPEG      0x02  /* Motion JPEG stream */
#define FH_VENC_TYPE_H264       0x04  /* H.264 main profile */
#define FH_VENC_TYPE_H265       0x08  /* H.265/HEVC main */
#define FH_VENC_TYPE_H264_HP    0x10  /* H.264 high profile */
#define FH_VENC_TYPE_H265_MAIN  0x20  /* H.265 main 10 */

/* Sensor format (from API_ISP_SetSensorFmt) */
#define FH_SENSOR_FMT_BAYER     0x203  /* Used for CV2003, GC2053, GC2063, GC2083 */

/* Sensor IDs (from p_image_get_sensor_id) */
#define FH_SENSOR_IMX307    1   /* imx307_mipi */
#define FH_SENSOR_GC2053    3   /* gc2053_mipi */
#define FH_SENSOR_GC2093    6   /* gc2093_mipi */
#define FH_SENSOR_GC2063    7   /* gc2063_mipi */
#define FH_SENSOR_GC2083    11  /* gc2083_mipi (0x0B) */
#define FH_SENSOR_CV2003    20  /* cv2003_mipi (0x14) */

/* ISP color mode (from FHAdv_Isp_SetColorMode / p_image_set_day_night_mode) */
#define FH_COLOR_MODE_BW    0   /* Black & white (night/IR) */
#define FH_COLOR_MODE_COLOR 1   /* Color (day) */

/* VPSS output mode (from FH_VPSS_SetVOMode) */
#define FH_VO_MODE_NORMAL   1   /* Normal output */
#define FH_VO_MODE_NNA      5   /* NNA input (downscaled) */

#endif /* __FH_COMMON_H__ */
