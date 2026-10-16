/**
 * ISP DMA buffer-address wiring for FH8852V201.
 *
 * Clean-room port of V201's own isp_core_init -> isp_core_alloc_buf_addr
 * (Ghidra decompile, 2026-10-16), cross-checked against OpenIPC's unstripped
 * libispcore.so for the FH8852V200/V210 sibling chip, and against isp.ko /
 * vmm.ko's own kernel-side handlers for every ioctl and struct byte used
 * here. Full derivation in sdk/headers/ISP_BUF_ADDR_PROTOCOL_V201.md.
 *
 * Restyled to match the vendor's own idiom rather than a from-scratch
 * clean-C rewrite: a persistent global isp_core (vendor code never threads
 * this through parameters — every function reaches into the one static
 * instance), the real function names where decompiled symbols survived
 * (isp_malloc, isp_core_get_buf_size, buffer_malloc_withname), the real
 * per-branch logging style (isp_core_alloc_buf_addr's own checks dual-log to
 * /proc/driver/trace + printf with real format strings/line numbers;
 * isp_malloc and two of the three ioctl failures use a plainer two-printf
 * style instead — the vendor binary is genuinely inconsistent about this,
 * not unified here), and the real FH-style error codes (0xa008400b for
 * isp_core_alloc_buf_addr's own checks, 0xa008400e for isp_malloc's — though
 * the vendor never actually checks isp_malloc's return value at any of its
 * 13 call sites, so that code is reachable but never observed by a caller
 * in the real binary either; ours does check it, for safety beyond fidelity).
 *
 * isp_core_set_addr_to_reg() is `{ return 0; }` on V201 — a no-op, not
 * implemented here. Every buffer address reaches hardware through ioctl
 * 0x40016920; isp.ko's handler for it copies the struct straight into live
 * ISP DMA-address registers. SET/GET_STAT_INFO (0x40786904/0x80786905) is a
 * separate, genuinely-unpopulated struct in the vendor binary — sent
 * all-zero here too. No /dev/mem access anywhere in this file.
 */

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <stdint.h>

#define ISP_IOCTL_HANDSHAKE     0x40016921  /* confirmed no-op in isp.ko */
#define ISP_IOCTL_GET_BUF_INFO  0x80306926  /* output unused by this path */
#define ISP_IOCTL_SET_DMA_ADDRS 0x40016920  /* isp.ko copies this straight into hw DMA regs */
#define ISP_IOCTL_SET_STAT_INFO 0x40786904  /* confirmed never populated by the vendor either */
#define ISP_IOCTL_GET_STAT_INFO 0x80786905

#define VMM_IOCTL_ALLOC 0xC0506D0A
#define POOL_ALIGN 0x10000  /* from buffer_malloc_withname's call site in isp_core_init */

/* Every failure path in the real isp_core_alloc_buf_addr returns this same
   FH-encoded value, whether the cause was buffer size, SET_STAT_INFO,
   SET_ADDR or GET_STAT_INFO. Matched here rather than inventing our own. */
#define FH_ERR_MEM_ALLOC 0xa008400b

/*
 * isp.ko's handler for 0x40016920 copies exactly 0x58 (88) bytes
 * (arm_copy_from_user(..., 0x58, ...)) and only reads relative offsets
 * 24-60 directly; 72/80/84 are copied in but not read by that specific
 * code path (may be read elsewhere, or padding this chip doesn't use).
 * Confirmed by address_name: all 9 offsets land inside isp_core[5824..5912),
 * the exact struct this ioctl takes — not isp_core[5468], which is a
 * separate, never-written 0x78-byte struct used only by SET/GET_STAT_INFO.
 */
#define DMA_ADDRS_SIZE 88
#define DMA_OFF_NR3D_FRAME        24
#define DMA_OFF_NR3D_COEF         28
#define DMA_OFF_NR3D_NR2D_COEF    32
#define DMA_OFF_NR3D_STAT         36
#define DMA_OFF_DRC_STAT0         40
#define DMA_OFF_GOSD              60
#define DMA_OFF_ISP_MEM_FRONT     72
#define DMA_OFF_MEM_AFTER_NR3D    80
#define DMA_OFF_MEM_AFTER_DRC     84

#define STAT_INFO_SIZE 120  /* 0x78 — confirmed genuinely all-zero in V201's own ipcam */

/* isp_core_set_mode() hardcodes both multipliers to 3, unconditionally. */
#define ISP_CORE_MODE_ISPP_STAT_UNITS 3
#define ISP_CORE_MODE_ISPF_STAT_UNITS 3
#define ISPP_STAT_SIZE (ISP_CORE_MODE_ISPP_STAT_UNITS * 0x60c8)
#define ISPF_STAT_SIZE (ISP_CORE_MODE_ISPF_STAT_UNITS << 2)

/*
 * The real isp_core is a single static instance spanning thousands of
 * bytes, referenced everywhere by raw offset (isp_core[5340], etc.) because
 * decompilation lost its real field-level struct definition. Named fields
 * here cover only what isp_core_alloc_buf_addr itself touches.
 */
typedef struct {
    int isp_fd;
    int vmm_fd;

    uint32_t pool_phys_base;
    uint32_t pool_total_size;
    uint32_t pool_cursor;

    uint32_t isp_mem_front, isp_mem_front_size;
    uint32_t gosd, gosd_size;
    uint32_t ispp_stat, ispp_stat_size;
    uint32_t ispf_stat, ispf_stat_size;
    uint32_t ispb_stat, ispb_stat_size;
    uint32_t nr3d_stat, nr3d_stat_size;
    uint32_t nr3d_frame, nr3d_frame_size;
    uint32_t nr3d_coef, nr3d_coef_size;
    uint32_t nr3d_nr2d_coef, nr3d_nr2d_coef_size;
    uint32_t mem_after_nr3d, mem_after_nr3d_size;
    uint32_t drc_stat0, drc_stat0_size;
    uint32_t mem_after_drc, mem_after_drc_size;
    uint32_t isp_mem_end, isp_mem_end_size;
} isp_core_t;

static isp_core_t isp_core;

static uint32_t round_up8(uint32_t v)
{
    return (v + 7) & 0xfffffff8;
}

static uint32_t round_up128(uint32_t v)
{
    return (v + 0x7f) & 0xffffff80;
}

/*
 * Matches the real dual-logging convention: every named buffer allocation
 * in isp_core_alloc_buf_addr writes the same line to /proc/driver/trace and
 * to stdout. Real format strings, real [MEM_LOG] tag.
 */
static void mem_log(int line, uint32_t addr, uint32_t size, const char *name)
{
    char msg[256];
    int n = snprintf(msg, sizeof(msg), "msg:%d:%d:[API]", 0, 0x10);
    n += snprintf(msg + n, sizeof(msg) - n,
                  "%s(%d): [MEM_LOG] 0x%08x-0x%08x %s(%d KB)\n",
                  "isp_core_alloc_buf_addr", line, addr, addr + size, name, size >> 10);

    int fd = open("/proc/driver/trace", O_WRONLY);
    if (fd >= 0) {
        write(fd, msg, n);
        close(fd);
    }
    printf("%s(%d): [MEM_LOG] 0x%08x-0x%08x %s(%d KB)\n",
           "isp_core_alloc_buf_addr", line, addr, addr + size, name, size >> 10);
}

/* Dual /proc/driver/trace + printf style — matches the upfront buffer-size
   check and the SET_STAT_INFO failure branch in the real decompile. Both
   destinations get the exact same formatted message, verbatim. */
static void err_log(const char *func, int line, const char *fmt, ...)
{
    char msg[256];
    int n = snprintf(msg, sizeof(msg), "msg:%d:%d:[API]", 4, 0x4000000);
    char body[192];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(body, sizeof(body), fmt, ap);
    va_end(ap);
    n += snprintf(msg + n, sizeof(msg) - n, "%s(%d): %s", func, line, body);

    int fd = open("/proc/driver/trace", O_WRONLY);
    if (fd >= 0) {
        write(fd, msg, n);
        close(fd);
    }
    printf("%s(%d): %s", func, line, body);
}

/* Plain two-printf style, no trace file — matches isp_malloc's own failure
   and the SET_ADDR/GET_STAT_INFO failure branches. The real decompile is
   genuinely inconsistent about which style each failure path uses; this
   mirrors that rather than unifying it into one convention. */
static void simple_err_log(const char *func, int line, const char *fmt, ...)
{
    printf("[ISP_CORE_ERROR]: %s LINE:%d: ", func, line);
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
}

/*
 * Verbatim port of V201's isp_core_get_buf_size. Indices 6, 8, 9, 10, 0xd
 * are never written by the vendor function either — they feed core_size
 * fields this call path never reads — so they stay zero here too.
 */
/*
 * out[] is 22 words, not 21: the vendor's real local_174[21] + adjacent
 * local_120 stack variable happened to alias index 0x15 (21) by accident of
 * stack layout — reproducing that as a true out-of-bounds write here would
 * be undefined behavior, not a faithful port, so the array is sized to fit
 * the index it actually uses.
 */
static void isp_core_get_buf_size(uint32_t width, uint32_t height, uint32_t out[22])
{
    uint32_t scratch[32] = {0};
    ioctl(isp_core.isp_fd, ISP_IOCTL_GET_BUF_INFO, scratch);

    memset(out, 0, 22 * sizeof(uint32_t));
    uint32_t main_row = round_up8((width * 10 + 7) >> 3);
    uint32_t sub_row = round_up8((((width >> 2) * 10) + 7) >> 3);
    out[0] = height * main_row;
    out[1] = (height >> 2) * sub_row;
    out[2] = out[1];
    out[3] = 0x5000;
    out[4] = 0x41a0;
    out[0x11] = 0;
    out[0x12] = 0;
    out[0x13] = 0;
    out[0x14] = 0x200;
    out[0x15] = (round_up128(out[0]) +
                 ((round_up128(ISPF_STAT_SIZE) + round_up128(ISPP_STAT_SIZE) + 0xffff) & 0xffff0000) +
                 round_up128(out[1]) * 2 + 0x9400);
}

/*
 * Request struct byte layout: offset 8 = align, offset 12 = size, name at
 * 0x1C, label at 0x2C — confirmed with certainty from vmm.ko's own kernel
 * allocator (mmb_alloc), not inferred. Its logic is unambiguous: the offset-12
 * field gets page-rounded as a byte count (size), the offset-8 field gets
 * power-of-2-validated and defaulted to 0x1000 if smaller (align).
 *
 * name (0x4b99bc in ipcam) is "isp"; label (0x4d1698) is 4 zero bytes, i.e.
 * empty — both read directly off the binary, not guessed.
 */
static int buffer_malloc_withname(uint32_t size, uint32_t align, const char *name)
{
    uint8_t req[80];
    memset(req, 0, sizeof(req));
    *(uint32_t *)(req + 8) = align;
    *(uint32_t *)(req + 12) = size;
    strncpy((char *)(req + 0x1C), name, 15);

    int r = ioctl(isp_core.vmm_fd, VMM_IOCTL_ALLOC, req);
    if (r < 0)
        return r;

    isp_core.pool_phys_base = *(uint32_t *)req;
    isp_core.pool_total_size = size;
    return 0;
}

/*
 * Verbatim port of V201's isp_malloc bump allocator: aligns the pool cursor
 * up, checks it fits, advances the cursor. size == 0 is a true no-op in the
 * vendor function (phys_out and the cursor are both left untouched) — kept
 * identical here for the ispb_stat call, which the vendor issues with
 * size 0 at this resolution/mode.
 */
static int isp_malloc(uint32_t size, uint32_t align, uint32_t *phys_out)
{
    if (size == 0)
        return 0;

    uint32_t mask = align ? ~(align - 1) : 0xffffffff;
    uint32_t align_term = align ? align : 1;
    uint32_t aligned = (isp_core.pool_phys_base + isp_core.pool_cursor + align_term - 1) & mask;
    uint32_t pool_end = isp_core.pool_phys_base + isp_core.pool_total_size;

    if (pool_end < size + aligned) {
        simple_err_log("isp_malloc", 0x25, "[mm] free size : 0x%x need size:0x%x. \n",
                        pool_end - aligned, size);
        return 0xa008400e;
    }

    *phys_out = aligned;
    isp_core.pool_cursor = (size + aligned) - isp_core.pool_phys_base;
    return 0;
}

/**
 * Allocates the 13 named ISP working buffers via VMM and wires their
 * physical addresses into the isp_core[5824] DMA-address struct exactly as
 * V201's own isp_core_alloc_buf_addr does — minus the dead
 * isp_core_set_addr_to_reg() call, which never does anything on this chip.
 * The separate isp_core[5468] SET/GET_STAT_INFO struct is sent all-zero,
 * matching the vendor binary exactly (it's never populated there either).
 *
 * Caller sets isp_core.isp_fd / isp_core.vmm_fd before calling, matching
 * the vendor's own pattern of earlier init steps populating isp_core ahead
 * of this function touching it.
 */
int isp_core_alloc_buf_addr(uint32_t width, uint32_t height)
{
    uint32_t bufsz[22];
    isp_core_get_buf_size(width, height, bufsz);

    ioctl(isp_core.isp_fd, ISP_IOCTL_HANDSHAKE, &(uint32_t){0});

    if (buffer_malloc_withname(bufsz[0x15], POOL_ALIGN, "isp") < 0)
        return FH_ERR_MEM_ALLOC;
    isp_core.pool_cursor = 0;

    /*
     * The real isp_core_alloc_buf_addr checks the already-reserved pool's
     * size against this same total before doing any isp_malloc calls (the
     * pool there was reserved earlier, in isp_core_init, against an earlier
     * isp_core_get_buf_size call with the same width/height — this check
     * guards against the two computations disagreeing). Reproduced for
     * fidelity even though it's structurally always-true here: we reserve
     * the pool against this exact bufsz[0x15] one line above, in the same
     * call, rather than the vendor's earlier-reserve/later-check split.
     */
    if (isp_core.pool_total_size < bufsz[0x15]) {
        err_log("isp_core_alloc_buf_addr", 0x20a, "isp core init not get enough buffer.\n");
        return FH_ERR_MEM_ALLOC;
    }

#define ALLOC(field, sz, line) \
    if (isp_malloc((sz), 0x80, &isp_core.field) < 0) \
        return FH_ERR_MEM_ALLOC; \
    isp_core.field##_size = (sz); \
    mem_log(line, isp_core.field, isp_core.field##_size, #field)

    ALLOC(isp_mem_front, bufsz[0x11], 0x215);
    ALLOC(gosd, bufsz[0x14], 0x219);
    ALLOC(ispp_stat, ISPP_STAT_SIZE, 0x224);
    ALLOC(ispf_stat, ISPF_STAT_SIZE, 0x225);
    ALLOC(ispb_stat, 0, 0x226);
    ALLOC(nr3d_stat, bufsz[3], 0x233);
    ALLOC(nr3d_frame, bufsz[0], 0x234);
    ALLOC(nr3d_coef, bufsz[1], 0x235);
    ALLOC(nr3d_nr2d_coef, bufsz[2], 0x236);
    ALLOC(mem_after_nr3d, bufsz[0x13], 0x237);
    ALLOC(drc_stat0, bufsz[4], 0x23b);
    ALLOC(mem_after_drc, bufsz[0x13], 0x23c);
    ALLOC(isp_mem_end, bufsz[0x12], 0x240);

#undef ALLOC

    uint8_t dma_addrs[DMA_ADDRS_SIZE];
    memset(dma_addrs, 0, sizeof(dma_addrs));
    *(uint32_t *)(dma_addrs + DMA_OFF_NR3D_FRAME) = isp_core.nr3d_frame;
    *(uint32_t *)(dma_addrs + DMA_OFF_NR3D_COEF) = isp_core.nr3d_coef;
    *(uint32_t *)(dma_addrs + DMA_OFF_NR3D_NR2D_COEF) = isp_core.nr3d_nr2d_coef;
    *(uint32_t *)(dma_addrs + DMA_OFF_NR3D_STAT) = isp_core.nr3d_stat;
    *(uint32_t *)(dma_addrs + DMA_OFF_DRC_STAT0) = isp_core.drc_stat0;
    *(uint32_t *)(dma_addrs + DMA_OFF_GOSD) = isp_core.gosd;
    *(uint32_t *)(dma_addrs + DMA_OFF_ISP_MEM_FRONT) = isp_core.isp_mem_front;
    *(uint32_t *)(dma_addrs + DMA_OFF_MEM_AFTER_NR3D) = isp_core.mem_after_nr3d;
    *(uint32_t *)(dma_addrs + DMA_OFF_MEM_AFTER_DRC) = isp_core.mem_after_drc;

    uint8_t stat_info[STAT_INFO_SIZE];
    memset(stat_info, 0, sizeof(stat_info));

    if (ioctl(isp_core.isp_fd, ISP_IOCTL_SET_STAT_INFO, stat_info) != 0) {
        err_log("isp_core_alloc_buf_addr", 0x250, "ioctl SET_STAT_INFO failed: %x\n", errno);
        return FH_ERR_MEM_ALLOC;
    }

    if (ioctl(isp_core.isp_fd, ISP_IOCTL_SET_DMA_ADDRS, dma_addrs) != 0) {
        simple_err_log("isp_core_alloc_buf_addr", 599, "set SET_ADDR to driver error, errorno=%d\n ", errno);
        return FH_ERR_MEM_ALLOC;
    }

    /* isp_core_set_addr_to_reg() goes here in the vendor binary — it's
       `{ return 0; }` on V201, so there is nothing to call. */

    if (ioctl(isp_core.isp_fd, ISP_IOCTL_GET_STAT_INFO, stat_info) != 0) {
        simple_err_log("isp_core_alloc_buf_addr", 0x260, "ioctl GET_STAT_INFO failed: %x\n", errno);
        return FH_ERR_MEM_ALLOC;
    }

    return 0;
}
