/**
 * Fullhan FH8852V201 SDK — Audio Codec (FH_AC)
 * Reverse-engineered from ipcam binary via Ghidra
 *
 * Device node: /dev/fh_audio (opened by FH_AC_Init)
 * Internal codec with mic input and speaker output.
 * Supports echo cancellation and noise reduction (algo param 0xEA).
 */

#ifndef __FH_AUDIO_H__
#define __FH_AUDIO_H__

#include "fh_common.h"

/* ---- Initialization ---- */

/**
 * Initialize internal audio codec.
 * Opens /dev/fh_audio, configures via ioctl:
 *   ioctl(fd, 0x40000000, 0)  — reset
 *   ioctl(fd, 0x20000000, &config) — configure
 * Then mmaps shared memory for audio buffers.
 *
 * @return 0 on success, 0x8013000D on failure
 */
FH_SINT32 FH_AC_Init(void);

/**
 * Initialize with external I2S codec.
 */
FH_SINT32 FH_AC_Init_WithExternalCodec(/* params */);

/**
 * Deinitialize audio codec.
 */
FH_SINT32 FH_AC_DeInit(void);

/**
 * Set audio configuration.
 * Params include: sample rate, bit depth (16/24), channels (1/2)
 * Default config observed: 0x1040004
 *   bits 0-7:   format (4 = 16-bit)
 *   bits 8-15:  reserved
 *   bits 16-23: channels? (4)
 *   bits 24-31: sample rate index? (1 = 8kHz?)
 */
FH_SINT32 FH_AC_Set_Config(/* config */);
FH_SINT32 FH_AC_Set_Config_Ext(/* config */);

/**
 * Get audio codec version.
 */
FH_SINT32 FH_AC_Version(FH_UINT32 print);

/* ---- Audio Input (AI) ---- */

FH_SINT32 FH_AC_AI_Enable(void);
FH_SINT32 FH_AC_AI_Disable(void);
FH_SINT32 FH_AC_AI_Pause(void);
FH_SINT32 FH_AC_AI_Resume(void);

/**
 * Get audio input frame.
 */
FH_SINT32 FH_AC_AI_GetFrame(/* frame */);
FH_SINT32 FH_AC_AI_GetFrameWithPts(/* frame */);
FH_SINT32 FH_AC_AI_GetFrameWithPtsFast(/* frame */);

/**
 * Set input volume.
 */
FH_SINT32 FH_AC_AI_SetVol(FH_UINT32 volume);
FH_SINT32 FH_AC_AI_MICIN_SetVol(FH_UINT32 volume);
FH_SINT32 FH_AC_AI_CH_SetAnologVol(/* params */);
FH_SINT32 FH_AC_AI_CH_SetDigitalVol(/* params */);

/**
 * Set audio processing algorithm parameters.
 * @param params  Algorithm config struct
 * @param size    0xEA (234 bytes)
 *
 * Includes echo cancellation, noise reduction, AGC.
 */
FH_SINT32 FH_AC_AI_Set_Algo_Param(void *params, FH_UINT32 size);
FH_SINT32 FH_AC_AI_Get_Algo_Param(void *params, FH_UINT32 size);

/**
 * Bind audio input to encoder pipeline.
 */
FH_SINT32 FH_AC_AI_Bind(/* params */);

/**
 * Clear audio input buffer.
 */
FH_SINT32 FH_AC_AI_ClearBuf(void);

/**
 * Query input buffer size.
 */
FH_SINT32 FH_AC_AI_QueryBufSize(/* params */);

/**
 * Power down microphone bias.
 */
FH_SINT32 FH_AC_AI_PowerDown_MicBias(void);

/**
 * Set mix method (for dual-mic).
 */
FH_SINT32 FH_AC_AI_SetMixMethod(/* params */);

/* ---- Audio Output (AO) ---- */

FH_SINT32 FH_AC_AO_Enable(void);
FH_SINT32 FH_AC_AO_Disable(void);
FH_SINT32 FH_AC_AO_Pause(void);
FH_SINT32 FH_AC_AO_Resume(void);

/**
 * Send audio frame to speaker.
 */
FH_SINT32 FH_AC_AO_SendFrame(/* frame */);

/**
 * Set output volume.
 */
FH_SINT32 FH_AC_AO_SetVol(FH_UINT32 volume);
FH_SINT32 FH_AC_AO_SetDigitalVol(/* params */);

/**
 * Set output mode.
 */
FH_SINT32 FH_AC_AO_Set_Mode(FH_UINT32 mode);

/**
 * Audio output algorithm parameters (echo cancellation for speaker).
 */
FH_SINT32 FH_AC_AO_Set_Algo_Param(void *params, FH_UINT32 size);
FH_SINT32 FH_AC_AO_Get_Algo_Param(void *params, FH_UINT32 size);

/**
 * Clear output buffer.
 */
FH_SINT32 FH_AC_AO_ClearBuf(void);

/**
 * Query output buffer size.
 */
FH_SINT32 FH_AC_AO_QueryBufSize(/* params */);

/**
 * Wait until playback completes.
 */
FH_SINT32 FH_AC_AO_WaitPlayComplete(void);

/* ---- AI/AO Sync ---- */

/**
 * Enable audio input/output synchronization (for intercom).
 */
FH_SINT32 FH_AC_AI_AO_SYNC_Enable(/* params */);

/* ---- Raw Audio ---- */

FH_SINT32 FH_AC_Raw_SetConfig(/* params */);
FH_SINT32 FH_AC_Raw_GetFrameFast(/* params */);

/* ---- Extended IOCTL ---- */

FH_SINT32 FH_AC_Ext_Ioctl(/* params */);
FH_SINT32 FH_AC_Ext2_Ioctl(/* params */);

/**
 * Print current algorithm parameters (debug).
 */
FH_SINT32 FH_AC_Print_Algo_Param(void);

#endif /* __FH_AUDIO_H__ */
