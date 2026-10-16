/**
 * Clean-room ISP hardware initialization.
 *
 * REGISTER VALUES (2026-10-17 revision): isp_ispp_init()/isp_ispf_init()/
 * isp_pkg_enable()/isp_reset() below are byte-for-byte extracted from
 * decompiling ipcam's real isp_startup_Ispp_Init / isp_startup_Ispf_Init /
 * isp_startup_Isp_PkgEnable / isp_core_system_reset — not derived from
 * theory, not guessed, replacing this file's original "computed from
 * sensor resolution and documented register semantics" values entirely
 * for those four functions.
 *
 * Base-pointer validation: these functions all write through
 * isp_core+0x1378 (`iVar1` in the decompile). That field is also exactly
 * what isp_core_system_reset itself uses (`isp_core._4984_4_` — 4984
 * decimal == 0x1378), and system_reset's two bitfield writes already
 * matched this file's pre-existing isp_reset() exactly before this
 * revision — strong independent evidence that isp_core+0x1378 really is
 * the /dev/mem-mapped ISP register base (same thing isp_mmap() returns),
 * not a separate software struct that happens to share a similar name.
 *
 * Two writes in the real Ispf_Init (register offsets 0x5b0 and 0x5d4)
 * write ASCII-string pointers ("s_mtx_init", "son_auto_reboot") rather
 * than control values — almost certainly a debug/log scratch region
 * sharing the same physical aperture, not functional ISP control state.
 * A pointer value is process-specific (ipcam's own text-segment address)
 * and not meaningfully reproducible in a different binary, so those two
 * writes are intentionally NOT replicated here; every purely numeric
 * write around them is reproduced exactly.
 *
 * isp_set_pic_size() (2026-10-18 revision): SetInputPicSize/SetIspPicSize/
 * SetIspPicCut/SetNr3dTab/SetDehazeTab/SetCnrExpTab are now confirmed
 * byte-exact the same way as the rest of this file. SetLscTab and
 * SetNr3dDrcWin are ALSO now confirmed — not from a value table this
 * time, but from porting the real algorithm (isp_win_param()/
 * isp_set_stat_param(), an iterative window-size search, decompiled from
 * isp_common_set_stat_win_param/isp_common_set_stat_param) line-for-line,
 * goto-for-goto, rather than reinterpreting its control flow into
 * "cleaner" C — that's a deliberate choice to minimize the chance of
 * silently changing its behavior while porting it. IspInPicCfg also does
 * one small confirmed bitfield write to register 0x28 driven by an input
 * byte (`*(isp_core+0xd)`, likely sensor orientation/mirror) that has no
 * equivalent source in this file's simpler ctx — intentionally not
 * replicated rather than guessing that input.
 *
 * Call order fixed this revision to match the real isp_startup
 * (confirmed from decompile): Ispp_Init, then Ispf_Init, then the
 * picture-size config, then module bypass — isp_set_pic_size was
 * previously called BEFORE the Ispp/Ispf equivalents, which did not
 * match the vendor order.
 *
 * isp_module_bypass's real trigger (a 0-vs-nonzero check on an upstream
 * parameter) isn't resolvable from this file alone; isp_apply_module_bypass()
 * below assumes the common/default path (parameter 0) and performs the
 * single confirmed bit-set unconditionally — flagged as an assumption,
 * not a guess about the bit's own meaning or value.
 *
 * STILL UNRESOLVED regardless of value accuracy: whether writing any of
 * these registers via /dev/mem while isp.ko is loaded is safe at all (the
 * memory-attribute-mismatch/IRQ-race risk documented in
 * ISP_BUF_ADDR_PROTOCOL_V201.md and mini_streamer.c's header). Byte-exact
 * values reduce the chance a wrong VALUE causes a hang or bad image, but
 * they do not address that separate, still-untested access-safety
 * question — this file's registers are still only safe to write BEFORE
 * isp.ko's own concurrent access begins, if at all.
 */

#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <stdint.h>

#include "isp_init.h"

#define ISP_REG_BASE  0xE8400000
#define ISP_REG_SIZE  0x3240   /* exact vendor isp_mmap size, confirmed from isp_core_init */

/**
 * Map ISP hardware registers via /dev/mem.
 */
int isp_mmap(isp_ctx_t *ctx, uint32_t width, uint32_t height)
{
    ctx->width = width;
    ctx->height = height;
    ctx->mem_fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (ctx->mem_fd < 0) return -1;

    ctx->regs = mmap(NULL, ISP_REG_SIZE, PROT_READ | PROT_WRITE,
                      MAP_SHARED, ctx->mem_fd, ISP_REG_BASE);
    if (ctx->regs == MAP_FAILED) {
        ctx->regs = NULL;
        close(ctx->mem_fd);
        return -1;
    }
    return 0;
}

/**
 * ISP soft reset. Confirmed byte-exact from `isp_core_system_reset`:
 *   Register 0x0F0: (reg & 0xFFFFFFCF) | 0x10
 *   Register 0x0F4: (reg & 0xFFFFFFFC) | 0x02
 */
static void isp_reset(isp_ctx_t *ctx)
{
    volatile uint32_t *R = ctx->regs;
    R[0x0F0/4] = (R[0x0F0/4] & 0xFFFFFFCF) | 0x10;
    R[0x0F4/4] = (R[0x0F4/4] & 0xFFFFFFFC) | 0x02;
    usleep(1000); /* 1ms settle — our own addition, not in the vendor function */
}

/*
 * CNR exposure table copied into registers 0xb4c.. by the real
 * isp_startup_IspInPicCfg -> SetCnrExpTab (read via read_bytes at ipcam's
 * nr3d_exp_tab, 56 words / 224 bytes, 2026-10-18).
 */
static const uint32_t ISP_CNR_EXP_TABLE[56] = {
    0x03c103ff, 0x03500387, 0x02ed031d, 0x029502bf, 0x0247026d, 0x02020224, 0x01c601e3, 0x019101aa,
    0x01610178, 0x0138014c, 0x01130125, 0x00f30102, 0x00d600e4, 0x00bd00c9, 0x00a700b1, 0x0093009d,
    0x0082008a, 0x0072007a, 0x0065006b, 0x0059005f, 0x004e0054, 0x0045004a, 0x003d0041, 0x00360039,
    0x002f0032, 0x002a002c, 0x00250027, 0x00200023, 0x001d001e, 0x0019001b, 0x00160018, 0x00130015,
    0x00110012, 0x000f0010, 0x000d000e, 0x000c000c, 0x000a000b, 0x0009000a, 0x00080008, 0x00070007,
    0x00060006, 0x00050006, 0x00050005, 0x00040004, 0x00030004, 0x00030003, 0x00030003, 0x00020002,
    0x00020002, 0x00020002, 0x00010001, 0x00010001, 0x00010001, 0x00010001, 0x00010001, 0x00000001,
};

/*
 * Fixed-point log2, confirmed byte-exact port of the real `easylog2`
 * (2026-10-18). Returns a Q8-ish value: integer part in bits 8+ (×0x100),
 * fractional part in the low 8 bits. Used by isp_win_param() below.
 */
static int easylog2(uint32_t x)
{
    uint32_t v5 = x >> 1;
    uint32_t v1 = 0, v2;
    int v3, v4;

    if (v5 == 0) {
        v5 = 8;
        v3 = 1;
        v4 = 0;
    } else {
        v2 = 0;
        do {
            v1 = v2;
            v5 = v5 >> 1;
            v2 = v1 + 1;
        } while (v5 != 0);
        if (v2 > 7) {
            return (int)(v2 * 0x100 + ((x - (1u << (v2 & 0xff))) >> ((v1 - 7) & 0xff)));
        }
        v5 = 8 - v2;
        v4 = (int)(v2 * 0x100);
        v3 = (int)(1u << (v2 & 0xff));
    }
    return v4 + (int)((x - (uint32_t)v3) << (v5 & 0xff));
}

/*
 * Statistics-window geometry search, confirmed byte-exact port of the
 * real `isp_common_set_stat_win_param` (2026-10-18) — ported goto-for-goto
 * against the decompile rather than restructured, to avoid silently
 * changing behavior while porting an iterative algorithm (as opposed to
 * copying a fixed value, where there's nothing to get subtly wrong).
 * `p` is a 5-word view into isp_set_stat_param()'s 21-word array below.
 */
static void isp_win_param(uint32_t *p, int mode, int sel)
{
    uint32_t v1 = 0, v2, v3, v4, v5 = 0, v6 = 0, v7 = 0;

    if (mode == 0) {
        if (sel == 1) {
            v5 = p[4];
            if (v5 * 10 < 0x43d) { v6 = v5 - 1; v7 = 7; }
            else { v6 = v5 - 1; v7 = (v5 * 10) / 0x9b; }
        } else if (sel == 0) {
            v5 = p[4];
            if (v5 * 10 < 0x89d) { v6 = v5 - 1; v7 = 7; }
            else { v6 = v5 - 1; v7 = (v5 * 10) / 0x13b; }
        } else {
            goto fallback;
        }
        goto common;
    } else if (mode == 1) {
        if (sel == 1) {
            v5 = p[4]; v6 = v5 - 1; v7 = v6 / 0x16 - 1;
        } else if (sel == 0) {
            v5 = p[4]; v6 = v5 - 1; v7 = v6 / 0x12 - 1;
        } else {
            goto fallback;
        }
        goto common;
    } else {
        goto fallback;
    }

common:
    v3 = (v6 >> 1) - 1;
    if (p[0] <= v3) v3 = p[0];
    if (v3 < v7) v3 = v7;
    p[0] = v3;
    if ((v3 & 1) != 0) goto after_adjust;
    goto adjust;

fallback:
    v5 = p[4];
    v6 = v5 - 1;
    v3 = (v6 >> 1) - 1;
    if (p[0] <= v3) v3 = p[0];
    p[0] = v3;
    v7 = 0;
    if ((v3 & 1) != 0) goto after_adjust;

adjust:
    if (v7 < v3) { v3 = v3 - 1; p[0] = v3; }
    else { v3 = v3 + 1; p[0] = v3; }

after_adjust:
    if (v3 < v7) {
        v1 = v3 + 1;
        goto finish;
    }
    v1 = v3 + 1;
    v2 = v5 - (v1 >> 1);
    v4 = v3;
    if ((v2 - v1 * (v2 / v1)) - 1 < 7) {
        do {
            v3 = v4 - 2;
            v1 = v4 - 1;
            v2 = v5 - (v1 >> 1);
            if (v3 < v7) { p[0] = v3; goto finish; }
            v4 = v3;
        } while ((v2 - v1 * (v2 / v1)) - 1 < 7);
        p[0] = v3;
    }
    v7 = v1;
    if (v3 == 0xffffffffu) v7 = 1;
    p[2] = v6 / v7 + 1;

finish: ;
    v5 = (uint32_t)easylog2(v1);
    v6 = p[0] + 1;
    v7 = v6 >> 1;
    if (p[0] == 0xffffffffu) v6 = 1;
    p[3] = (v5 >> 8) + 2;
    p[1] = (v7 + (1u << (((v5 >> 8) + 10) & 0xffu))) / v6 - 1;
}

/*
 * Confirmed byte-exact port of the real `isp_common_set_stat_param`
 * (2026-10-18). `a` is a 21-word array: a[18]=height, a[19]=width,
 * a[20]=mode (0 or 1, selects which of SetLscTab/SetNr3dDrcWin's two
 * passes this is), set by the caller before each call.
 */
static void isp_set_stat_param(uint32_t *a)
{
    uint32_t mode = a[20] & 0xff;
    uint32_t preset_a, preset_b;

    if (mode == 0) {
        a[2] = 0xe;
        a[8] = 0x18;
        a[14] = 6;
        a[16] = 0x400;
        preset_b = 0x17;
        a[12] = 0xcb;
        preset_a = 0xd;
    } else if (mode == 1) {
        preset_a = 8;
        a[14] = 8;
        a[8] = 0x10;
        a[2] = 9;
        a[12] = 0x6da;
        a[16] = 0x3000;
        preset_b = 0xf;
    } else {
        preset_a = a[2] - 1;
        preset_b = a[8] - 1;
        if (preset_a == 0) preset_a = 1;
        if (preset_b == 0) preset_b = 1;
    }
    a[4] = a[18];
    a[10] = a[19];
    a[17] = 1;
    a[6] = (a[19] - 1u) / preset_b - 1;
    a[0] = (a[18] - 1u) / preset_a - 1;
    a[5] = 0;
    a[11] = 0;
    isp_win_param(a + 0, (int)mode, 0);
    isp_win_param(a + 6, (int)mode, (int)a[11]);
    isp_win_param(a + 12, (int)mode, (int)a[17]);
}

/*
 * Confirmed byte-exact port of the real `SetNr3dDrcWin` (2026-10-18):
 * two passes of isp_set_stat_param (mode 0, then mode 1), each writing
 * the same 8-register pattern extracted from the resulting 21-word array.
 */
static void isp_set_nr3d_drc_win(isp_ctx_t *ctx, uint32_t width, uint32_t height)
{
    volatile uint32_t *R = ctx->regs;
    uint32_t a[21];
    memset(a, 0, sizeof(a));
    a[18] = height;
    a[19] = width;

    a[20] = 0;
    isp_set_stat_param(a);
    R[0x2ac/4] = (a[14] << 16) | (a[8] << 8) | a[2];
    R[0x2b0/4] = a[0] | (a[6] << 16);
    R[0x2b4/4] = a[12];
    R[0x2b8/4] = (a[7] << 16) | (a[3] << 10) | a[1] | (a[9] << 26);
    R[0x2bc/4] = a[13] | (a[15] << 10);
    R[0x2d0/4] = (a[14] << 16) | (a[8] << 8) | a[2];
    R[0x2d4/4] = a[0] | (a[6] << 16);
    R[0x2d8/4] = a[12];

    a[20] = 1;
    isp_set_stat_param(a);
    R[0x604/4] = (a[14] << 16) | (a[8] << 8) | a[2];
    R[0x608/4] = a[0] | (a[6] << 16);
    R[0x60c/4] = a[12];
    R[0x610/4] = (a[7] << 16) | (a[3] << 10) | a[1] | (a[9] << 26);
    R[0x614/4] = a[13] | (a[15] << 10);
    R[0x624/4] = (a[14] << 16) | (a[8] << 8) | a[2];
    R[0x628/4] = a[0] | (a[6] << 16);
    R[0x62c/4] = a[12];
}

/*
 * Confirmed byte-exact port of the real `SetLscTab` (2026-10-18). Zeros a
 * 299-word register region (0x1590-0x1a38) then overwrites the last 10
 * words with block-count-derived values — matches the real function's
 * mostly-zero local buffer with 10 explicit tail writes, copied out via a
 * loop this port collapses into direct writes (same end state, no loop
 * needed since every zeroed word stays zero). Only the width/height>=0
 * path applies — this project never calls it with negative crop values.
 */
static void isp_set_lsc_tab(isp_ctx_t *ctx, uint32_t width, uint32_t height)
{
    volatile uint32_t *R = ctx->regs;
    uint32_t blk_w = width >> 5;
    uint32_t blk_h = height >> 5;

    for (uint32_t off = 0x1590; off <= 0x1a38; off += 4) {
        R[off/4] = 0;
    }

    uint32_t v0 = (blk_w << 16) | (blk_w << 24) | (blk_w << 8);
    uint32_t v1 = blk_w | v0;
    uint32_t v5 = (blk_h << 16) | (blk_h << 24) | (blk_h << 8);
    uint32_t v6 = blk_h | v5;

    R[0x1a14/4] = v0;
    R[0x1a18/4] = v1;
    R[0x1a1c/4] = v1;
    R[0x1a20/4] = v1;
    R[0x1a24/4] = blk_w - 1;
    R[0x1a28/4] = v5;
    R[0x1a2c/4] = v6;
    R[0x1a30/4] = v6;
    R[0x1a34/4] = v6;
    R[0x1a38/4] = blk_h - 1;
}

/**
 * Picture size / crop / stride / stat-window registers.
 *
 * CONFIRMED (2026-10-18) from decompiling isp_startup_IspInPicCfg's real
 * helpers: SetInputPicSize, SetIspPicSize, SetIspPicCut, SetLscTab,
 * SetNr3dDrcWin, SetCnrExpTab — all byte-exact now, the last two via a
 * ported algorithm rather than a value table (see isp_win_param() and
 * isp_set_lsc_tab() above). SetNr3dTab and SetDehazeTab are confirmed
 * no-ops — nothing to call for them. No crop offset equivalent exists in
 * this file's ctx (the real IspInPicCfg takes crop x/y from isp_core
 * fields this file has no equivalent for); SetIspPicCut is called with
 * (0,0), full-frame, which is what the pre-existing code already assumed.
 */
static void isp_set_pic_size(isp_ctx_t *ctx)
{
    volatile uint32_t *R = ctx->regs;
    uint32_t w = ctx->width;
    uint32_t h = ctx->height;

    /* SetInputPicSize(ctx, w, h) */
    uint32_t input_size = (w - 1) | ((h - 1) << 16);
    R[0x130/4] = input_size;
    R[0x144/4] = input_size;

    /* SetIspPicSize(ctx, w, h) */
    uint32_t blk_h = (h >> 5) - 1;
    uint32_t blk_w = (w >> 5) - 1;
    uint32_t isp_size = (w - 1) | ((h - 1) << 16);
    R[0x138/4] = isp_size;
    R[0x14c/4] = isp_size;
    R[0x490/4] = (blk_h << 24) | ((w / 0x30 - 1) << 16);
    R[0x0d0/4] = isp_size;
    R[0x500/4] = 0;
    R[0x504/4] = 0;
    R[0x508/4] = blk_w | (blk_h << 16) | 0x78007800;

    uint32_t raw_stride = ((w * 10 + 7) >> 3) + 7;
    R[0x4e0/4] = ((blk_w | (blk_h << 8)) & 0x1fffffff) | (R[0x4e0/4] & 0xe0000000) | 0xf0f0000;
    uint32_t q_stride = (((w >> 2) * 10) >> 3) + 7;
    R[0x4e4/4] = 0;
    R[0x4e8/4] = 0x1fff1;
    R[0x2e8/4] = raw_stride & 0xfffffff8;
    uint32_t q_stride_en = (q_stride & 0x7ffffff8) | 0x80000000;
    R[0x2f0/4] = (raw_stride & 0x7ffffff8) | 0x80000000;
    R[0x2f8/4] = 0x280;
    R[0x300/4] = 0x80000280;
    R[0x308/4] = q_stride & 0xfffffff8;
    R[0x310/4] = q_stride_en;
    R[0x318/4] = q_stride & 0xfffffff8;
    R[0x320/4] = q_stride_en;
    R[0x41c/4] = q_stride & 0xfff8;
    R[0xaa8/4] = q_stride & 0xfff8;
    R[0x648/4] = 0x80000348;
    R[0x658/4] = 0x80000348;

    /* SetIspPicCut(ctx, 0, 0) — full frame, no crop */
    R[0x134/4] = 0;
    R[0x148/4] = 0;

    isp_set_lsc_tab(ctx, w, h);
    isp_set_nr3d_drc_win(ctx, w, h);
    /* SetNr3dTab(): confirmed no-op */

    /* SetCnrExpTab(ctx): direct 1:1 copy, register offset 0xb4c + i*4 = table[i] */
    for (size_t i = 0; i < 56; i++) {
        R[(0xb4c + i * 4) / 4] = ISP_CNR_EXP_TABLE[i];
    }
}

/*
 * Gamma-derived table copied into registers 0x1a40.. by the real
 * isp_startup_Ispp_Init (read via read_bytes at ipcam's Tab_gamma, 80
 * words / 320 bytes, 2026-10-17).
 */
static const uint32_t ISP_GAMMA_TABLE[80] = {
    0x00050000, 0x000f000a, 0x00190014, 0x0022001e, 0x002c0027, 0x00350031, 0x003e003a, 0x00470043,
    0x0050004c, 0x00590055, 0x0062005d, 0x006a0066, 0x0073006f, 0x007b0077, 0x0083007f, 0x008b0087,
    0x0093008f, 0x009b0097, 0x00a3009f, 0x00aa00a7, 0x00b200ae, 0x00b900b6, 0x00c100bd, 0x00c800c4,
    0x00cf00cb, 0x00d600d3, 0x00dd00d9, 0x00e400e0, 0x00eb00e7, 0x00f100ee, 0x00f800f5, 0x00fe00fb,
    0x010e0102, 0x0127011b, 0x013e0132, 0x01540149, 0x0169015f, 0x017d0173, 0x01910187, 0x01a3019a,
    0x01b501ac, 0x01c601be, 0x01d601ce, 0x01e601de, 0x01f501ee, 0x020401fd, 0x0212020b, 0x021f0219,
    0x022c0226, 0x02390233, 0x0245023f, 0x0251024b, 0x025c0256, 0x02670262, 0x0272026c, 0x027c0277,
    0x02940281, 0x02b702a6, 0x02d702c7, 0x02f302e5, 0x030c0300, 0x03230318, 0x0338032e, 0x034b0342,
    0x035d0354, 0x036d0365, 0x037c0375, 0x038a0383, 0x03970391, 0x03a3039d, 0x03ae03a9, 0x03b903b4,
    0x03c303be, 0x03cc03c7, 0x03d503d0, 0x03dd03d9, 0x03e503e1, 0x03ec03e8, 0x03f303f0, 0x03fa03f6,
};

/**
 * ISP processing-pipeline module enable + tone-curve registers. Confirmed
 * byte-exact from `isp_startup_Ispp_Init` (2026-10-17).
 */
static void isp_ispp_init(isp_ctx_t *ctx)
{
    volatile uint32_t *R = ctx->regs;

    R[0x008/4] = 0x7ffffff;
    R[0x080/4] = 0x50402010;
    R[0x084/4] = 0x90807060;
    R[0x088/4] = 0xd0c0b0a0;
    R[0x08c/4] = 0xfff0e0;
    R[0x090/4] = 0x10101010;
    R[0x094/4] = 0x10101010;
    R[0x098/4] = 0x10101010;
    R[0x09c/4] = 0xf1010;
    R[0x0a0/4] = 0x30281810;
    R[0x0a4/4] = 0x50484038;
    R[0x0a8/4] = 0x80706058;
    R[0x0ac/4] = 0xc8c0a090;
    R[0x0b0/4] = 0xe0d8d0;
    R[0x0b4/4] = 0;
    R[0x0b8/4] = 0;
    R[0x494/4] = 0x9015;
    R[0x13c/4] = 0;
    R[0x140/4] = 0;
    R[0x150/4] = 0;
    R[0x154/4] = 0;
    R[0x578/4] = 0;
    R[0x57c/4] = 0;

    /*
     * Gamma table fan-out, confirmed from the real copy loop: each
     * table word is written raw at +0x3c0, and left-shifted by 2 at
     * +0, +0x140, +0x280 (all relative to register offset 0x1a40).
     */
    for (size_t i = 0; i < 80; i++) {
        uint32_t raw = ISP_GAMMA_TABLE[i];
        uint32_t shifted = raw << 2;
        R[(0x1a40 + i * 4) / 4] = shifted;
        R[(0x1b80 + i * 4) / 4] = shifted;
        R[(0x1cc0 + i * 4) / 4] = shifted;
        R[(0x1e00 + i * 4) / 4] = raw;
    }
}

/*
 * DRC k-L curve table copied into registers 0x690.. by the real
 * isp_startup_Ispf_Init (read via read_bytes at ipcam's Tab_drc_klcurve,
 * 129 words / 516 bytes, 2026-10-17).
 */
static const uint32_t ISP_DRC_KLCURVE_TABLE[129] = {
    0x03000000, 0x03fe037b, 0x050e0484, 0x0629059a, 0x074b06ba, 0x087307de, 0x099d0908, 0x0aca0a33,
    0x0bf90b61, 0x0d280c90, 0x0e570dbf, 0x0f850eee, 0x10b3101c, 0x11e0114a, 0x130b1275, 0x1433139f,
    0x155b14c8, 0x167f15ee, 0x17a11711, 0x18c11832, 0x19de1950, 0x1af71a6b, 0x1c0e1b83, 0x1d221c99,
    0x1e321daa, 0x1f3f1eb9, 0x20481fc4, 0x214e20cb, 0x224f21cf, 0x234e22cf, 0x244823cc, 0x253f24c5,
    0x26aa25b9, 0x28812798, 0x2a482967, 0x2bff2b26, 0x2da72cd5, 0x2f3e2e74, 0x30c63004, 0x323e3184,
    0x33a732f5, 0x35013456, 0x364c35a8, 0x378836eb, 0x38b53820, 0x39d53947, 0x3ae83a60, 0x3bed3b6c,
    0x3d5e3c6b, 0x3f213e45, 0x40bc3ff3, 0x4233417b, 0x439042e4, 0x44dd4438, 0x46214580, 0x475d46c0,
    0x489047f7, 0x49bc4927, 0x4ae14a4f, 0x4bff4b70, 0x4d164c8b, 0x4e284da0, 0x4f344eaf, 0x503b4fb8,
    0x51bb50bc, 0x53ac52b6, 0x558b549e, 0x575b5675, 0x591d583e, 0x5ad159f8, 0x5c795ba6, 0x5e155d48,
    0x5fa75ee0, 0x612f606d, 0x62ae61f0, 0x6424636a, 0x659264dc, 0x66f86646, 0x685767a9, 0x69af6904,
    0x6ba76a58, 0x6e316cef, 0x70a66f6e, 0x730671d8, 0x7554742f, 0x77917674, 0x79be78a9, 0x7bdd7acf,
    0x7dee7ce7, 0x7ff37ef2, 0x81ed80f1, 0x83db82e5, 0x85bf84ce, 0x879986ad, 0x896b8883, 0x8b338a50,
    0x8dd28c15, 0x91358f87, 0x947e92dd, 0x97ae9619, 0x9ac9993e, 0x9dcf9c4e, 0xa0c29f4b, 0xa3a5a236,
    0xa678a511, 0xa93da7dd, 0xabf5aa9b, 0xaea0ad4c, 0xb140aff1, 0xb3d5b28c, 0xb660b51c, 0xb8e2b7a2,
    0xbc95ba20, 0xc169bf03, 0xc621c3c8, 0xcac2c874, 0xcf4dcd0a, 0xd3c5d18b, 0xd82dd5fb, 0xdc87da5c,
    0xe0d4deaf, 0xe516e2f6, 0xe94ee733, 0xed7deb66, 0xf1a5ef92, 0xf5c7f3b7, 0xf9e4f7d6, 0xfdfcfbf1,
    0x00010007,
};

/**
 * ISP filter/color-correction register block. Confirmed byte-exact from
 * `isp_startup_Ispf_Init` (2026-10-17). Two writes present in the real
 * function (offsets 0x5b0, 0x5d4 — ASCII-string pointers, not control
 * values) are intentionally omitted; see file header.
 */
static void isp_ispf_init(isp_ctx_t *ctx)
{
    volatile uint32_t *R = ctx->regs;

    R[0x0e0/4] = 0x400102;
    R[0x0e4/4] = R[0x0e4/4] | 2;
    R[0x400/4] = 1;
    R[0x404/4] = 0xf001f4a0;
    R[0x408/4] = 0xf001f4a0;
    R[0x40c/4] = 0xf001f4a0;
    R[0x410/4] = 0xf001f4a0;
    R[0x414/4] = 0x1fff009c;
    R[0x1c4/4] = 0xc;
    R[0x510/4] = 0x400;
    R[0x514/4] = 0x3ffffbff;
    R[0x518/4] = 0x3ff00001;
    R[0x51c/4] = 0x3feffc00;
    R[0x520/4] = 0xffbfe;
    R[0x50c/4] = 0x100fff;
    R[0x548/4] = 0xfff;
    R[0x54c/4] = 0xfff0000;
    R[0x550/4] = 0xfff;
    R[0x554/4] = 0xfff0fff;
    R[0x558/4] = 0xfff0000;
    R[0x55c/4] = 0xfff0fff;
    R[0x538/4] = 0x40;
    R[0x53c/4] = 0;
    R[0x570/4] = 0x20002e4;
    R[0x574/4] = 0x35c0200;
    R[0x578/4] = 0;
    R[0x57c/4] = 0;
    R[0xc2c/4] = 0x404;
    R[0xc30/4] = 0x10000404;
    R[0xc34/4] = 0x786e6868;
    R[0xc38/4] = 0x7e7a7a78;
    R[0xc3c/4] = 0x8000000;
    R[0xc40/4] = 0xa140204;
    R[0xc44/4] = 0xa141325;
    R[0xc48/4] = 0x10101325;
    R[0xc4c/4] = 0x3ff0000;
    R[0xc50/4] = 0x1400c04;
    R[0xc54/4] = 0x7c000606;
    R[0xc58/4] = 0x7f7d7d7c;
    R[0xc5c/4] = 0x6000000;
    R[0xc60/4] = 0x1a140204;
    R[0xc64/4] = 0x1a143425;
    R[0xc68/4] = 0x48483425;
    R[0xc6c/4] = 0x3fc0000;
    R[0xc70/4] = 0x4000;
    R[0xaac/4] = 0;
    R[0xab0/4] = 0x160000;
    R[0xab4/4] = 0x100010;
    R[0xab8/4] = 0x801;
    R[0xabc/4] = 0x1000805b;
    R[0xac0/4] = 0x80030000;
    R[0xac4/4] = 0x11100a10;
    R[0xac8/4] = 0x400040;
    R[0xacc/4] = 0x400040;
    R[0xad0/4] = 0x400040;
    R[0xad4/4] = 0x400040;
    R[0xad8/4] = 0x400040;
    R[0xadc/4] = 0x400040;
    R[0xae0/4] = 0x400040;
    R[0xae4/4] = 0x400040;
    R[0xae8/4] = 0x400040;
    R[0xaec/4] = 0x400040;
    R[0xaf0/4] = 0x400040;
    R[0xaf4/4] = 0x400040;
    R[0xaf8/4] = 0x400040;
    R[0xafc/4] = 0x400040;
    R[0xb00/4] = 0x400040;
    R[0xb04/4] = 0x400040;
    R[0xb08/4] = 0x400040;
    R[0xb0c/4] = 0x400040;
    R[0xb10/4] = 0x400040;
    R[0xb14/4] = 0x400040;
    R[0xb18/4] = 0x400040;
    R[0xb1c/4] = 0x400040;
    R[0xb20/4] = 0x400040;
    R[0xb24/4] = 0x400040;
    R[0xb28/4] = 0xaa00a0;
    R[0xb2c/4] = 0x2800280;
    R[0xb30/4] = 0x7800780;
    R[0xb34/4] = 0x1200120;
    R[0xb38/4] = 0x360036;
    R[0xb3c/4] = 0x260026;
    R[0xb40/4] = 0x30181414;
    R[0xb44/4] = 0x4000;
    R[0xb48/4] = 0x80404000;
    R[0xe00/4] = 0x40;
    R[0xe04/4] = 0x800000;
    R[0xe08/4] = 0xcc011c;
    R[0xe0c/4] = 0x10101;
    R[0xe10/4] = 0xff0000;
    R[0xe14/4] = 0;
    R[0xe18/4] = 0x29c0054;
    R[0xe1c/4] = 0x29c0260;
    R[0xe20/4] = 0x1640260;
    R[0xe24/4] = 0x16403ac;
    R[0xe28/4] = 0x16401a4;
    R[0xe2c/4] = 0x36401a4;
    R[0xa30/4] = 0x200;
    R[0xa34/4] = 0;
    R[0xa38/4] = 0x200;
    R[0xa3c/4] = 0;
    R[0xa40/4] = 0x200;
    R[0xa44/4] = 0;
    R[0xc74/4] = 0x105;
    R[0xa60/4] = 0x2590132;
    R[0xa64/4] = 0xf530074;
    R[0xa68/4] = 0x2000ead;
    R[0xa6c/4] = 0xe530200;
    R[0xa70/4] = 0xfad;
    R[0xa74/4] = 0x4000400;
    R[0xa78/4] = 0x3ff3ff11;
    R[0x250/4] = 0xff3983;
    R[0x254/4] = 0x1000100;
    R[0x258/4] = 0x1000100;
    R[0x25c/4] = 0x80;
    R[0x260/4] = 0x80;
    R[0x264/4] = 0x80;
    R[0x268/4] = 0x80;
    R[0x26c/4] = 0x200c000;
    R[0x278/4] = 0xc8081a0a;
    R[0x27c/4] = 0x2408c23;
    R[0x280/4] = 0x2d0a026;
    R[0x284/4] = 0x6511c37;
    R[0x288/4] = 0x1603a898;
    R[0x28c/4] = 0x30da29f2;
    R[0x290/4] = 0x3d0eaf6d;
    R[0x294/4] = 0x3f8fc7e5;
    R[0x298/4] = 0x3feff7fb;
    R[0x29c/4] = 0x3ffffffe;
    R[0x2a8/4] = 2;
    R[0x2c0/4] = 0x3523120a;
    R[0x2c4/4] = 0x9b5e9b5b;
    R[0x2c8/4] = 0xff;
    R[0x590/4] = 0xd;
    R[0x594/4] = 0x30303;
    R[0x598/4] = 0x1400a0;
    R[0x59c/4] = 0xfc0de0;
    R[0x5a0/4] = 0x1800c00;
    R[0x5a4/4] = 0x334;
    R[0x5a8/4] = 0x3e6;
    R[0x5ac/4] = 0xff;
    /* 0x5b0: "s_mtx_init" string pointer in the vendor binary — omitted, see file header */
    R[0x5b4/4] = 0xc208f;
    R[0x5b8/4] = 0x163117;
    R[0x5bc/4] = 0x22e1c4;
    R[0x5c0/4] = 0x3a829d;
    R[0x5c4/4] = 0x4a2;
    R[0x5c8/4] = 0x2b211607;
    R[0x5cc/4] = 0x514e4639;
    R[0x5d0/4] = 0x61c394c;
    /* 0x5d4: "son_auto_reboot" string pointer in the vendor binary — omitted, see file header */
    R[0x5d8/4] = 0x1340ab;
    R[0x5dc/4] = 0x3b0263;
    R[0x5e0/4] = 0x7a2589;
    R[0x5e4/4] = 0xd689b1;
    R[0x5e8/4] = 0xf1e;
    R[0x5ec/4] = 199;
    R[0x5f0/4] = 0x80e06080;
    R[0x5f4/4] = 0x4006480;
    R[0x5f8/4] = 0x4006480;
    R[0x5fc/4] = 0xb40d9a;
    R[0x600/4] = 0x10000cd;
    R[0x618/4] = 0x3523120a;
    R[0x61c/4] = 0x9b5e9b5b;
    R[0x620/4] = 0xff;
    R[0x664/4] = 0x20222e;
    R[0x668/4] = 0;
    R[0x66c/4] = 0x3f0;

    /* DRC k-L curve table: direct 1:1 copy, register offset 0x690 + i*4 = table[i] */
    for (size_t i = 0; i < 129; i++) {
        R[(0x690 + i * 4) / 4] = ISP_DRC_KLCURVE_TABLE[i];
    }
}

/**
 * Single confirmed bit-set from `isp_module_bypass` (register 0x2c, bit
 * 0x400). The vendor function only performs this when its caller-supplied
 * parameter is 0; that parameter's real value isn't resolvable from this
 * file alone, so this assumes the common/default (0) path. See file header.
 */
static void isp_apply_module_bypass(isp_ctx_t *ctx)
{
    volatile uint32_t *R = ctx->regs;
    R[0x02c/4] = R[0x02c/4] | 0x400;
}

/**
 * ISP processing enable. Confirmed byte-exact from
 * `isp_startup_Isp_PkgEnable`.
 */
static void isp_pkg_enable(isp_ctx_t *ctx)
{
    volatile uint32_t *R = ctx->regs;
    R[0x100/4] = 4;
    R[0x034/4] = 5;
    R[0x100/4] = R[0x100/4] | 1;
}

/**
 * Complete ISP hardware initialization.
 * Call this BEFORE isp_start (ioctl 0x690A).
 */
int isp_hw_init(isp_ctx_t *ctx, uint32_t width, uint32_t height)
{
    if (isp_mmap(ctx, width, height) < 0) return -1;

    /* Verify ISP chip ID. Not vendor behavior — isp_core_init itself does
       no such check — this is our own extra safety probe before any write. */
    if (ctx->regs[0] != 0x5A542093) {
        fprintf(stderr, "ISP chip ID mismatch: 0x%08X\n", ctx->regs[0]);
        return -1;
    }

    isp_reset(ctx);
    isp_ispp_init(ctx);
    isp_ispf_init(ctx);
    isp_set_pic_size(ctx);
    isp_apply_module_bypass(ctx);
    /* Do NOT call isp_pkg_enable yet — wait for caller to do ioctl 0x690A first */

    return 0;
}

/**
 * Enable ISP frame processing.
 * Call this AFTER ioctl 0x690A (isp_start).
 */
void isp_start_processing(isp_ctx_t *ctx)
{
    isp_pkg_enable(ctx);
}

void isp_cleanup(isp_ctx_t *ctx)
{
    if (ctx->regs) {
        munmap((void *)ctx->regs, ISP_REG_SIZE);
        ctx->regs = NULL;
    }
    if (ctx->mem_fd >= 0) {
        close(ctx->mem_fd);
        ctx->mem_fd = -1;
    }
}
