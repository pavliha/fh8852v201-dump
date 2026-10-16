#ifndef __ISP_INIT_H__
#define __ISP_INIT_H__

#include <stdint.h>

typedef struct {
    volatile uint32_t *regs;
    int mem_fd;
    uint32_t width;
    uint32_t height;
} isp_ctx_t;

int isp_mmap(isp_ctx_t *ctx, uint32_t width, uint32_t height);

/*
 * Maps the ISP registers and writes the register-table bring-up sequence
 * (reset, pic size, module enables, filters). THIS IS THE STILL-UNRESOLVED
 * PART — see mini_streamer.c's file header. Call after ioctl 0x690A
 * (isp_core_init in the real vendor binary issues that ioctl before its
 * own mmap/register-table init, not after — confirmed by decompile).
 */
int isp_hw_init(isp_ctx_t *ctx, uint32_t width, uint32_t height);

void isp_start_processing(isp_ctx_t *ctx);
void isp_cleanup(isp_ctx_t *ctx);

#endif
