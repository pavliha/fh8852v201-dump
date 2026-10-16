# VENC (Encoder) ioctl Protocol — FH8852V201

Derived 2026-10-17 directly from V201's own `ipcam` binary (Ghidra 12.1.4, REA). All codes and
struct offsets below come from decompiling the real `_VENC_*`/`fh_venc_*`/`_fh_sys_*` functions,
not from guessing or from sibling-chip documentation.

## The actual finding

`PORTING_GUIDE.md` (option B) listed 3 VENC ioctl codes with no struct layout, sourced from
general Fullhan SDK convention, never decompiled. Decompiling `ipcam`'s real VENC call chain
confirms all 3 of those codes were correct guesses, and surfaces the architecture around them:

- **`FH_VENC_*` functions are not thin ioctl wrappers.** Each dispatches through a per-channel
  function-pointer vtable at `g_venc + chan*0xfc`, populated once by `_create_venc_info()` with
  pointers to internal `_VENC_*` functions in the same binary. The ioctls live one level deeper,
  inside those `_VENC_*` functions.
- **VENC ioctls go on `/dev/enc`'s fd, not `/dev/isp`'s.** `fh_enc_init()` opens `/dev/enc` into
  a global handle (`fh_dev_open(g_enc, "/dev/enc")`) and every `_VENC_*` ioctl uses that same fd
  (passed as the vtable dispatcher's first argument) — there is no per-channel fd.
- Two sys-level ioctls (`0xc0085401`/`0xc00c5400`) exist for a shared VENC memory pool, same
  pattern as VPSS's `SET_SYS_ADDR`/`GET_SYS_SIZE`, and are **not** in `PORTING_GUIDE.md` at all.
- `GetStream`'s real path is `FH_VENC_GetStream` → `_fh_sys_get_stream` → single ioctl
  `0xc1a04d05` (confirmed identical to the guide's guess), then `fh_venc_handle_stream` →
  `_VENC_HandleStream`, which copies the kernel's pack-list into the caller's output struct —
  the output struct layout below was never previously derived at all.

## Call sequence

```
fh_enc_init()
  fh_dev_open(g_enc, "/dev/enc")          // one shared fd for all channels

_VENC_SysInit(g_enc)                       // once, before any channel
  ioctl(enc_fd, 0xc0085401, &{size, cached})      // GET_SYS_SIZE
  buffer_malloc_withname(size, align=8, "venc-sys")  // vendor: shared VMM pool; this code: own vmm_alloc
  ioctl(enc_fd, 0xc00c5400, &{phys, 0, size})     // SET_SYS_ADDR

_VENC_QueryChnMem(g_enc, chan, width, height, codec, &size_out)
  ioctl(enc_fd, 0xc0145402, &{chan, <out:size>, width, height, codec})  // QUERY_CHN_MEM

_VENC_CreateChn(g_enc, chan, width, height, codec, buf_phys, buf_virt, buf_size)
  ioctl(enc_fd, 0xc01c5403, &{chan, buf_phys, buf_virt, buf_size, width, height, codec})  // 28 bytes

_VENC_SetRCAttr(g_enc, chan, rc_params)
  ioctl(enc_fd, 0xc06c540c, &rc_struct)    // 104 bytes, layout depends on rc_mode (see below)

_VENC_StartRecvPic(g_enc)
  ioctl(enc_fd, 0xc0045407)                // no struct argument at all

// per frame:
FH_VENC_GetStream(chan, out_buf)
  _fh_sys_get_stream({chan})
    ioctl(enc_fd, 0xc1a04d05, &{chan, ...})   // request word[0]=chan; response per layout below
  fh_venc_handle_stream(...)
    _VENC_HandleStream(...)                   // builds out_buf from the raw ioctl response

_VENC_ReleaseStream(handle)
  ioctl(enc_fd, 0xc004540a, &handle)
```

## `CreateChn` struct — `0xC01C5403`, 28 bytes

Confirmed from `_VENC_CreateChn`'s local-variable frame (7 words, no guessing):

| Offset | Field      | Source                          |
|--------|------------|----------------------------------|
| 0x00   | `chan`     | `param_2` (caller's channel id) |
| 0x04   | `buf_phys` | `param_6`                        |
| 0x08   | `buf_virt` | `param_7`                        |
| 0x0c   | `buf_size` | `param_8`                        |
| 0x10   | `width`    | `param_3`                        |
| 0x14   | `height`   | `param_4`                        |
| 0x18   | `codec`    | `param_5` (0 = H.264, 1 = H.265) |

Vendor pulls `buf_phys`/`buf_size` from `isp_core`'s shared VMM pool via `buffer_malloc_withname`.
This project's `mini_streamer.c` uses an independent `vmm_alloc()` call instead — same underlying
`VMM_IOCTL_ALLOC`, different (simpler) bookkeeping, functionally equivalent.

## `SetRCAttr` struct — `0xC06C540C`, 104 bytes

`_VENC_SetRCAttr` is one large switch on an input `rc_mode` (values 0–0xe), each branch
remapping the caller's parameters into a different field layout for a hardware RC-type value
(`hw_rc_type`, stored at offset 0x04: 0, 1, 2, 4, 5, or 6 depending on mode). All branches write
the same 26-word struct; only the field *sources* differ per mode.

**Mode 9's branch** (used by `mini_streamer.c` — simplest: bitrate-only, no QP tuning from the
caller) is given in full in `isp_buf_addr.c`'s sibling `venc_rc_attr_mode9_t` (see
`majestic/mini_streamer.c`). Offsets 0x00–0x64, all 26 words, with the fixed constants
(`gop=5`, `qp_min=0x1e`, `qp_max=0x22/0x20`, `qp_step=-2`, `bitrate_floor/ceil=2000/4000`, etc.)
copied verbatim from the decompiled branch — none of those are guessed.

Other modes (0,2,5,7,8,10,11,12,13,14) map to CBR/fixed-QP/other RC strategies with different
caller-parameter counts (seen up to `param_3[0x10]`, i.e. 17 input words for mode 6/0xd/0xe) —
not needed for this project and not transcribed here.

## `GetStream` request/response — `0xC1A04D05`

Request is minimal: word[0] = channel id. The kernel fills the rest of the same buffer in place
(confirmed from `FH_VENC_GetStream`'s 105-word stack buffer and `_VENC_HandleStream`'s field
writes, H.264 branch — stream type 4):

| Word(s)   | Field                                                      |
|-----------|--------------------------------------------------------------|
| `[0]`     | stream type on output (4 = H.264, 8 = H.265)                 |
| `[1]`     | stream/buffer handle — pass to `ReleaseStream`                |
| `[2]`     | nonzero iff a stream is actually available                    |
| `[3]`,`[4]`,`[5]`,`[6]` | additional per-stream fields (not needed by this project) |
| `[7]`     | pack count                                                     |
| `[8..]`   | pack array, 3 words each: `{phys_addr, length, pack_type}`     |
| `[0x44..0x4f]` | trailing stats (resolution, frame rate/100.0 as float, etc.) |

If `[2]` is 0, no stream is available and `ReleaseStream` is called internally with no data
returned — callers should treat a zero pack count the same way.

## `ReleaseStream` — `0xC004540A`

`ioctl(enc_fd, 0xc004540a, &handle)` where `handle` is the word read back at `GetStream`'s
offset `[1]`. Preceded internally by a cache-flush call (`_VENC_FLUSH_CACHE`) on the vendor
side — not replicated here since this project reads the pack data via an `/dev/mem` mmap
(uncached by nature of the mapping), not via a cached virtual pointer the vendor code expects.
