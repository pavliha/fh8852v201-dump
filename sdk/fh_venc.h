/**
 * Fullhan FH8852V201 SDK — Video Encoder (VENC)
 * Reverse-engineered from ipcam binary via Ghidra
 *
 * Supports H.264, H.265/HEVC, JPEG, MJPEG encoding.
 * Max 9 encode channels (0-8).
 * Stream buffer: 1.5MB (stm_1572864 from _media_driver_config)
 *
 * Typical channel layout:
 *   Channel 0: H.265 main stream (1920x1080@25fps)
 *   Channel 1: H.265 sub stream (640x360@15fps)
 *   Channel 2-8: JPEG snapshots, additional streams
 */

#ifndef __FH_VENC_H__
#define __FH_VENC_H__

#include "fh_common.h"

/* ---- Structures (inferred from decompilation) ---- */

/**
 * Channel attributes.
 * param_2[0] = codec type (FH_VENC_TYPE_*)
 * param_2[1] = width
 * param_2[2] = height
 */
typedef struct {
    FH_UINT32 type;         /* Codec type bitmask (FH_VENC_TYPE_*) */
    FH_UINT32 width;        /* Encode width */
    FH_UINT32 height;       /* Encode height */
} FH_VENC_CHN_ATTR;

/**
 * Encoded stream NAL unit.
 * Array of these in FH_VENC_STREAM, count = nalu_count.
 * From _VENC_HandleStream: 3 uint32s per NAL, packed at param_2[8..].
 */
typedef struct {
    FH_UINT32 addr;         /* [+0x00] Physical/virtual address of NAL data */
    FH_UINT32 type;         /* [+0x04] NAL unit type (SPS/PPS/IDR/P-frame etc.) */
    FH_UINT32 length;       /* [+0x08] NAL unit size in bytes */
} FH_VENC_NALU;

/**
 * Encoded stream output.
 * Populated by fh_venc_handle_stream → _VENC_HandleStream / _JPEG_HandleStream.
 * Retrieved via ioctl 0xC1A04D05 on /dev/media_process.
 *
 * Layout decompiled from _VENC_HandleStream (H.264/H.265):
 *   [0]  type        — codec type (4=H.264, 8=H.265)
 *   [1]  chn         — channel index
 *   [2]  total_size  — total encoded data size
 *   [3]  frame_type  — I-frame / P-frame / B-frame
 *   [4]  pts_lo      — presentation timestamp (low 32 bits)
 *   [5]  dts_lo      — decode timestamp (low 32 bits)
 *   [6]  dts_hi      — decode timestamp (high 32 bits)
 *   [7]  nalu_count  — number of NAL units
 *   [8..] nalus      — array of FH_VENC_NALU (3 uint32s each × nalu_count)
 *   [0x44] seq_num   — sequence number
 *   [0x45..0x4B]     — encoder statistics (QP, bitrate, etc.)
 *   [0x4C..0x4D]     — framerate (as double, from hw tick / clock)
 *   [0x4E] width     — encoded width
 *   [0x4F] height    — encoded height
 *
 * Layout for JPEG (_JPEG_HandleStream):
 *   [0]  type        — 1=JPEG snapshot, 2=MJPEG
 *   [1]  chn         — channel index
 *   [2]  data_addr   — JPEG data address
 *   [3]  data_size   — JPEG data size in bytes
 *   [4]  pts_lo      — presentation timestamp
 *   [5]  width       — image width
 *   [6]  height      — image height
 */
typedef struct {
    FH_UINT32 type;         /* [0x00] Codec: 1=JPEG, 2=MJPEG, 4=H.264, 8=H.265 */
    FH_UINT32 chn;          /* [0x04] Channel index */
    FH_UINT32 total_size;   /* [0x08] Total encoded data size (VENC) / data addr (JPEG) */
    FH_UINT32 frame_type;   /* [0x0C] Frame type (VENC) / data size (JPEG) */
    FH_UINT32 pts_lo;       /* [0x10] PTS low 32 bits */
    FH_UINT32 dts_lo;       /* [0x14] DTS low (VENC) / width (JPEG) */
    FH_UINT32 dts_hi;       /* [0x18] DTS high (VENC) / height (JPEG) */
    FH_UINT32 nalu_count;   /* [0x1C] Number of NAL units (H.264/H.265 only) */
    FH_VENC_NALU nalus[18]; /* [0x20] NAL unit array (max 18, 3×uint32 each) */
    FH_UINT32 seq_num;      /* [0x110] Sequence number */
    FH_UINT32 stat[6];      /* [0x114] Encoder stats (QP, bitrate, etc.) */
    double    framerate;    /* [0x130] Measured frame rate */
    FH_UINT32 enc_width;    /* [0x138] Encoded width */
    FH_UINT32 enc_height;   /* [0x13C] Encoded height */
} FH_VENC_STREAM;

/* IOCTL for getting stream from /dev/media_process */
#define VENC_IOCTL_GET_STREAM   0xC1A04D05  /* _fh_sys_get_stream */

/* ---- API Functions ---- */

/**
 * Initialize encoder system memory.
 * Must be called before creating channels.
 */
FH_SINT32 FH_VENC_SysInitMem(/* params */);

/**
 * Create an encode channel.
 * @param chn   Channel index 0-8
 * @param attr  Channel attributes (codec type, resolution)
 * @return 0 on success
 *
 * Codec type determines encoder:
 *   0x01 = JPEG snapshot → _JPEG_CreateChn(snapshot=0)
 *   0x02 = MJPEG stream → _JPEG_CreateChn(snapshot=1)
 *   0x04 = H.264 → _VENC_CreateChn(hevc=0)
 *   0x08 = H.265 → _VENC_CreateChn(hevc=1)
 *   0x10 = H.264 HP → _VENC_CreateChn(hevc=0)
 *   0x20 = H.265 Main10 → _VENC_CreateChn(hevc=1)
 */
FH_SINT32 FH_VENC_CreateChn(FH_UINT32 chn, const FH_VENC_CHN_ATTR *attr);

/**
 * Set channel attributes (change codec/resolution on existing channel).
 * If codec type changes, old encoder is destroyed and new one created.
 */
FH_SINT32 FH_VENC_SetChnAttr(FH_UINT32 chn, const FH_VENC_CHN_ATTR *attr);
FH_SINT32 FH_VENC_GetChnAttr(FH_UINT32 chn, FH_VENC_CHN_ATTR *attr);

/**
 * Start receiving pictures from VPSS for encoding.
 * Calls the encoder's start callback via function pointer table.
 */
FH_SINT32 FH_VENC_StartRecvPic(FH_UINT32 chn);
FH_SINT32 FH_VENC_StopRecvPic(FH_UINT32 chn);

/**
 * Get an encoded stream (non-blocking).
 * Internally calls _fh_sys_get_stream() then fh_venc_handle_stream().
 *
 * @param chn     Channel index
 * @param stream  Output stream data
 */
FH_SINT32 FH_VENC_GetStream(FH_UINT32 chn, FH_VENC_STREAM *stream);

/**
 * Get an encoded stream (blocking — waits for next frame).
 */
FH_SINT32 FH_VENC_GetStream_Block(FH_UINT32 chn, FH_VENC_STREAM *stream);

/**
 * Release stream buffer after processing.
 * Must be called after FH_VENC_GetStream/GetStream_Block.
 */
FH_SINT32 FH_VENC_ReleaseStream(FH_UINT32 chn);

/**
 * Request an IDR (keyframe) on next encode.
 */
FH_SINT32 FH_VENC_RequestIDR(FH_UINT32 chn);

/**
 * Submit a frame for encoding (manual mode).
 */
FH_SINT32 FH_VENC_Submit_ENC(FH_UINT32 chn, /* frame */);
FH_SINT32 FH_VENC_Submit_ENC_Ex(FH_UINT32 chn, /* frame */);

/**
 * Get current presentation timestamp.
 */
FH_UINT32 FH_VENC_GetCurPts(FH_UINT32 chn);

/**
 * Get channel encoding status.
 */
FH_SINT32 FH_VENC_GetChnStatus(FH_UINT32 chn, /* status */);

/* ---- VENC IOCTL codes ---- */
#define VENC_IOCTL_CREATE_CHN   0xC01C5403  /* _VENC_CreateChn */
#define VENC_IOCTL_SET_RC       0xC06C540C  /* _VENC_SetRCAttr */

/* ---- Rate Control Types (from _VENC_SetRCAttr switch) ---- */
#define FH_RC_CBR_H264      3   /* CBR for H.264 */
#define FH_RC_VBR_H264      4   /* VBR for H.264 */
#define FH_RC_FIXQP_H264    5   /* Fixed QP for H.264 */
#define FH_RC_AVBR_H264     6   /* AVBR for H.264 */
#define FH_RC_CBR_H265      7   /* CBR for H.265 */
#define FH_RC_VBR_H265      8   /* VBR for H.265 */
#define FH_RC_FIXQP_H265    9   /* Fixed QP for H.265 */
#define FH_RC_AVBR_H265     10  /* AVBR for H.265 */
#define FH_RC_CVBR_H264     11  /* CVBR for H.264 */
#define FH_RC_CVBR_H265     12  /* CVBR for H.265 */
#define FH_RC_QVBR_H264     13  /* QVBR for H.264 */
#define FH_RC_QVBR_H265     14  /* QVBR for H.265 */

/**
 * Rate control attributes.
 * Decompiled from _VENC_SetRCAttr — the struct layout varies by RC type,
 * but all types share the same first fields and are converted to a common
 * internal format (27 uint32s) before ioctl.
 *
 * Common CBR fields (type 3/7):
 *   [0] type          — RC type (FH_RC_*)
 *   [1] bitrate       — Target bitrate in kbps
 *   [2] max_bitrate   — Max bitrate (VBR) or same as bitrate (CBR)
 *   [3] gop           — GOP size (I-frame interval)
 *   [4] max_qp        — Maximum QP (default 200 for CBR)
 *   [5] min_qp        — Minimum QP (default 0)
 *   [6] qp_delta      — QP delta (default -2 for CBR)
 *   [7] stat_time     — Statistics time window
 *   [8] max_i_size    — Max I-frame size multiplier
 *   [9] reserved      — Reserved
 *
 * VBR adds (type 4/8):
 *   [3] max_rate_percent  — Max rate percent
 *   [7] i_qp_delta       — I-frame QP delta
 *   [8] change_pos        — Quality change position
 *   [9] min_i_ratio       — Min I-frame ratio
 *
 * AVBR adds (type 6/10):
 *   [3..6] qp_range      — QP range per frame type
 *   [11..13] adv_params   — Advanced VBR tuning
 *   [14] min_qp_i         — Min QP for I-frames
 *   [15] max_qp_i         — Max QP for I-frames
 *
 * FixedQP (type 5/9):
 *   [1] i_qp       — I-frame QP
 *   [2] p_qp       — P-frame QP
 *   [3] gop        — GOP size
 *
 * Internal ioctl struct (27 uint32s at 0xC06C540C):
 *   [0]  channel
 *   [1]  rc_mode (0=CBR_ext, 1=VBR, 2=FixQP, 4=AVBR, 5=CVBR, 6=QVBR)
 *   [2]  unused (0)
 *   [3]  bitrate
 *   [4]  max_bitrate
 *   [5]  min_bitrate
 *   [6]  max_i_bitrate
 *   [7]  framerate_num
 *   [8]  framerate_den
 *   [9]  gop
 *   [10] stat_time (8)
 *   [11] max_qp
 *   [12] min_qp
 *   [13] max_i_qp (default 0x1E=30)
 *   [14] min_i_qp (default 0x1E=30)
 *   [15] max_qp_delta (default 0x22=34)
 *   [16] change_pos (default 0x20=32)
 *   [17] qp_delta
 *   [18] min_qp_i_delta (default 0x1E=30)
 *   [19] min_still_bitrate (default 2000)
 *   [20] max_still_bitrate (default 4000)
 */
typedef struct {
    FH_UINT32 type;             /* [0] RC type (FH_RC_*) */
    FH_UINT32 bitrate;          /* [1] Target/max bitrate (kbps) */
    FH_UINT32 max_bitrate;      /* [2] Max bitrate (VBR) */
    FH_UINT32 gop;              /* [3] GOP size */
    FH_UINT32 field4;           /* [4] Varies by RC type */
    FH_UINT32 field5;           /* [5] Varies by RC type */
    FH_UINT32 field6;           /* [6] Varies by RC type */
    FH_UINT32 stat_time;        /* [7] Statistics window */
    FH_UINT32 max_qp;           /* [8] or field8 */
    FH_UINT32 min_qp;           /* [9] or field9 */
    FH_UINT32 qp_delta;         /* [10] */
    FH_UINT32 i_qp_delta;       /* [11] */
    FH_UINT32 change_pos;       /* [12] */
    FH_UINT32 min_i_ratio;      /* [13] */
    FH_UINT32 min_qp_i;         /* [14] AVBR/QVBR */
    FH_UINT32 max_qp_i;         /* [15] AVBR/QVBR */
    FH_UINT32 reserved[1];      /* [16] */
} FH_VENC_RC_ATTR;

/* ---- Rate Control ---- */

/**
 * Set rate control attributes.
 * Internally converts to 27-uint32 struct and calls ioctl 0xC06C540C.
 * @param chn      Channel 0-8
 * @param rc_attr  Rate control configuration
 */
FH_SINT32 FH_VENC_SetRCAttr(FH_UINT32 chn, const FH_VENC_RC_ATTR *rc_attr);
FH_SINT32 FH_VENC_GetRCAttr(FH_UINT32 chn, FH_VENC_RC_ATTR *rc_attr);

/**
 * Dynamic rate control parameter change.
 */
FH_SINT32 FH_VENC_SetRcChangeParam(FH_UINT32 chn, /* params */);

/**
 * Encoder parameters (QP, bitrate fine-tuning).
 */
FH_SINT32 FH_VENC_SetEncParam(FH_UINT32 chn, /* enc_param */);
FH_SINT32 FH_VENC_GetEncParam(FH_UINT32 chn, /* enc_param */);

/**
 * Encoder reference mode.
 */
FH_SINT32 FH_VENC_SetEncRefMode(FH_UINT32 chn, /* ref_mode */);
FH_SINT32 FH_VENC_GetEncRefMode(FH_UINT32 chn, /* ref_mode */);

/* ---- Rotation ---- */

/**
 * Set hardware rotation (0, 90, 180, 270 degrees).
 */
FH_SINT32 FH_VENC_SetRotate(FH_UINT32 chn, FH_UINT32 rotation);
FH_SINT32 FH_VENC_GetRotate(FH_UINT32 chn, FH_UINT32 *rotation);

/* ---- H.264 specific ---- */

FH_SINT32 FH_VENC_SetH264Entropy(FH_UINT32 chn, /* params */);
FH_SINT32 FH_VENC_GetH264Entropy(FH_UINT32 chn, /* params */);
FH_SINT32 FH_VENC_SetH264Dblk(FH_UINT32 chn, /* params */);
FH_SINT32 FH_VENC_GetH264Dblk(FH_UINT32 chn, /* params */);
FH_SINT32 FH_VENC_SetH264SliceSplit(FH_UINT32 chn, /* params */);
FH_SINT32 FH_VENC_GetH264SliceSplit(FH_UINT32 chn, /* params */);
FH_SINT32 FH_VENC_SetH264IntraFresh(FH_UINT32 chn, /* params */);
FH_SINT32 FH_VENC_GetH264IntraFresh(FH_UINT32 chn, /* params */);
FH_SINT32 FH_VENC_SetH264VUI(FH_UINT32 chn, /* params */);
FH_SINT32 FH_VENC_GetH264VUI(FH_UINT32 chn, /* params */);

/* ---- H.265 specific ---- */

FH_SINT32 FH_VENC_SetH265Entropy(FH_UINT32 chn, /* params */);
FH_SINT32 FH_VENC_GetH265Entropy(FH_UINT32 chn, /* params */);
FH_SINT32 FH_VENC_SetH265Dblk(FH_UINT32 chn, /* params */);
FH_SINT32 FH_VENC_GetH265Dblk(FH_UINT32 chn, /* params */);
FH_SINT32 FH_VENC_SetH265SliceSplit(FH_UINT32 chn, /* params */);
FH_SINT32 FH_VENC_GetH265SliceSplit(FH_UINT32 chn, /* params */);
FH_SINT32 FH_VENC_SetH265IntraFresh(FH_UINT32 chn, /* params */);
FH_SINT32 FH_VENC_GetH265IntraFresh(FH_UINT32 chn, /* params */);
FH_SINT32 FH_VENC_SetH265VUI(FH_UINT32 chn, /* params */);
FH_SINT32 FH_VENC_GetH265VUI(FH_UINT32 chn, /* params */);

/* ---- ROI (Region of Interest) ---- */

FH_SINT32 FH_VENC_SetRoiCfg(FH_UINT32 chn, /* roi_cfg */);
FH_SINT32 FH_VENC_ClearRoi(FH_UINT32 chn);

/* ---- Advanced ---- */

/**
 * Set background QP for static scene optimization.
 */
FH_SINT32 FH_VENC_SetBkgQP(FH_UINT32 chn, /* params */);
FH_SINT32 FH_VENC_GetBkgQP(FH_UINT32 chn, /* params */);

/**
 * De-breathing effect (reduces pulsing artifacts).
 */
FH_SINT32 FH_VENC_SetDeBreathEffect(FH_UINT32 chn, /* params */);
FH_SINT32 FH_VENC_GetDeBreathEffect(FH_UINT32 chn, /* params */);

/**
 * Lost frame attributes (frame drop behavior under load).
 */
FH_SINT32 FH_VENC_SetLostFrameAttr(FH_UINT32 chn, /* params */);
FH_SINT32 FH_VENC_GetLostFrameAttr(FH_UINT32 chn, /* params */);

/**
 * Insert user data (SEI NAL unit).
 */
FH_SINT32 FH_VENC_InsertUserData(FH_UINT32 chn, /* data, len */);

/**
 * Set stream encryption seed.
 */
FH_SINT32 FH_VENC_SetEncryptSeed(FH_UINT32 chn, /* seed */);

/**
 * Hardware average encoding time (performance metric).
 */
FH_SINT32 FH_VENC_GetHwAvgTime(FH_UINT32 chn, /* time_us */);

/**
 * Query memory requirements.
 */
FH_SINT32 FH_VENC_QuerySysMem(/* params */);
FH_SINT32 FH_VENC_QueryChnMem(FH_UINT32 chn, /* params */);

/**
 * Clear YUV input queue.
 */
FH_SINT32 FH_VENC_ClearYuvQueue(FH_UINT32 chn);

#endif /* __FH_VENC_H__ */
