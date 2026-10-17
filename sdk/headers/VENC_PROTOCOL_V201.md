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
- `GetStream` is a sys-level ioctl: `FH_VENC_GetStream(type_mask, out)` → `_fh_sys_get_stream` →
  `0xc1a04d05` on `/dev/media_process`, then `_VENC_HandleStream` converts the raw reply into
  `FH_VENC_STREAM`. Its first argument is a stream-type mask, not a channel (see below).

## Call sequence

```
open("/dev/enc"), open("/dev/media_process")

_VENC_SysInit                                  // once
  ioctl(enc, 0xc0085401, {size, cached})       // GET_SYS_SIZE
  ioctl(enc, 0xc00c5400, {phys, 0, size})      // SET_SYS_ADDR

per channel:
  ioctl(enc, 0xc0145402, {chan, <size>, w, h, codec})              // QUERY_CHN_MEM
  ioctl(enc, 0xc01c5403, {chan, phys, virt, size, w, h, codec})    // CREATE_CHN
  ioctl(enc, 0xc0485405, channel config)                           // SET_CHN_CONFIG, registers object (4, chan)
  ioctl(enc, 0xc06c540c, rc)                                       // SET_RC_ATTR
  ioctl(media, 0xc0104d00, {2, vpu_chan, 4, chan})                 // FH_SYS_BindVpu2Enc
  ioctl(enc, 0xc0045407, &chan)                                    // START_RECV_PIC

per frame:
  ioctl(media, 0xc1a04d05, {4 /* H.264 mask */, ...})              // GET_STREAM, any channel
  ioctl(enc, 0xc004540a, &reply_chan)                              // RELEASE_STREAM

on demand:
  ioctl(enc, 0xc004540b, &chan)                                    // REQUEST_IDR
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

## `GetStream` request/response — `0xC1A04D05` (corrected 2026-10-17)

The earlier version of this section described `FH_VENC_STREAM`, the struct `_VENC_HandleStream`
builds for callers, not the raw ioctl buffer. The raw layout below comes from `media_process.ko`
(`media_ioctl` → `media_query_stream` → `media_driver_query`) and `enc.ko` (`hhe_fw_get_result`,
which queues `channel + 0xf88` as the per-frame record), cross-checked against how ipcam's
`_VENC_HandleStream` reads it.

- Issued on `/dev/media_process` (ipcam's `g_sys`), not `/dev/enc`. `0xC1A04D06` is the same call
  with a 100 ms wait (`FH_VENC_GetStream_Block`).
- Word 0 is a **stream-type mask**, not a channel: 1 = JPEG, 2 = MJPEG, 4 = H.264, 8 = H.265. One
  call returns the next frame of any channel of that type; the reply says which channel.
- The kernel writes the matching type to word 1 and copies the 0x198-byte record from word 2.

| Word | Field |
|------|-------|
| 0 | in: stream-type mask |
| 1 | out: stream type (4 / 8) |
| 2 | record type (same) |
| 3 | encoder channel — pass `&channel` to `ReleaseStream` |
| 4 | NAL count |
| 5 | frame type |
| 6 | stream buffer physical base |
| 7 | stream buffer app virtual base (from `CreateChn`'s `buf_virt`) |
| 8 | frame size in bytes |
| 10, 11 | PTS, 64-bit, low word first, microseconds (`pts_clk` is 1 MHz on this camera) |
| 12.. | NAL entries, 4 words each: `{flag (1 for H.264), phys, app vaddr, length}` (at most 20 before the stats block at byte 0x170) |

## Channel config — `0xC0485405`, 72 bytes

`_VENC_SetChnAttr` sends this between `CreateChn` and `SetRCAttr`; `enc.ko`'s
`hhe_set_chn_config` handles it and registers the channel as media object `(4, channel)`, which
is what makes it bindable. Without it the channel never encodes.

| Word | Field |
|------|-------|
| 0 | channel |
| 1 | codec: 4 = H.264, 8 = H.265 |
| 2, 3 | width, height |
| 4, 5 | width, height again |
| 6 | GOP in frames (must be nonzero) |
| 7 | profile: 66 / 77 / 100 for H.264, 1 for H.265 |
| 8 | initial QP (ipcam: 35, must be < 52) |
| 9 | frame count \| frame time << 16 (ipcam: `25 \| 1 << 16`) |
| 10–17 | 0 |

## Bind VPSS → VENC — `0xC0104D00` on `/dev/media_process`

`FH_SYS_BindVpu2Enc(vpu_chan, enc_chan)` → `{2 /* VPU object */, vpu_chan, 4 /* encoder object */, enc_chan}`.

## `StartRecvPic` / `RequestIDR` / `ReleaseStream`

All three take `&channel` on `/dev/enc`: `0xC0045407`, `0xC004540B`, `0xC004540A`.

