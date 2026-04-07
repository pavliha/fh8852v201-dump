/**
 * Fullhan FH8852V201 SDK — Neural Network Accelerator (NNA)
 * Reverse-engineered from ipcam binary via Ghidra
 *
 * Device node: /dev/nna (via nna.ko module)
 * Clock: 90MHz (configured via _media_driver_config: "nn_clk,enable,90000000")
 * IRQ: 42 (NNA_IRQ)
 *
 * Model format: .nbg (Fullhan proprietary neural binary graph)
 * Input: 512x288 from VPSS channel 2 (1/5 frame rate)
 * Used for: person detection (persondet.nbg)
 *
 * Init flow:
 *   FH_SYS_BindVpu2Nn()  — bind VPSS ch2 → NNA
 *   FH_NNA_DET_Init()    — load model, allocate 0x460C bytes context
 *   FH_NNA_InitNet()     — load model to NNA hardware
 *   FH_NNA_SubmitFrame() — submit frame from VPSS
 *   FH_NNA_DET_Process() — run inference
 *   FH_NNA_DET_Forward() — get detection results
 */

#ifndef __FH_NNA_H__
#define __FH_NNA_H__

#include "fh_common.h"

/**
 * NNA detection init parameters.
 * Passed to FH_NNA_DET_Init.
 */
typedef struct {
    void       *model_data;     /* [0] Pointer to .nbg model data */
    FH_UINT32   num_classes;    /* [1] Number of detection classes */
    FH_UINT32   input_width;    /* [2] Input width (e.g. 512) */
    FH_UINT32   input_height;   /* [3] Input height (e.g. 288) */
    FH_UINT32   input_channels; /* [4] Input channels (e.g. 3 for RGB) */
    FH_UINT32   batch_size;     /* [5] Batch size */
    FH_UINT32   rotate_mode;    /* [6] Rotation mode (0-3) */
} FH_NNA_DET_PARAM;

/* ---- Detection API ---- */

/**
 * Initialize NNA detector.
 * Allocates 0x460C bytes for context.
 * Loads model via FH_NNA_InitNet.
 *
 * @param handle    Output handle pointer
 * @param model_fd  Model file descriptor or data pointer
 * @param params    Detection parameters
 * @return 0 on success
 */
FH_SINT32 FH_NNA_DET_Init(void **handle, FH_SINT32 model_fd, const FH_NNA_DET_PARAM *params);

/**
 * Init with user-allocated memory.
 */
FH_SINT32 FH_NNA_DET_USRALLOC_Init(void **handle, FH_SINT32 model_fd,
                                     const FH_NNA_DET_PARAM *params);

/**
 * Shutdown detector and free resources.
 */
FH_SINT32 FH_NNA_DET_Exit(void *handle);

/**
 * Run inference on submitted frame.
 */
FH_SINT32 FH_NNA_DET_Process(void *handle);

/**
 * Get detection results after processing.
 */
FH_SINT32 FH_NNA_DET_Forward(void *handle);

/**
 * Point detection (face landmarks, etc.).
 */
FH_SINT32 FH_NNA_DET_POINT_Forward(void *handle);
FH_SINT32 FH_NNA_DET_POINT_Process(void *handle);

/**
 * Set detection parameters (thresholds, etc.).
 */
FH_SINT32 FH_NNA_DET_SetParam(void *handle, /* params */);

/* ---- Low-Level NNA API ---- */

/**
 * Initialize NNA hardware.
 */
FH_SINT32 fh_nna_init(void);

/**
 * Close NNA hardware.
 */
FH_SINT32 fh_nna_close(void);

/**
 * Load .nbg model to NNA.
 * @param model_fd    Model file descriptor
 * @param model_data  Model binary data
 * @param handles     Output NNA handles
 */
FH_SINT32 FH_NNA_InitNet(FH_SINT32 model_fd, void *model_data, void *handles);

/**
 * Deinitialize loaded model graph.
 */
FH_SINT32 FH_NNA_DeInitGraph(void *handle);

/**
 * Release graph resources.
 */
FH_SINT32 FH_NNA_RleaseGraph(void *handle);

/**
 * Get neural network info (layers, dimensions).
 */
FH_SINT32 FH_NNA_GetNetInfo(void *handle, /* info */);

/**
 * Get number of post-processing buffers needed.
 */
FH_SINT32 FH_NNA_GetPostBufNum(FH_UINT32 *num);

/**
 * Submit a video frame for NNA processing.
 * Frame comes from VPSS channel 2.
 */
FH_SINT32 FH_NNA_SubmitFrame(/* frame */);

/**
 * Set input rotation for NNA.
 */
FH_SINT32 fh_nna_set_rotate(void *ctx, const FH_NNA_DET_PARAM *params);

/**
 * Initialize .nbg model parser.
 */
FH_SINT32 fh_nna_nbg_init(void *ctx, /* params */);
FH_SINT32 fh_nna_nbg_deinit(void *ctx);
FH_SINT32 fh_nna_nbg_read(void *ctx, /* params */);
FH_SINT32 fh_nna_nbg_save(void *ctx, /* params */);
FH_UINT32 fh_nna_nbg_getsize(void *ctx);
FH_UINT32 fh_nna_nbg_calc_fmsize(void *ctx);

/**
 * Create NNA processing context.
 */
FH_SINT32 fh_nna_create_processer(void *ctx, /* params */);
FH_SINT32 fh_nna_post_processer(void *ctx, /* params */);

/**
 * Calculate NNA command count.
 */
FH_SINT32 fh_nna_cla_cmd_num(void *ctx);

/**
 * Calculate MAC (multiply-accumulate) operations.
 */
FH_UINT32 fh_nna_clc_mac_size(void *ctx);

/**
 * Flush NNA cache.
 */
FH_SINT32 fh_nna_flush_cache(void *ctx);

#endif /* __FH_NNA_H__ */
