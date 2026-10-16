# ISP Register-Table Bring-Up — FH8852V201

Derived 2026-10-17 directly from V201's own `ipcam` binary (Ghidra 12.1.4, REA). Supersedes the
original `sdk/lib/isp_init.c`'s register values, which were "computed from sensor resolution,
standard signal processing theory, and documented register semantics" — i.e. informed guesses,
not vendor data. This derivation reads the real values straight off the decompiled vendor code.

## The actual finding

`isp_core_init` — the true top-level ISP bring-up function, not previously fully decompiled — is
short and unambiguous:

```
ioctl(isp_fd, 0x690a, ...)              // handshake, confirmed benign, issued FIRST
isp_mmap(0xe8400000, &ispp_reg, 0x3240) // maps exactly 0x3240 bytes, not 0x4000
isp_core_alloc_buf_addr(w, h, ...)      // buffer-address wiring — BEFORE the register table
isp_core_system_reset()                 // 2 bitfield writes (0xF0, 0xF4)
common_init(isp_core)                   // software flag only, not a register write
isp_core_set_init_flag(1)               // software flag only
isp_startup(isp_core, 0x771dcc)         // the register table — see below
isp_module_bypass(...)                  // 1 conditional bit (register 0x2C bit 0x400)
stat_init / Fifo_Init / mutex_init      // software-only, not applicable to this project's ctx
```

The project's prior ordering — register table, *then* buffer-address allocation — was backwards.
Fixed in `majestic/mini_streamer.c`'s `isp_setup()`.

`isp_startup` itself:

```
isp_startup_pkg_init()          // confirmed no-op
isp_startup_Isp_SetMode()       // confirmed no-op
isp_startup_Ispp_Init()         // register table, part 1 — see below
isp_startup_Ispf_Init()         // register table, part 2 — see below
isp_startup_Chip_Adapt_Init()   // confirmed no-op
[optional sensor vtable hook]   // not statically resolvable; not exercised by this project
isp_startup_IspInPicCfg()       // resolution-dependent helpers (SetInputPicSize etc.) — NOT
                                 // decompiled this round; isp_set_pic_size() stays theory-derived
sensorDp_setAttribute2Isp()     // dispatches through a sensor function-pointer table Ghidra
                                 // couldn't resolve statically ("too many branches") — open gap
isp_startup_Isp_PkgEnable()     // register table, part 3 — see below
```

## Base-pointer validation

Every register write below goes through `isp_core+0x1378` (`iVar1` in the decompile). Before
trusting that as the real `/dev/mem`-mapped register base (as opposed to some other software
struct), this was cross-checked: `isp_core_system_reset` uses the *same* field
(`isp_core._4984_4_` — 4984 decimal is exactly `0x1378`), and its two bitfield writes already
matched `isp_init.c`'s pre-existing `isp_reset()` byte-for-byte *before* this derivation. That
match is strong independent evidence the field really is the register base.

One wrinkle: two writes in the real `Ispf_Init` (offsets `0x5b0`, `0x5d4`) write ASCII-string
pointers (`"s_mtx_init"`, `"son_auto_reboot"`) rather than control values — almost certainly a
debug/log scratch region sharing the physical aperture, not functional ISP state. A pointer value
is process-specific and not reproducible across binaries, so those two writes are intentionally
not replicated; every purely numeric write around them is.

## Confirmed register writes

**`isp_core_system_reset`** (2 writes, already matched the pre-existing code exactly):

| Offset | Operation |
|--------|-----------|
| 0x0F0  | `(reg & 0xFFFFFFCF) \| 0x10` |
| 0x0F4  | `(reg & 0xFFFFFFFC) \| 0x02` |

**`isp_startup_Ispp_Init`** (22 fixed writes + a table fan-out): fixed writes at offsets `0x008`,
`0x080–0x0b8`, `0x494`, `0x13c/0x140/0x150/0x154`, `0x578/0x57c` — see `isp_ispp_init()` in
`sdk/lib/isp_init.c` for the exact values. Plus: an 80-word table (`Tab_gamma`, read via
`read_bytes`) fanned out into four register regions starting at `0x1a40`, `0x1b80`, `0x1cc0`
(each word left-shifted by 2) and `0x1e00` (raw, unshifted) — all four regions `80 × 4 = 320`
bytes, confirmed from the real copy loop's index arithmetic.

**`isp_startup_Ispf_Init`** (~95 fixed writes + a table copy): fixed writes across offsets
`0x0e0–0x1c4`, `0x50c–0x57c`, `0xaac–0xb48`, `0xc2c–0xc74`, `0xe00–0xe2c`, `0xa30–0xa78`,
`0x250–0x2c8`, `0x590–0x66c` — see `isp_ispf_init()` for the exact values (and the two omitted
string-pointer writes, `0x5b0`/`0x5d4`). Plus: a 129-word table (`Tab_drc_klcurve`, read via
`read_bytes`) copied 1:1 into registers `0x690` through `0x890`.

**`isp_startup_Isp_PkgEnable`** (3 writes, already matched the pre-existing code exactly):
register `0x100 = 4`, register `0x034 = 5`, then `0x100 |= 1`.

**`isp_module_bypass`** (1 conditional write, trigger unresolved): register `0x02C |= 0x400`,
only when the caller's parameter is 0. That parameter's real runtime value isn't resolvable from
this file alone (the call site's apparent 12 arguments are a decompiler artifact — the function
body itself takes exactly one parameter). `isp_apply_module_bypass()` assumes the 0 path
unconditionally; flagged as an assumption, not a resolved fact.

## What this does and doesn't fix

Byte-exact register values reduce the chance a *wrong value* causes a hang, a corrupted image, or
undefined hardware behavior. They do **not** resolve the separate, still-untested question of
whether writing any ISP register via `/dev/mem` while `isp.ko` is loaded is safe at all — the
memory-attribute-mismatch/IRQ-race risk this project has never settled. That risk is about the
*access mechanism*, independent of what value is being written. Only real hardware can resolve it.
