# Live Video Configuration

Extracted from running ipcam.conf on KEVIEW H43 (P05H).

## Encoder Channels

### Channel 0 — Main Stream
| Parameter | Value | Notes |
|-----------|-------|-------|
| Codec | H.265 (venc_type0=0) | Type 0 = auto (H.265 preferred) |
| Resolution | 1920×1080 | Full HD |
| Frame rate | 25 fps | PAL standard |
| GOP | 4 seconds | Keyframe every 4s (100 frames) |
| RC mode | 1 (VBR) | Variable bitrate |
| Bitrate | 810 kbps | Target bitrate |
| Smart encode | 0 (off) | Scene-adaptive encoding |
| Quality | 6 | Quality level (1-9) |

### Channel 1 — Sub Stream
| Parameter | Value | Notes |
|-----------|-------|-------|
| Codec | H.265 (venc_type1=0) | |
| Resolution | 704×576 | D1/PAL |
| Frame rate | 25 fps | |
| GOP | 4 seconds | |
| RC mode | 1 (VBR) | |
| Bitrate | 512 kbps | |
| Smart encode | 0 (off) | |
| Quality | 6 | |

## Audio
| Parameter | Value | Notes |
|-----------|-------|-------|
| Enabled | Yes | |
| Codec | 4 (AAC) | aenc_type=4 |
| Sample rate | 8000 Hz | 8 kHz narrowband |
| Bit width | 1 (16-bit) | |
| Input volume | 80% | |
| Output volume | 100% | |

## venc_type Values (from binary analysis)
| Value | Codec |
|-------|-------|
| 0 | Auto (H.265 preferred on FH8852V201) |
| 1 | H.264 |
| 2 | H.265 |

## venc_rcmode Values
| Value | Mode |
|-------|------|
| 0 | CBR (Constant Bitrate) |
| 1 | VBR (Variable Bitrate) |
| 2 | Fixed QP |

## aenc_type Values
| Value | Codec |
|-------|-------|
| 0 | G.711 A-law |
| 1 | G.711 μ-law |
| 4 | AAC |
