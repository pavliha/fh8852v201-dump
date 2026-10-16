# ISP DMA Buffer-Address Protocol — FH8852V201

Derived 2026-10-16 directly from V201's own `ipcam` binary (Ghidra 12.1.4, REA), cross-checked
against the **unstripped** `libispcore.so` from OpenIPC's `fullhan-osdrv-fh8852v200` package
(`~/Code/openipc/openipc-firmware/general/package/fullhan-osdrv-fh8852v200/files/lib/`), which
has real function/variable names for the same API family on a sibling chip.

## The actual finding

The months-old "ISP DMA buffers not fully configured" blocker is **not** a register-access
problem. V201's own `isp_core_set_addr_to_reg()` — the function that, on the V200/V210 sibling,
pokes buffer physical addresses directly into two mmap'd register apertures — is a no-op stub
on V201:

```c
undefined4 isp_core_set_addr_to_reg(void) { return 0; }
```

V201 never writes DMA buffer addresses into any register from userspace. All of it goes through
three ioctls on `/dev/isp`, carrying buffer physical addresses as struct fields; the kernel
driver (`isp.ko`) does the actual register writes internally. This is why `/dev/mem` writes for
this purpose were always doomed and also always unnecessary — the real, working path never
touches `/dev/mem`.

## Call sequence (from `isp_core_init` → `isp_core_alloc_buf_addr`, both fully decompiled)

```
ioctl(isp_fd, 0x40016921, &isp_core[5076])      // handshake, confirmed no-op in isp.ko's own ioctl dispatch
isp_core_init_alloc(vmm_ctx)
isp_core_get_buf_size(width, height, &local_174) // see below
  ioctl(isp_fd, 0x80306926, &isp_core[7084])     // GET; output unused by indices needed below
size_check(vmm_ctx.pool_size >= total_needed)     // fail path: 0xa008400b

// 13 named buffers, each: isp_malloc(pool, &isp_core[slot], size, align=0x80)
isp_malloc(pool, &isp_core[5340], local_174[0x11],          0x80)  // isp_mem_front
isp_malloc(pool, &isp_core[5404], core_size[0x14],           0x80)  // gosd buf
isp_malloc(pool, &isp_core[5420], isp_core[?]*0x60c8,         0x80)  // ispp stat
isp_malloc(pool, &isp_core[5436], isp_core[?]<<2,             0x80)  // ispf stat
isp_malloc(pool, &isp_core[5452], 0,                          0x80)  // ispb stat (size 0 at this res/mode)
isp_malloc(pool, &isp_core[5212], core_size[3],               0x80)  // nr3d_stat buf
isp_malloc(pool, &isp_core[5164], core_size[0],               0x80)  // nr3d_frame buf
isp_malloc(pool, &isp_core[5180], core_size[1],               0x80)  // nr3d_coef buf
isp_malloc(pool, &isp_core[5196], core_size[2],               0x80)  // nr3d_nr2d_coef buf
isp_malloc(pool, &isp_core[5372], core_size[0x13],            0x80)  // mem after nr3d buf
isp_malloc(pool, &isp_core[5228], core_size[4],               0x80)  // drc_stat0 buf
isp_malloc(pool, &isp_core[5388], core_size[0x13],            0x80)  // mem after drc buf
isp_malloc(pool, &isp_core[5356], core_size[0x12],            0x80)  // isp_mem_end

// physical-address resolution (virtual isp_malloc pointer -> phys addr)
isp_core[5896] = phys(isp_core[5340])   // isp_mem_front
isp_core[5848] = phys(isp_core[5164])   // nr3d_frame
isp_core[5852] = phys(isp_core[5180])   // nr3d_coef
isp_core[5856] = phys(isp_core[5196])   // nr3d_nr2d_coef
isp_core[5860] = phys(isp_core[5212])   // nr3d_stat
isp_core[5904] = phys(isp_core[5372])   // mem after nr3d
isp_core[5864] = phys(isp_core[5228])   // drc_stat0
isp_core[5908] = phys(isp_core[5388])   // mem after drc
isp_core[5884] = phys(isp_core[5404])   // gosd

ioctl(isp_fd, 0x40786904, &isp_core[5468])   // SET_STAT_INFO, 0x78 bytes — confirmed never populated, sent zero
ioctl(isp_fd, 0x40016920, &isp_core[5824])   // SET_DMA_ADDRS, 0x58 bytes — the 9 physical addresses below
isp_core_set_addr_to_reg()                    // no-op on V201 — confirmed, do not implement
ioctl(isp_fd, 0x80786905, &isp_core[5468])   // GET_STAT_INFO — read-back / confirm
```

`core_size[N]` = `isp_core_get_buf_size`'s output struct (21 x uint32 words). Confirmed fields
(decompiled verbatim):

| index | value |
|---|---|
| 0 | `height * round_up8((width*10+7)>>3)` |
| 1 | `(height>>2) * round_up8(((width>>2)*10+7)>>3)` |
| 2 | same as index 1 |
| 3 | `0x5000` (fixed) |
| 4 | `0x41a0` (fixed) |
| 0x11 | `0` |
| 0x12 | `0` |
| 0x13 | `0` |
| 0x14 | `0x200` |
| 0x15 | combined total (sum of rounded 0,1(x2),3,4 + `0x9400`) |

Indices 6, 8, 9, 10, 0xd are never written by `isp_core_get_buf_size` and are **not used by any
malloc size in this call path** — they feed `core_size` offsets 20/24/28/32/40, which this exact
sequence never reads. Safe to leave zero.

## The `isp_core[5824]` DMA-address struct (passed to `0x40016920` only)

9 physical addresses at these byte offsets, relative to `isp_core+5824` (confirmed via
`address_name` on every absolute offset — all 9 land inside `[5824, 5912)`, the exact 0x58-byte
span `isp.ko`'s handler for this ioctl `arm_copy_from_user`s):

| offset | buffer |
|---|---|
| 24 | nr3d_frame |
| 28 | nr3d_coef |
| 32 | nr3d_nr2d_coef |
| 36 | nr3d_stat |
| 40 | drc_stat0 |
| 60 | gosd |
| 72 | isp_mem_front |
| 80 | mem after nr3d |
| 84 | mem after drc |

Struct is exactly `isp_core[5824..5912)` (88 bytes). Everything outside these 9 slots is never
explicitly written in `isp_core_alloc_buf_addr` — zero-initialize.

**The separate `isp_core[5468]` struct, passed to `0x40786904`/`0x80786905` (SET/GET_STAT_INFO,
confirmed 0x78 = 120 bytes from isp.ko's own `arm_copy_from_user(..., 0x78, ...)`), is never
written anywhere in `ipcam` — genuinely all-zero in the vendor binary too.** These are two
separate structs; don't conflate them.

## Closed (2026-10-16, second pass)

- **`isp_core._5504_4_` / `isp_core._5544_4_`** — traced to `isp_core_set_mode()`, called early
  in `isp_core_init`. It's 3 unconditional assignments, no parameter dependency:
  `isp_core._5504_4_ = 3; isp_core._5544_4_ = 3; isp_core._5584_4_ = 3;`. So
  `ispp_stat_size = 3 * 0x60c8 = 0x12248`, `ispf_stat_size = 3 << 2 = 0xC`. Fixed constants, not
  resolution-dependent. `isp_buf_addr.c` hardcodes these (`ISPP_STAT_SIZE`, `ISPF_STAT_SIZE`),
  no longer caller-supplied.
- **`isp_core[5824]` (the `0x40016920` ioctl struct) — IS populated**, by the same 9 physical
  addresses listed above. Initially misattributed to `isp_core[5468]` instead (an indexing
  error: I'd subtracted the wrong base when converting absolute `isp_core` offsets to
  struct-relative ones). Caught and corrected by re-deriving the kernel's exact copy size for
  both ioctls (`0x58` for `0x40016920`, `0x78` for `0x40786904`/`0x80786905`) instead of assuming
  one of them — the 9 addresses' absolute offsets (5848..5908) only fit inside the `[5824,5912)`
  window, not `[5468,5588)`. See the corrected struct section above.
- From decompiling `isp.ko`'s handler for `0x40016920` directly: this 88-byte struct's bytes at
  relative offsets 24/28/32/36/40/44 get `arm_copy_from_user`'d into the kernel, then written
  **unconditionally** straight into ISP hardware registers `0x2e4, 0x2ec, 0x2fc, 0x304, 0x30c,
  0x31c, 0x418, 0x644, 0x654, 0x124, 0xaa4` (several offsets reused for 2+ registers each — a
  double/triple-buffered DMA ring pattern). Since V201's `ipcam` populates this struct with the
  real buffer physical addresses, these registers get the real DMA addresses on every real V201
  boot — this is the actual payload the whole chain exists to deliver.
- The `isp_core[5468]` SET/GET_STAT_INFO struct (0x78 bytes) is confirmed genuinely unpopulated
  in the vendor binary — all 3 xrefs to it resolve to the two ioctl call sites themselves, no
  write site anywhere. Send it all-zero; that's a match, not a gap.

No remaining unknowns block replicating this exact sequence.

## Allocation mechanism — closed (2026-10-16, third pass)

The one remaining non-literal piece (13 independent VMM allocations vs. the vendor's single
pool + bump allocator) is now also closed, by decompiling the full chain:
`isp_core_init` → `buffer_malloc_withname` → `FH_SYS_VmmAllocEx` → `FH_SYS_VmmAllocEx64`.

- `FH_SYS_VmmAllocEx64` issues the exact same ioctl our proven test harness uses
  (`0xC0506D0A`), then a second "remap" ioctl (`0xC0506D14`) purely to get a CPU-mappable
  virtual pointer — unneeded here since we only wire physical addresses into hardware, never
  touch the buffer contents ourselves.
- The pool it reserves is sized to exactly `core_size[0x15]` (the same total-size field we
  compute) at `0x10000`-byte alignment.
- `isp_malloc` (decompiled in full) is a plain bump allocator over that one pool: align the
  cursor up, check it fits, write `[phys, virt, size]`, advance the cursor. `size == 0` is a
  true no-op (output and cursor both left untouched) — matters for the `ispb_stat` call, which
  the vendor issues with size 0 at this resolution.
- `__get_phy_addr(x)` — the thing that looked like it might imply special VMM-backed linker
  placement of the `isp_core` global — turned out to be `{ return *x; }`. A trivial dereference,
  not a translation. `isp_malloc`'s phys output is already the real physical address; nothing
  about `isp_core`'s own memory placement matters.

`isp_buf_addr.c` now reserves one pool via `vmm_alloc_pool()` and sub-allocates all 13 buffers
via `isp_malloc_equiv()`, in the vendor's exact call order, with `align=0x80` for every call —
a verbatim port of the mechanism, not just the end result.

**The offset-8-vs-12 ambiguity is now closed with certainty, not a judgment call.** Decompiled
`vmm.ko`'s own kernel-side allocator (`mmz_userdev_ioctl` → `mmb_alloc`) directly: its logic is
unambiguous — the offset-12 field gets page-rounded as a byte count (`(x+0xfff)&~0xfff`), the
offset-8 field gets power-of-2-validated and clamped up to `0x1000` if smaller. So **offset 8 is
align, offset 12 is size** — confirmed from the kernel code that actually interprets these
bytes, not inferred from a caller's variable names. This also explains why
`test_minimal_regs.c`'s `va()` helper — which hardcoded offset 8 to the meaningless value `8` —
still worked on real hardware: `mmb_alloc` silently clamps any sub-4K align request up to 4K, so
it produced a valid (if accidentally so) 4K-aligned allocation. `vmm_alloc_pool()` now passes the
real alignment (`0x10000`, matching `buffer_malloc_withname`'s actual call in `isp_core_init`)
instead of that placeholder.

**The name/label strings are also now the real bytes, not invented placeholders.** Read directly
from `ipcam`: the name constant at `0x4b99bc` is `"isp"` (not `"isp_core"`); the label constant
at `0x4d1698` is 4 zero bytes, i.e. an empty string (not `"anonymous"`). Both corrected in
`vmm_alloc_pool()`. Per-buffer `isp_malloc` calls never had a name argument in the vendor code
either (confirmed from its decompiled signature) — our `isp_named_bufs_t` field names
(`nr3d_frame`, `gosd`, etc.) are C-source identifiers only, never sent anywhere; nothing to
match there.

With this, every byte this code sends to the kernel — every ioctl code, every struct size and
offset, every constant value, every string — is read from the binary, not approximated.

## Restyled to the vendor's own idiom (2026-10-17)

`isp_buf_addr.c` was rewritten to match the vendor's actual coding style, not just its protocol
bytes: a persistent global `isp_core` struct instead of parameters (the vendor never threads this
through function arguments), the real function names that survived stripping
(`isp_malloc`, `isp_core_get_buf_size`, `buffer_malloc_withname`), and the real per-branch logging
and error-code conventions — which turned out to be genuinely inconsistent in the vendor binary,
not a single uniform style:

- `isp_core_alloc_buf_addr`'s own checks (buffer-size precondition, `SET_STAT_INFO` failure)
  dual-log to `/proc/driver/trace` *and* `printf`, both with real format strings/line numbers
  (`0x20a`, `0x250`), return code `0xa008400b`.
- The `SET_ADDR` (line **599**, not `0x25f` as first transcribed — corrected) and
  `GET_STAT_INFO` (`0x260`) failures use a plainer two-`printf` style with no trace-file write,
  same `0xa008400b`.
- `isp_malloc`'s own internal failure uses yet another style (`"[ISP_CORE_ERROR]: %s LINE:%d: "`
  prefix, line `0x25`) and its own distinct code `0xa008400e` — which the vendor's own 13 call
  sites never actually check, so that code is real but unobserved in the original binary. Ours
  does check it, a deliberate safety improvement beyond fidelity.
- Fixed a real bug introduced in the process: `out[0x15]` (index 21) on a 21-element array is
  out of bounds. The vendor's real `local_174[21]` only worked because an adjacent stack
  variable (`local_120`) happened to alias that index by accident of stack layout — reproducing
  that as a literal out-of-bounds write would be undefined behavior, not fidelity. Sized the
  array to 22 instead.

## Not needed

- Any `/dev/mem` mmap or register write for buffer addresses. The whole `isp_mmap` +
  `0x400-0xE2C` register table investigation from earlier sessions is superseded for this
  purpose.
- A kernel-module shim (previously proposed to work around an attribute-mismatch theory) —
  moot, since this path never touches `/dev/mem` at all.
