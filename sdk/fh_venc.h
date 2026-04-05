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

/* ---- Rate Control ---- */

/**
 * Set rate control attributes (CBR/VBR, bitrate, GOP, etc.).
 */
FH_SINT32 FH_VENC_SetRCAttr(FH_UINT32 chn, /* rc_attr */);
FH_SINT32 FH_VENC_GetRCAttr(FH_UINT32 chn, /* rc_attr */);

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
