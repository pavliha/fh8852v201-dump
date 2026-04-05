/**
 * Fullhan FH8852V201 SDK — System API
 * Reverse-engineered from ipcam binary via Ghidra
 *
 * Device node: /dev/media_process
 */

#ifndef __FH_SYS_H__
#define __FH_SYS_H__

#include "fh_common.h"

/**
 * Initialize the system.
 * Opens /dev/media_process, calls FH_SYS_Set_Resource().
 * Must be called before any VPSS/VENC operations.
 * @return 0 on success
 */
FH_SINT32 FH_SYS_Init(void);

/**
 * Post-initialization (called after VPSS/VENC setup).
 */
FH_SINT32 FH_SYS_Init_Post(void);

/**
 * Pre-initialization (called before main init).
 */
FH_SINT32 FH_SYS_Init_Pre(void);

/**
 * Shutdown the system, close device.
 */
FH_SINT32 FH_SYS_Exit(void);

/**
 * Bind video processing output to encoder.
 * Creates the VPSS → VENC pipeline.
 */
FH_SINT32 FH_SYS_BindVpu2Enc(/* params unknown */);

/**
 * Bind video processing output to background model (motion detection).
 */
FH_SINT32 FH_SYS_BindVpu2Bgm(/* params unknown */);

/**
 * Bind video processing output to neural network accelerator.
 * Channel 2 (512x288 @ 1/5 fps) is typically used for NNA.
 */
FH_SINT32 FH_SYS_BindVpu2Nn(/* params unknown */);

/**
 * Unbind by destination.
 */
FH_SINT32 FH_SYS_UnBindbyDst(/* params unknown */);

/**
 * Unbind by source.
 */
FH_SINT32 FH_SYS_UnBindbySrc(/* params unknown */);

/**
 * Unbind NNA destination.
 */
FH_SINT32 FH_SYS_UnBindByNnDst(/* params unknown */);

/**
 * Get binding info by destination.
 */
FH_SINT32 FH_SYS_GetBindbyDest(/* params unknown */);

/**
 * Read SoC chip ID.
 */
FH_SINT32 FH_SYS_GetChipID(/* params unknown */);

/**
 * Get SDK version string.
 */
FH_SINT32 FH_SYS_GetVersion(/* params unknown */);

/**
 * Read hardware register.
 */
FH_UINT32 FH_SYS_GetReg(FH_UINT32 addr);

/**
 * Write hardware register.
 */
FH_SINT32 FH_SYS_SetReg(FH_UINT32 addr, FH_UINT32 value);

/**
 * Map physical address to virtual address.
 */
FH_VOID *FH_SYS_Mmap(FH_UINT32 phys_addr, FH_UINT32 size);

/**
 * Map with cache enabled.
 */
FH_VOID *FH_SYS_MmapCached(FH_UINT32 phys_addr, FH_UINT32 size);

/**
 * Unmap virtual address.
 */
FH_SINT32 FH_SYS_Munmap(FH_VOID *virt_addr, FH_UINT32 size);

/* ---- Video Memory Manager (VMM / MMZ) ---- */

/**
 * Allocate video memory from MMZ pool.
 * MMZ region: 0xA2480000, size 28160K (from vmm.ko insmod args)
 */
FH_SINT32 FH_SYS_VmmAlloc(FH_UINT32 *phys_addr, FH_VOID **virt_addr, FH_UINT32 size);

/**
 * Free video memory.
 */
FH_SINT32 FH_SYS_VmmFree(FH_UINT32 phys_addr);

/**
 * Allocate with name tag (for debugging).
 */
FH_SINT32 FH_SYS_VmmAllocEx(FH_UINT32 *phys_addr, FH_VOID **virt_addr,
                              FH_UINT32 size, const char *name);

/**
 * Allocate cached video memory.
 */
FH_SINT32 FH_SYS_VmmAlloc_Cached(FH_UINT32 *phys_addr, FH_VOID **virt_addr, FH_UINT32 size);

/**
 * Flush cache for DMA coherency.
 */
FH_SINT32 FH_SYS_VmmFlushCache(FH_UINT32 phys_addr, FH_VOID *virt_addr, FH_UINT32 size);

/**
 * Get physical address from virtual address.
 */
FH_UINT32 FH_SYS_VmmGetPaddr_ByVaddr(FH_VOID *virt_addr);

/**
 * Set system resource configuration (called internally by FH_SYS_Init).
 */
FH_SINT32 FH_SYS_Set_Resource(void);

#endif /* __FH_SYS_H__ */
