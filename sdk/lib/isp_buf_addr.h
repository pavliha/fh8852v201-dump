#ifndef __ISP_BUF_ADDR_H__
#define __ISP_BUF_ADDR_H__

#include <stdint.h>

/*
 * Give isp_core_alloc_buf_addr() the two already-open file descriptors it
 * needs (isp.ko and vmm.ko). Call once before isp_core_alloc_buf_addr().
 */
void isp_core_set_fds(int isp_fd, int vmm_fd);

/*
 * Allocates the 13 named ISP working buffers and wires their physical
 * addresses into hardware via ioctl 0x40016920. See
 * sdk/headers/ISP_BUF_ADDR_PROTOCOL_V201.md for the full derivation.
 * Returns 0 on success, an FH-style error code otherwise.
 */
int isp_core_alloc_buf_addr(uint32_t width, uint32_t height);

#endif
