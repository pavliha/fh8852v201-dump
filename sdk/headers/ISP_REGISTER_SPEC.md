# ISP Register Specification — FH8852V201

Reverse-engineered from Ghidra decompilation of ipcam binary functions.
Each register's purpose is derived from the function name and parameters.

## ISP Hardware Register Base: 0xE8400000

Size: 0x3240 bytes (12,864 bytes). Accessed via mmap on /dev/mem.

## Register Map

### Identity (Read-Only)
| Offset | Name | Description |
|--------|------|-------------|
| 0x000 | ISP_ID | Chip ID: 0x5A542093 |

### Module Enable
| Offset | Name | Description | Live Value |
|--------|------|-------------|------------|
| 0x010 | MODULE_EN | Module enable mask | 0x0003FFFF |
| 0x014 | MODULE_EN2 | Secondary enable | 0x00001FFF |
| 0x018 | MODULE_EN3 | Tertiary enable | 0x0007FFFF |

From `isp_startup_Ispp_Init`: `regs[0x008] = 0x07FFFFFF` enables all modules.
Live camera uses 0x0003FFFF (fewer modules).

### ISP Configuration
| Offset | Name | Source Function | Description |
|--------|------|-----------------|-------------|
| 0x028 | BAYER_CFG | `isp_startup_IspInPicCfg` | Bayer order [1:0], mirror [3:2], data format. Live=0x0218C200 |
| 0x02C | ISP_GAIN | `FHAdv_Isp_Init` | ISP gain: 0x80 = 1x |
| 0x030 | ISP_CONFIG | Critical config | 0x00FEFF82 — meaning derived from common_init |
| 0x034 | ISP_ENABLE | `isp_startup_Isp_PkgEnable` | Processing enable. Write 5 or 9. |
| 0x044 | AE_MODE | `isp_core_set_mode` | AE mode: 3 |

### Picture Size
| Offset | Name | Source Function | Formula |
|--------|------|-----------------|---------|
| 0x0D0 | PIC_SIZE | `SetIspPicSize` | `(width-1) \| (height-1) << 16` |
| 0x130 | INPUT_SIZE | `SetInputPicSize` | `(width-1) \| (height-1) << 16` |
| 0x134 | PIC_CUT | `SetIspPicCut` | `x_offset \| y_offset << 16` |
| 0x138 | ISP_SIZE | `SetIspPicSize` | Same as PIC_SIZE |
| 0x144 | INPUT_SIZE2 | `SetInputPicSize` | Duplicate of INPUT_SIZE |
| 0x14C | SUB_SIZE | `SetIspPicSize` | Sub-resolution: `(sub_w-1) \| (sub_h-1) << 16` |

For 1920x1080:
- PIC_SIZE = `(1920-1) | (1080-1) << 16` = 0x0437077F
- SUB_SIZE: live shows 0x01DF027F = `(640-1) | (480-1) << 16` = 640x480

### Tone Mapping Curves
| Offset | Name | Source | Description |
|--------|------|--------|-------------|
| 0x080-0x08C | TONEMAP_1 | `isp_startup_Ispp_Init` | Linear tone curve 1 (4 regs) |
| 0x090-0x09C | GAMMA_PRE | `isp_startup_Ispp_Init` | Gamma pre-curve (4 regs) |
| 0x0A0-0x0B0 | TONEMAP_2 | `isp_startup_Ispp_Init` | Tone curve 2 (5 regs) |

### ISP Filter (Ispf)
| Offset | Name | Source | Description |
|--------|------|--------|-------------|
| 0x0E0 | ISPF_CTRL | `isp_startup_Ispf_Init` | Filter control: 0x00400000 |
| 0x0E4 | ISPF_CFG | `isp_startup_Ispf_Init` | Filter config (OR with bit 1) |
| 0x0E8 | ISPF_SIZE | Derived | `(height-1) | ((width/(48))-1) << 16` ≈ 0x00437437 |

### ISP Processing Control
| Offset | Name | Source | Description |
|--------|------|--------|-------------|
| 0x0F0 | ISP_PROC_CTRL | `isp_core_system_reset` | Processing control. Bit 4 = soft reset |
| 0x0F4 | ISP_PROC_CFG | `isp_core_system_reset` | Processing config. Bit 1 = reset |
| 0x100 | PKG_ENABLE | `isp_startup_Isp_PkgEnable` | Package enable. Write 4, then OR 1 to start |
| 0x104 | DEBUG_PAT1 | Init marker | 0xFEDCBA98 — indicates ISP initialized |
| 0x108 | DEBUG_PAT2 | Init marker | 0x76543210 |
| 0x114 | PROC_MODE | | 0x00000C01 |

### Buffer Configuration
| Offset | Name | Source | Description |
|--------|------|--------|-------------|
| 0x2E8 | RAW_STRIDE | `SetIspPicSize` | `((width*10+7)/8 + 7) & ~7` |
| 0x2F0 | RAW_STRIDE_EN | `SetIspPicSize` | RAW_STRIDE \| 0x80000000 |
| 0x2F8 | RAW_BUF_ADDR | `SetIspPicSize` | RAW buffer physical address |
| 0x300 | RAW_BUF_ADDR2 | | Second buffer \| 0x80000000 |
| 0x308-0x320 | Q_BUF | | Quarter-resolution buffers |

### Statistics Configuration
| Offset | Name | Source | Description |
|--------|------|--------|-------------|
| 0x490 | BLK_SIZE | `SetIspPicSize` | `(h/32-1)<<24 \| (w/48-1)<<16` |
| 0x494 | STAT_CFG | `isp_startup_Ispp_Init` | Statistics config: 0x9015 |
| 0x4E0 | AE_CFG | `SetIspPicSize` | AE window config |
| 0x500-0x508 | NR3D_WIN | `SetNr3dDrcWin` | NR3D window config |

### AWB (Auto White Balance)
| Offset | Name | Source | Description |
|--------|------|--------|-------------|
| 0x230 | AWB_GAIN_RG | `FHAdv_Isp_Init` | R/G gain: 0x02000200 = 1x |
| 0x234 | AWB_GAIN_BG | | B/G gain: 0x02000200 = 1x |
| 0x250 | AWB_MODE | | AWB mode: 2 |

### Sharpness / APC
| Offset | Name | Source | Description |
|--------|------|--------|-------------|
| 0x2AC | APC_CFG1 | | Buffer config: 0x000F1F1F |
| 0x2B0 | APC_CFG2 | | 0x00470029 |
| 0x2B4-0x2BC | APC_PARAMS | | Sharpness parameters |
| 0x2C0-0x2C8 | APC_CURVE | | Sharpness curve |

### CCM (Color Correction Matrix)
| Offset | Name | Source | Description |
|--------|------|--------|-------------|
| 0xA30 | CCM_R | `isp_startup_Ispf_Init` | R coefficient: 0x200 = 1x (identity) |
| 0xA34 | CCM_R_OFF | | R offset: 0 |
| 0xA38 | CCM_G | | G coefficient: 0x200 = 1x |
| 0xA3C | CCM_G_OFF | | G offset: 0 |
| 0xA40 | CCM_B | | B coefficient: 0x200 = 1x |
| 0xA44 | CCM_B_OFF | | B offset: 0 |

## ISP Init Sequence (from `isp_core_init` decompilation)

```
1. ioctl(isp_fd, 0x690A)            — kernel isp_start
2. mmap(0xE8400000, 0x3240)         — map ISP registers

3. isp_core_system_reset:
   regs[0x0F0] = (regs[0x0F0] & 0xCF) | 0x10   — soft reset
   regs[0x0F4] = (regs[0x0F4] & 0xFC) | 0x02

4. isp_startup_Ispp_Init:
   regs[0x008] = 0x07FFFFFF                     — enable all modules
   regs[0x080..0x0B8] = tone mapping curves
   regs[0x494] = 0x9015                          — statistics config
   Clear: regs[0x13C,0x140,0x150,0x154,0x578,0x57C] = 0

5. isp_startup_Ispf_Init:
   regs[0x0E0] = 0x400102                        — filter control
   regs[0x0E4] |= 2                              — enable filter
   regs[0x400..0x414] = AWB filter params
   regs[0x1C4] = 0x0C
   regs[0x510..0x520] = NR3D filter tables
   regs[0x538..0x57C] = NR window params
   regs[0xC2C..0xC70] = advanced NR params
   regs[0xAAC..0xB48] = sharpness LUTs
   regs[0xE00..0xE2C] = dehaze params
   regs[0xA30..0xA44] = CCM identity matrix
   regs[0xA60..0xA78] = color saturation
   regs[0x250..0x298] = AWB/gamma params
   regs[0x2C0..0x2C8] = sharpness curves
   regs[0x590..0x620] = gamma curves
   regs[0x664..0x66C] = post-processing

6. isp_startup_IspInPicCfg:
   SetInputPicSize: regs[0x130,0x144] = (w-1)|(h-1)<<16
   SetIspPicSize:   regs[0x138,0x14C,0x0D0,...] = size + buffer strides
   SetIspPicCut:    regs[0x134,0x148] = crop offsets
   SetLscTab:       lens shading correction table
   SetNr3dDrcWin:   regs[0x500,0x504,0x508]
   SetNr3dTab:      NR3D lookup tables
   SetCnrExpTab:    chroma NR tables
   SetDehazeTab:    dehaze tables
   regs[0x028] = Bayer order config

7. isp_startup_Isp_PkgEnable:
   regs[0x100] = 4         — set mode
   regs[0x034] = 5         — enable processing
   regs[0x100] |= 1        — START!
```

## Safe Writing Protocol

The vendor SDK coordinates ISP register writes with the kernel interrupt
handler via the `offset 0x364` flag in the ISP core struct:

1. `isp_start` (ioctl 0x690A) sets `offset 0x364 = 1` (starting up)
2. On first ISP interrupt, `init_isp` checks `0x364 == 1`:
   - Initializes triple buffers
   - Sets `0x364 = 0` (normal mode)
   - Reads `regs[0x138]` for width
3. Subsequent interrupts process frames normally

The register writes in steps 3-7 must complete BEFORE the first ISP
interrupt fires. The vendor SDK writes all registers, THEN enables
the ISP via PkgEnable (step 7), which triggers the first interrupt.

## Implementation Note

Writing these registers via `/dev/mem` from a separate process while
the kernel ISP module is active causes race conditions and kernel panics.
The writes must be done by the same process that owns the ISP device,
using a mmap obtained through the SDK's init path.
