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

/* ================================================================
 * Structs validated from live memory dumps (runtime verification)
 * Dumped from /proc/234/mem of running ipcam process
 * ================================================================ */

/**
 * Account entry (validated from live memory at 0x660408).
 * 10 entries, 0x48 bytes each.
 */
typedef struct {
    char      name[0x20];    /* [0x00] Username, null-terminated */
    char      password[0x20];/* [0x20] Password in PLAINTEXT, null-terminated */
    FH_UINT32 type;          /* [0x40] Account type (0 = admin) */
    FH_UINT32 power;         /* [0x44] Permission flags (0xFFFFFFFF = full access) */
} FH_ACCOUNT;

/**
 * Device information struct (validated from live memory at 0x658800).
 * This is the struct returned by mod=device&cmd=get.
 * Total size: ~544 bytes.
 */
typedef struct {
    FH_UINT32 unknown0;      /* [  0] */
    FH_UINT8  pad1[12];      /* [  4] */
    char      devtype[32];   /* [ 16] Device type: "H43" */
    char      uboot_ver[48]; /* [ 48] U-Boot version: "uboot-2016-14" */
    char      kernel_ver[48];/* [ 96] Kernel version: "linux-4.9-15" */
    char      version[48];   /* [144] Firmware version: "V1.14.48-20240903" */
    char      devno[64];     /* [192] P2P device number: "PPXCAO0AA55FD6CC15" */
    char      serial[32];    /* [256] Serial number: "989FEA22D1F1F9AF" */
    char      nickname[64];  /* [288] Device nickname: "H43" */
    char      chn_name[64];  /* [352] Channel name: "Channel0" */
    char      manufacturer[32]; /* [416] Manufacturer: "Vatilon" */
    char      location[32];  /* [448] Location: "ShenZhen" */
    FH_UINT32 language;      /* [480] Language: 1 */
    FH_UINT32 timezone;      /* [484] Timezone: 27 */
    FH_UINT32 voice;         /* [488] Voice prompt type: 2 */
    FH_UINT32 chn_num;       /* [492] Number of channels: 1 */
    FH_UINT32 reserved;      /* [496] */
    FH_UINT32 system_function; /* [500] Feature bitmask: 0x3ABCB (240587) */
    FH_UINT32 home_ipc;      /* [504] Home IPC flag: 1 */
    FH_UINT32 stream_num;    /* [508] Streams per channel: 2 */
    char      cpu_type[32];  /* [512] SoC name: "fh8852v201" */
} FH_DEVICE_INFO;

/**
 * Hardware info struct (hwinfo).
 * Referenced as `hwinfo[]` throughout the binary.
 * Populated from /usr/app/product, /tmp/etc/config/hw.conf,
 * license data, and sensor probing.
 *
 * Decompiled field-by-field from:
 *   m_platform_sync_product_info (offsets 136-144)
 *   sysctrl_read_hw_config (offsets 295-356)
 *   sysctrl_read_double_sensor_config (offset 373)
 *   p_image_get_sensor_id (offset 220)
 *   _media_driver_config / FUN_00295168 (offsets 216, 228-256)
 *   cloud_p2p_start (offset 212)
 *   p_platform_gpio_init (offsets 288, 300, 332, 336, 373)
 *   m_osd_init (offset 348)
 *
 * Size: ~380 bytes (0x17C)
 */
typedef struct {
    FH_UINT8  pad0[136];             /* [0x00-0x87] Unknown/reserved */

    /* From /usr/app/product file */
    FH_UINT32 uboot_version_num;     /* [0x88] Product uboot version number */
    FH_UINT32 kernel_version_num;    /* [0x8C] Product kernel version number */
    FH_UINT32 rootfs_version_num;    /* [0x90] Product rootfs/appfs version number */

    FH_UINT8  pad1[64];             /* [0x94-0xD3] Unknown */

    /* From license/authentication */
    FH_UINT32 p2p_type;             /* [0xD4] P2P cloud type (1=Tuya,2=Dana,3=HuiYun,4=XiaoCao,
                                              5=QIPC,6/9=Closeli,7=IVY,8=Aliyun,10=Halou) */

    /* Video configuration */
    FH_UINT32 chn_num;              /* [0xD8] Number of sensor channels (1 or 2) */
    FH_UINT32 sensor_type;          /* [0xDC] Sensor ID (1=imx307,3=gc2053,6=gc2093,
                                              7=gc2063,11=gc2083,20=cv2003) */
    FH_UINT8  pad2[4];             /* [0xE0] */
    FH_UINT32 isp_width;           /* [0xE4] ISP processing width */
    FH_UINT32 isp_height;          /* [0xE8] ISP processing height */
    FH_UINT8  pad3[8];             /* [0xEC] */
    FH_UINT32 vi_width;            /* [0xF4] Video input width (1920) */
    FH_UINT32 vi_height;           /* [0xF8] Video input height (1080) */
    FH_UINT32 cap0_width;          /* [0xFC] Capture channel 0 width */
    FH_UINT32 cap0_height;         /* [0x100] Capture channel 0 height */

    FH_UINT8  pad4[16];            /* [0x104-0x113] */
    FH_UINT32 msg_queue;           /* [0x114] System message queue handle */

    FH_UINT8  pad5[8];             /* [0x118-0x11F] */
    FH_UINT32 has_wifi;            /* [0x120] WiFi module present (0 or 1) */

    FH_UINT8  pad6[3];             /* [0x124-0x126] */

    /* From hw.conf — byte fields */
    FH_UINT8  af_protocol;         /* [0x127] Autofocus protocol type */
    FH_UINT8  ptz_h_convert;       /* [0x128] PTZ horizontal direction invert */
    FH_UINT8  ptz_v_convert;       /* [0x129] PTZ vertical direction invert */
    FH_UINT8  ir_lamp_level;       /* [0x12A] IR lamp polarity (inverts PWM if set) */
    FH_UINT8  white_lamp_level;    /* [0x12B] White lamp polarity */
    FH_UINT8  ircut_convert;       /* [0x12C] IR-cut filter direction invert */
    FH_UINT8  speaker_level;       /* [0x12D] Speaker polarity */
    FH_UINT8  photoresistor_type;  /* [0x12E] Photoresistor type (0=GPIO, 1=ADC) */
    FH_UINT8  photoresistor_level; /* [0x12F] Photoresistor polarity */
    FH_UINT8  hw_change_lamp_mode; /* [0x130] Hardware lamp mode change enable */

    FH_UINT8  pad7[3];             /* [0x131-0x133] */

    /* From hw.conf — uint32 fields */
    FH_UINT32 max_ptz_h_step;     /* [0x134] Max PTZ horizontal steps (e.g. 3150) */
    FH_UINT32 max_ptz_v_step;     /* [0x138] Max PTZ vertical steps (e.g. 1200) */
    FH_UINT32 ptz_h_speed;        /* [0x13C] PTZ horizontal speed (e.g. 2000000) */
    FH_UINT32 ptz_v_speed;        /* [0x140] PTZ vertical speed (e.g. 3000000) */
    FH_UINT32 ptz_move_volume;    /* [0x144] PTZ movement audio volume (e.g. 80) */
    FH_UINT32 ptz_track_mode;     /* [0x148] PTZ tracking mode */
    FH_UINT32 custom_io_func1;    /* [0x14C] IR lamp GPIO type (1=GPIO,2=PWM,3=direct,4=ext) */
    FH_UINT32 custom_io_func2;    /* [0x150] White lamp GPIO type */

    FH_UINT8  pad8[4];            /* [0x154-0x157] */

    /**
     * System function hardware bitmask (from hw.conf flags).
     * Built by ORing bits from individual hw.conf keys:
     *   bit 4  (0x10):    PTZ support (ptz_support=1)
     *   bit 6  (0x40):    GB28181 support
     *   bit 7  (0x80):    IR lamp present (custom_io_func1=0 or 3)
     *   bit 8  (0x100):   Dual lamp (double_lamp_support=1)
     *   bit 15 (0x8000):  Cloud support (cloud_support=1, requires p2p_type)
     *   bit 16 (0x10000): ONVIF image control (onvif_set_image_support=1)
     */
    FH_UINT32 sys_function_hw;    /* [0x158] Hardware feature bitmask */

    FH_UINT8  pad9[8];            /* [0x15C-0x163] */
    FH_UINT32 af_protocol_ext;    /* [0x164] af_protocol & 0xFF (extended) */

    FH_UINT8  pad10[13];          /* [0x168-0x174] */
    FH_UINT8  sensor_index;       /* [0x175] Dual sensor active index (0 or 1) */
} FH_HWINFO;

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
