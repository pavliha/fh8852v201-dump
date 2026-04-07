# KEVIEW H43 Firmware Dump — Fullhan FH8852V201

Full flash dump from a KEVIEW H43 IP camera for OpenIPC development.

## Repository Structure

```
firmware/
  partitions/          # Flash partition images (uboot, kernel, appfs, config, data)
  modules/             # Kernel modules (.ko) — ISP, encoder, JPEG, motor, NNA, etc.
  binaries/            # ipcam binary, run.sh, load_driver.sh, ARC firmware
sdk/
  headers/             # Reconstructed FH8852V201 SDK headers (from Ghidra)
  lib/                 # Clean-room libraries: fh_mpi, isp_init, cv2003 sensor driver
  tools/               # Standalone utilities: I2C scanner, register dumpers, smoke test
  tests/               # Reference tests (proc config discovery, GET_FRAME timeout fix)
    archive/           # 40+ experimental tests from ISP bringup campaign (all failed)
runtime/               # Live config captures: ipcam.conf, hw.conf, GPIO state, sensor regs
majestic/              # OpenIPC Majestic streamer porting files
```

## Hardware Specs

| Spec | Detail |
|------|--------|
| **Model** | KEVIEW H43 (product code: P05H) |
| **SoC** | Fullhan FH8852V201 |
| **CPU** | ARM Cortex-A7, hard-float, NEON VFPv4 |
| **Co-processor** | ARC core running RT-Thread RTOS (loaded at 0xBFF80000) |
| **RAM** | ~44MB total (28MB MMZ at 0xA2480000 for video, ~9.4MB for Linux) |
| **Flash** | 8MB SPI NOR |
| **Sensor** | CV2003 (MIPI, 1-lane, 1080p@30fps) |
| **Video encoding** | H.264/H.265 hardware encoder, up to 1080p@30fps |
| **JPEG** | Hardware JPEG encoder |
| **AI/NNA** | Neural network accelerator (person/face detection) |
| **PTZ** | 2x stepper motors (pan/tilt), GPIO 13 for sensor reset |
| **Ethernet** | 100M (RMII, FH_EMAC_0) |
| **WiFi** | Multiple chip support (8188fu, hgics, ssv6x5x, aic8800, atbm603x) |
| **IR** | Dual lamp (IR + white LED), auto day/night switching |

## Software

| Component | Version |
|-----------|---------|
| **Firmware** | V1.14.48-20240903 |
| **Bootloader** | U-Boot 2010.06 (Dec 07 2022) |
| **Kernel** | Linux 4.9.129 (Dec 05 2023), preempt, ARMv7 |
| **libc** | uClibc 0.9.33.2 |
| **BusyBox** | 1.26.2 (Mar 03 2022) |
| **Web server** | thttpd (port 80) |
| **TLS** | wolfSSL 4.8.1 (TLS 1.3, AES-GCM, ChaCha20-Poly1305, ECC) |
| **HTTP client** | libcurl |
| **XML** | TinyXML |
| **Thread pool** | Apache APR 1.6.3 / APR-util 1.6.1 |
| **Cloud storage** | Tencent COS C SDK |
| **Cross-compiler** | `arm-fullhanv3-linux-uclibcgnueabi-gcc` |

## Build Info

| Detail | Value |
|--------|-------|
| **Compiler flags** | `-mcpu=cortex-a7 -mfloat-abi=hard -mfpu=neon-vfpv4 -Os` |
| **Cross-compiler** | `arm-fullhanv3-linux-uclibcgnueabi-gcc` |
| **ABI** | EABI5, hard-float |
| **Kernel vermagic** | `4.9.129 preempt mod_unload ARMv7 p2v8` |
| **wolfSSL prefix** | `/home/wilbur/workspace/tools/wolfssl-4.8.1/build-fh8852v201/_install` |
| **SVN repo** | `/mnt/hgfs/SVNIPC/ipc_release/` (VMware shared folder) |
| **ONVIF source** | `/mnt/hgfs/ecs/svn/onvif_server/` |

### Developer Build Paths

| Developer | Path | Component |
|-----------|------|-----------|
| `lwj` | `/home/lwj/work/avsdk/` | P2P SDK (xiaocao/pprpc), cloud storage |
| `lwj` | `/home/lwj/work/ipc-sdk/` | APR thread pool, third-party libs |
| `lwj` | `/home/lwj/work/cloud_storage/` | Tencent COS integration |
| `trx` | `/home/trx/WORK/onvif_server/` | ONVIF server (C++) |
| `trx` | `/home/trx/WORK/libasf/` | ASF recording library |
| `wilbur` | `/home/wilbur/workspace/tools/` | wolfSSL cross-compilation |
| (shared) | `/mnt/hgfs/SVNIPC/ipc_release/` | Main firmware SVN repo |
| (shared) | `/mnt/hgfs/ecs/svn/` | ONVIF + TinyXML |
| (shared) | `/mnt/hgfs/htl/personal/gengzhiwei/` | Tiny streams service |

## ipcam Binary Analysis

Main monolithic application handling all camera functions.

| Property | Value |
|----------|-------|
| **Type** | ELF 32-bit LSB executable, ARM EABI5, dynamically linked |
| **Interpreter** | `/lib/ld-uClibc.so.0` |
| **Stripped** | Yes |
| **Entry point** | `0x9ED44` |
| **Text segment** | 5.7MB (3,940,596 bytes) |
| **BSS (runtime)** | 1.6MB (1,699,404 bytes) |
| **Data** | 58K |
| **Total in memory** | ~7.4MB |
| **Dynamic symbols** | 10,524 total (9,125 exported functions) |

### Shared Library Dependencies

```
libpthread.so.0    — POSIX threads
libm.so.0          — Math library
libdl.so.0         — Dynamic loading
librt.so.0         — Realtime extensions
libstdc++.so.6     — C++ standard library
libresolv.so.0     — DNS resolver
libgcc_s.so.1      — GCC runtime
libc.so.0          — uClibc
```

### Hardcoded Credentials

| Credential | Value | Context |
|------------|-------|---------|
| `SuperPasswd` | string in binary | Super admin password (likely factory reset) |
| `SuperPasswd01` | string in binary | Alternative super admin password |
| Root password hash | `8dxMkZjXi01sk` (DES-crypt) | `/etc/passwd`, decrypts to `ipc@hs66` |
| Default web login | `admin` / `123456` | Web UI and RTSP |
| GB28181 password | `12345678` | `ipcam.conf` |

### Phone-Home URLs

| URL | Purpose |
|-----|---------|
| `https://home.360.cn/a/d?pk=%s&dn=%s` | Qihoo 360 P2P device registration |
| `https://%s%s/%s/%d/%s/%llu.xc` | Cloud storage upload (Tencent COS) |
| `http://%s:%d/cgi-bin/snapshot.cgi` | Remote snapshot endpoint |
| `cos.myqcloud.com` | Tencent Cloud object storage |
| `cloud.vatilon.com` | Vatilon vendor cloud |
| `private.vatilon.com:12380` | Web player plugin CDN |

### Hardcoded IPs

| IP | Purpose |
|----|---------|
| `114.114.114.114` | Chinese public DNS (like 8.8.8.8) |
| `8.8.8.8` | Google DNS |
| `14.215.177.39` | Baidu IP (likely connectivity check) |
| `192.168.1.88` | Default static IP |
| `10.81.81.81` | U-Boot default IP |
| `239.255.255.250` | SSDP/UPnP multicast |

## Boot Sequence

```
U-Boot → reads kernel from SPI flash offset 0x40000
       → loads to RAM at 0xA1000000
       → bootargs: mem=37369K console=ttyS0,115200 quiet
       → Linux 4.9.129 boots

init → /usr/app/run.sh
     → mount jffs2 config partition
     → insmod vmm.ko (28MB video memory)
     → GPIO 13 low (reset sensor)
     → insmod xbus_rpc.ko (loads rtthread_arc.bin to ARC core at 0xBFF80000)
     → GPIO 13 high (release sensor)
     → insmod media_process.ko → isp.ko → enc.ko → jpeg.ko → nna.ko
     → start telnetd on port 2360
     → copy ipcam to /tmp, decompress if .lzma
     → run /tmp/ipcam (main application)
     → reboot if ipcam exits (watchdog behavior)
```

## Network Services

| Port | Service |
|------|---------|
| 80 | HTTP (thttpd — web UI + ONVIF) |
| 554 | RTSP (2 streams per channel) |
| 2360 | Telnet (BusyBox ash) |
| 9101 | WebSocket (video player) |

## RTSP URL Format (decompiled)

```
rtsp://<ip>:<port>/stream<N>
```

Where `N` = channel + 1. Default port from `DAT_0066081c` (554).
- `rtsp://<ip>:554/stream1` — main stream (channel 0)
- `rtsp://<ip>:554/stream2` — sub stream (channel 1)

## Watchdog (decompiled)

```
Device: /dev/watchdog
Timeout: 30 seconds (0x1E)
Mode: 2 (auto-reset on timeout)

IOCTL codes:
  0xC0045706  — Set timeout (WDIOC_SETTIMEOUT)
  0x80045704  — Set options (WDIOC_SETOPTIONS)
  0x80045705  — Keepalive/feed (WDIOC_KEEPALIVE)
```

If `ipcam` crashes or hangs (stops calling `m_wdt_feed()`), the hardware watchdog reboots the camera after 30 seconds. The `run.sh` script also reboots if `ipcam` exits normally.

## Cellular/4G Support (decompiled)

The camera supports cellular modems when `hwinfo[0x15c] & 8` is set:
- Detects SIM card (dual SIM support, tries both slots)
- Configures modem via AT commands
- Falls back between SIM 0 and SIM 1
- Modem accessed via `/dev/ttyUSB*` or similar

## Firmware Recovery Format (decompiled from `m_firmware_backup`)

The camera creates a recovery ROM on SD card at `<sdcard>/<product>_recovery.rom`.

```
Recovery ROM layout:
  [0x00] Magic: 0x48465548 ("HFUH" — reversed "HUFH")
  [0x04] Version: 0xA0000001
  [0x08] Header CRC32 (calculated over 0x220 bytes)
  [0x0C] Build date string
  [0x18] Firmware version number (parsed from "V1.14.48")
  [0x1C] Partition count: 1
  [0x20] Product name (16 bytes)
  [0x30] Flash type: "flash"

  Partition table (8 entries × 0x38 bytes each):
    [+0x00] Name (8 bytes): "uboot", "kernel", "appfs"
    [+0x08] MTD device (32 bytes): "/dev/mtd0", etc.
    [+0x28] Flash offset
    [+0x2C] Size
    [+0x30] Version number
    [+0x34] Valid flag (1)

  Data: raw MTD partition dumps concatenated
  CRC32 of all data appended
```

The recovery skips "custom" and "config" partitions — only backs up uboot, kernel, and appfs. Data is read from `/dev/mtdN` in 64KB blocks with `utl_calc_crc32`.

## System Reset (decompiled)

```
system_reset(0) → sends message 0x102 (soft reset, keep config)
system_reset(1) → sends message 0x103 (factory reset, clear config)
system_reboot() → sends message 0x100 (reboot)
```

All via `utl_msg_queue_push(hwinfo._276_4_, msg_id, 0, 1)` — the system message queue at hwinfo offset 276.

## UART Console

```
Baudrate: 115200
Console: ttyS0
Boot delay: 1 second
```

## CPU Info

```
Processor:    ARMv7 Processor rev 5 (v7l)
CPU part:     0xC07 (Cortex-A7)
Features:     half thumb fastmult vfp edsp neon vfpv3 tls vfpv4 idiva idivt vfpd32 lpae
Hardware:     FH8852V201
MemTotal:     32460 kB (kernel visible)
Kernel cmdline: mem=37369K hfmask=3 console=ttyS0,115200 quiet mtdparts=spi0.0:256K(uboot),2304K(kernel),5056K(appfs),512K(config),64K(data)
```

Note: No device tree (DTB) — this kernel uses legacy board files (no `/proc/device-tree/`). No embedded FDT magic found in kernel binary.

## I/O Memory Map (`/proc/iomem`)

### System RAM

| Address Range | Size | Description |
|--------------|------|-------------|
| `0xA0000000 - 0xA247DFFF` | ~36.5MB | System RAM |
| `0xA0008000 - 0xA0413FFF` | ~4MB | Kernel code |
| `0xA04DE000 - 0xA052BC3F` | ~312K | Kernel data |
| `0xA2480000 - 0xBFF7FFFF` | ~28MB | MMZ (video memory, from vmm.ko) |
| `0xBFF80000` | — | RT-Thread ARC core load address |

### Peripheral Register Map

| Address | Size | Peripheral | IRQ | Driver |
|---------|------|-----------|-----|--------|
| `0xE0300000` | 16K | AXI DMA | 55 | `fh-axi-dmac` |
| `0xE0600000` | 16K | Ethernet MAC (GMAC) | 76 | `fh_gmac.0` |
| `0xE0700000` | 1MB | USB 2.0 OTG | 71 | `fh_usb.0` / `dwc2_hsotg` |
| `0xE8200000` | 16K | AES crypto engine | 48 | `fh-aes` |
| `0xF0000000` | 16K | Performance monitor | 40 | `fh_perf` |
| `0xF0100000` | 16K | I2C bus 2 | 78 | `fh_i2c.2` |
| `0xF0200000` | 16K | I2C bus 0 (sensor) | 43 | `fh_i2c.0` |
| `0xF0300000` | 16K | GPIO bank 0 | — | `FH_GPIO.0` |
| `0xF0400000` | 16K | PWM | 68 | `fh_pwm` |
| `0xF0500000` | 16K | SPI bus 0 (flash) | 60 | `fh_spi.0` |
| `0xF0600000` | 16K | SPI bus 1 | 53 | `fh_spi.1` |
| `0xF0640000` | 16K | SPI 2 memory-mapped | — | — |
| `0xF0700000` | 16K | UART 0 (console) | 62 | `fh-serial` / `ttyS0` |
| `0xF0800000` | 16K | UART 1 | — | `fh-serial` / `ttyS1` |
| `0xF0B00000` | 16K | I2C bus 1 | 44 | `fh_i2c.1` |
| `0xF0D00000` | 16K | Watchdog timer | 34 | `fh_wdt` |
| `0xF0E00000` | 16K | Stepper motor 0 (pan) | 66 | `fh_stepmotor.0` |
| `0xF0F00000` | 16K | Stepper motor 1 (tilt) | 67 | `fh_stepmotor.1` |
| `0xF1200000` | 16K | SAR ADC (light sensor) | 52 | `fh_sadc` |
| `0xF1300000` | 16K | UART 2 | — | `fh-serial` / `ttyS2` |
| `0xF1500000` | 16K | RTC | 65 | `fh_rtc` |
| `0xF1600000` | 16K | eFuse (OTP) | — | `fh_efuse` |
| `0xF4000000` | 16K | GPIO bank 1 | — | `FH_GPIO.1` |

### Video Pipeline Interrupts

| IRQ | Name | Description |
|-----|------|-------------|
| 35 | `timer0` | System timer |
| 39 | `ispp` | ISP processing pipeline |
| 41 | `veu-sw-irq` | Video encoding unit (software IRQ) |
| 42 | `NNA_IRQ` | Neural network accelerator |
| 43 | `i2c-0` | Sensor I2C bus |
| 45 | `jpeg` | JPEG encoder |
| 47 | `veu_loop` | Video encoding loop |
| 50 | `mipi_wrap_int` | MIPI CSI wrapper |
| 54 | `jpg_loop` | JPEG encoding loop |
| 64 | `xbus` | Co-processor IPC bus |

## GPIO Pin Map — P05H Model (decompiled from `p_platform_gpio_init`)

Your camera (product `P05H`) uses this GPIO assignment:

| GPIO | Offset in `gpios[]` | Function | Direction |
|------|---------------------|----------|-----------|
| 13 | `gpios[0]` (0x00) | **Sensor reset** | Output (pulse low→high→low→high) |
| 3 | `gpios[4]` (0x10) | **Light sensor / photoresistor** | Input |
| 39 (0x27) | `gpios[9]` (0x24) | **IR LED enable** | Input (read status) |
| 40 (0x28) | `gpios[11]` (0x2C) | **Speaker / alarm** | Output |

### GPIO Functions (all models)

| Offset | Field | Purpose |
|--------|-------|---------|
| `gpios[0]` | 0x00 | Sensor reset (GPIO 13) |
| `gpios[1]` | 0x04 | Sensor reset 2 (dual sensor models, GPIO 31) |
| `gpios[2]` | 0x08 | Sensor power (dual sensor models, GPIO 32) |
| `gpios[3]` | 0x0C | Sensor power 2 |
| `gpios[4]` | 0x10 | Light sensor / photoresistor (GPIO 3 or 27) |
| `gpios[5]` | 0x14 | IR LED 1 control (GPIO 0 or 4) |
| `gpios[6]` | 0x18 | IR LED 2 control (GPIO 1 or 40) |
| `gpios[7]` | 0x1C | IR cut filter (PWM channel 0) |
| `gpios[8]` | 0x20 | White LED (PWM channel 1) |
| `gpios[9]` | 0x24 | IR LED status |
| `gpios[11]` | 0x2C | Speaker enable |
| `gpios[13]` | 0x34 | IRCUT filter direction (GPIO 26) |
| `gpios[22]` | 0x58 | Status LED 1 |
| `gpios[23]` | 0x5C | Status LED 2 |
| `gpios[24]` | 0x60 | Status LED 3 |
| `gpios[26]` | 0x68 | Button input |

### IR/White LED Control

IR lamp brightness uses **PWM channel 0** (`gpios[7]`), white lamp uses **PWM channel 1** (`gpios[8]`). Brightness is 0-100% via PWM duty cycle. The `hwinfo[0x12a]` flag inverts the polarity (active-low vs active-high).

### Product Model GPIO Variants

| Model | Type | Sensor Reset | IR LED | White LED | Light Sensor | PTZ |
|-------|------|-------------|--------|-----------|-------------|-----|
| **P05H** | Wired PTZ | GPIO 13 | PWM ch0 | GPIO 40 | GPIO 3 | Yes (stepmotor) |
| P03H | Wired fixed | GPIO 13 | PWM ch0 | GPIO 40 | GPIO 3 | No |
| W03T/W03A | WiFi PTZ | GPIO 13 | GPIO 4+40 | PWM ch1 | GPIO 27 | Yes |
| WD03A | WiFi dual-sensor | GPIO 13+31+32 | GPIO 4+40 | PWM ch1 | GPIO 27 | Yes |

## SDK Init Sequence (decompiled from `p_platform_sdk_init`)

The exact Fullhan SDK initialization sequence for majestic integration:

```c
// 1. Configure media driver
_media_driver_config();
// Sets: vi_{width}_{height}, cap_0_{w}_{h}, cap_1_800_576, cap_2_512_288
// Buffers: buf_0_2, buf_1_3, buf_2_1, cirbuf_on, cbufsize_80
// Encoder: stm_1572864 (1.5MB stream buffer)
// NNA: nn_clk,enable,90000000 (90MHz NNA clock)

// 2. Init video processing
FH_SYS_Init();                           // Open /dev/media_process
FH_VPSS_SetViAttr({width, height});      // Set sensor input size
FH_VPSS_Enable(0);                       // Enable VPSS group 0
FH_VPSS_SetLDCAttr({w/2, h/2, 50});     // Lens distortion correction

// 3. Open VPSS channels (2 per sensor + 1 for NNA)
for (ch = 0; ch < num_sensors * 2; ch++) {
    FH_VPSS_SetChnAttr(ch, {width, height});
    FH_VPSS_OpenChn(ch);
    FH_VPSS_SetVOMode(ch, 1);            // Output mode
}
// Channel 2: 512×288 for NNA, frame rate control 5:1
FH_VPSS_SetChnAttr(2, {512, 288});
FH_VPSS_OpenChn(2);
FH_VPSS_SetVOMode(2, 5);
FH_VPSS_SetFramectrl(2, {5, 1});          // 1 out of 5 frames

// 4. Init ISP
FHAdv_MS_GetCurSensorCtrl();             // Get sensor control struct
API_ISP_MemInit(width, height);           // Allocate ISP memory
API_ISP_SensorRegCb(0, sensor_ctrl);      // Register sensor callbacks
API_ISP_SensorInit();                     // Initialize sensor via I2C
API_ISP_SetSensorFmt(0x203);             // Bayer format (for CV2003/GC sensors)
API_ISP_Init();                           // Initialize ISP pipeline
API_ISP_SensorKick();                     // Start sensor streaming

// 5. Init audio
FH_AC_Init();                             // Init audio codec
FH_AC_AI_Set_Algo_Param(params, 0xEA);   // Audio input algorithm (echo cancel, etc.)
FH_AC_AO_Set_Algo_Param(params, 0xEA);   // Audio output algorithm
```

## Audio Subsystem (decompiled)

**Fullhan Audio Codec (`FH_AC_*`) — 43 functions:**

| Function | Purpose |
|----------|---------|
| `FH_AC_Init` | Initialize internal audio codec |
| `FH_AC_Init_WithExternalCodec` | Init with external I2S codec |
| `FH_AC_AI_Enable` / `Disable` | Audio input on/off |
| `FH_AC_AI_GetFrame` / `GetFrameWithPts` | Capture audio frame |
| `FH_AC_AI_SetVol` / `MICIN_SetVol` | Input volume control |
| `FH_AC_AO_Enable` / `Disable` | Audio output on/off |
| `FH_AC_AO_SendFrame` | Play audio frame |
| `FH_AC_AO_SetVol` / `SetDigitalVol` | Output volume control |
| `FH_AC_AI_Set_Algo_Param` | Echo cancellation, noise reduction (param 0xEA) |
| `FH_AC_AI_Bind` | Bind audio input to encoder |
| `FH_AC_AI_ClearBuf` / `AO_ClearBuf` | Clear audio buffers |
| `FH_AC_Set_Config` | Sample rate, bit depth, channels |
| `FH_AC_Version` | Audio codec version |

Two-way audio: `m_audio_start_chatting()` / `m_audio_stop_chatting()` — enables speaker output for intercom.

## NNA (Neural Network Accelerator) — Decompiled

**Init sequence (`FH_NNA_DET_Init`):**

```c
// 1. Allocate detector context (0x460C bytes)
ctx = calloc(0x460C, 1);

// 2. Load .nbg model file
ctx->model_data = model_ptr;     // from persondet.nbg
ctx->num_classes = num_classes;   // detection classes
ctx->input_w = width;            // input dimensions
ctx->input_h = height;

// 3. Initialize NNA hardware
fh_nna_set_rotate(ctx, params);  // Handle rotation
fh_nna_cla_cmd_num(ctx);         // Calculate command count
FH_NNA_InitNet(model_data, model_size, &handles);  // Load to NNA

// 4. Process frames
FH_NNA_SubmitFrame(frame);       // Submit VPSS channel 2 frame
FH_NNA_DET_Process(ctx);         // Run inference
FH_NNA_DET_Forward(ctx);         // Get results
```

**NNA runs at 90MHz** clock, processes 512×288 frames at 1/5 sensor frame rate.

## Flash Partition Layout

```
Offset    Size    Name      Filesystem
0x000000  256K    uboot     raw (U-Boot 2010.06)
0x040000  2304K   kernel    uImage (Linux 4.9.129, uncompressed)
0x280000  5056K   appfs     squashfs v4.0, xz compressed (239 inodes)
0x780000  512K    config    jffs2 (rw, user settings)
0x7F0000  64K     data      raw (factory data)
```

## Kernel Modules

All modules: `vermagic=4.9.129 preempt mod_unload ARMv7 p2v8`

| Module | Size | Symbols | Description |
|--------|------|---------|-------------|
| `vmm.ko` | 20K | 16 | Video memory manager (MMZ at 0xA2480000, 28160K) |
| `xbus_rpc.ko` | 26K | 30 | IPC bus to ARC co-processor, firmware loader |
| `media_process.ko` | 153K | 295 | Video processing pipeline (depended on by isp, enc, jpeg, nna) |
| `isp.ko` | 214K | 326 | Image signal processor |
| `enc.ko` | 204K | 219 | H.264/H.265 video encoder (HEVC NAL/VPS/SPS/PPS) |
| `jpeg.ko` | 90K | 84 | JPEG/MJPEG encoder |
| `nna.ko` | 55K | 86 | Neural network accelerator |
| `motor.ko` | 13K | 6 | Stepper motor driver (open/close/read/write) |

### Module Dependency Tree

```
vmm.ko
└── xbus_rpc.ko (loads RT-Thread firmware)
    └── media_process.ko (core video pipeline)
        ├── isp.ko (image signal processor)
        ├── enc.ko (H.264/H.265 encoder)
        ├── jpeg.ko (JPEG encoder)
        └── nna.ko (neural network)
```

## Supported Sensors (compiled in binary)

| Sensor | Interface | Resolutions |
|--------|-----------|-------------|
| **CV2003** | MIPI 1-lane | 1080p@15/20/25/30fps — **this camera** |
| GC2053 | MIPI | 1080p |
| GC2063 | MIPI | 1080p |
| GC2083 | MIPI | 1080p |
| GC2093 | MIPI | 1080p |
| GC4201AC | — | 4MP |
| IMX307 | MIPI | 1080p |

## Key Files

| Path | Description |
|------|-------------|
| `/usr/app/run.sh` | Main startup script |
| `/usr/app/driver/load_driver.sh` | Kernel module loading + GPIO sensor reset |
| `/usr/app/driver/rtthread_arc.bin` | RT-Thread firmware for ARC core |
| `/usr/app/bin/ipcam` → `/tmp/ipcam` | Main application (6MB, stripped ELF) |
| `/usr/app/bin/thttpd` | Web server |
| `/usr/app/bin/system_wapper` | System watchdog |
| `/usr/app/www/` | Web UI files (jQuery, layui) |
| `/tmp/etc/config/ipcam.conf` | Main config |
| `/tmp/etc/config/hw.conf` | Hardware config (PTZ, lamp, GPIO) |
| `/tmp/etc/config/user.conf` | User accounts (binary format) |
| `/tmp/etc/config/sensor.conf` | Sensor config (`sensor=cv2003_mipi`) |
| `/tmp/etc/config/network.conf` | Network settings |
| `/tmp/etc/config/mac.conf` | MAC address |

## Security Issues

- **Hardcoded telnet password**: `root:ipc@hs66` on port 2360 — cannot be changed, baked in firmware
- **Telnet always enabled**: started unconditionally in `run.sh`, no config to disable
- **Super admin passwords**: `SuperPasswd` and `SuperPasswd01` hardcoded in binary
- **Plaintext credentials**: `mod=account&cmd=list` API returns passwords in plaintext
- **No HTTPS**: all traffic is unencrypted HTTP
- **Hardcoded Chinese DNS**: `114.114.114.114` as fallback DNS
- **P2P phone-home**: contacts `home.360.cn` (Qihoo 360), `cloud.vatilon.com`, `cos.myqcloud.com` (Tencent)
- **CVE-2025-63667**: session bypass (not confirmed on this firmware version)
- **CVE-2025-67159**: plaintext credential exposure via web.cgi
- **DES password hash**: uses obsolete DES-crypt for root password (trivially crackable)

## Cloud/P2P Services (baked into binary)

| Service | Chinese Name | Build Path | Purpose |
|---------|-------------|------------|---------|
| xiaocao | 小草 | `/mnt/hgfs/SVNIPC/ipc_release/p2p/xiaocao/` | P2P SDK (KCP-based, pprpc protocol) |
| halou | 哈喽 | (in ipcam binary) | Cloud recording, thumbnails, SD card status |
| qipc | — | (in ipcam binary) | Device management, AP config, encoder info |
| Qihoo 360 | 360 | `home.360.cn` | P2P relay and device registration |
| Tencent COS | 腾讯云 | `/home/lwj/work/cloud_storage/` | Cloud video storage |
| Vatilon | — | `cloud.vatilon.com` | Vendor cloud services |

## ONVIF Endpoints

```
/onvif/device_service    — Device management
/onvif/Media             — Media service v1
/onvif/Media2            — Media service v2
/onvif/Ptz               — PTZ control
/onvif/Imaging           — Imaging settings
/onvif/Events            — Event handling
/onvif/Analytics         — Video analytics
/onvif/plus              — Vendor extensions
/onvif/hik_ext           — Hikvision compatibility
```

## Access

```bash
# Telnet (root shell)
telnet <ip> 2360
# login: root / ipc@hs66

# Web UI
http://<ip>/view/login.html
# login: admin / 123456

# RTSP
rtsp://admin:123456@<ip>:554/stream0  # main stream
rtsp://admin:123456@<ip>:554/stream1  # sub stream

# UART
115200 8N1 on ttyS0

# API
curl --digest -u admin:123456 "http://<ip>/cgi-bin/web.cgi?mod=device&cmd=get"

# Disable P2P phone-home
# 1. Login to get session
SID=$(curl -s --digest -u admin:123456 -D - "http://<ip>/cgi-bin/web.cgi?mod=session&cmd=login" | grep -oP 'Session-Id: \K.*' | tr -d '\r')
# 2. Disable P2P
curl -s -H "Session-Id: $SID" -H "Content-Type: application/json" -X POST "http://<ip>/cgi-bin/web.cgi" -d '{"mod":"p2p","cmd":"set","param":{"enable":0}}'
```

## Dump Contents

| File | Size | Description |
|------|------|-------------|
| `uboot.bin` | 256K | U-Boot bootloader (mtd0) |
| `kernel.bin` | 2.3M | Linux kernel uImage (mtd1) |
| `appfs.bin` | 4.9M | Application squashfs (mtd2) |
| `config.bin` | 512K | Config jffs2 partition (mtd3) |
| `data.bin` | 64K | Factory data (mtd4) |
| `ipcam` | 5.8M | Main application binary |
| `isp.ko` | 214K | ISP kernel module |
| `enc.ko` | 204K | Video encoder module |
| `media_process.ko` | 153K | Media pipeline module |
| `jpeg.ko` | 90K | JPEG encoder module |
| `nna.ko` | 55K | Neural network module |
| `rtthread_arc.bin` | 288K | ARC co-processor firmware |
| `xbus_rpc.ko` | 26K | Co-processor IPC module |
| `vmm.ko` | 20K | Video memory manager module |
| `motor.ko` | 13K | PTZ motor driver module |
| `load_driver.sh` | 485B | Driver loading script |
| `run.sh` | 2.3K | Main startup script |

## OTA Firmware Update

The `product` file contains the OTA update path:

```
product=P05H
uboot=14
kernel=15
appfs=11448
ota_url=/fh8852v201/P05H/P05H.bin
```

The firmware upgrade mechanism (`m_firmware_upgrade.c`) downloads firmware via HTTP/HTTPS, verifies MD5, and writes to flash. OTA can be triggered via:
- ONVIF `StartFirmwareUpgrade` / `UpgradeSystemFirmware`
- P2P cloud push (`avsdk_firmware_poll`)
- Web UI upload (`upload_file.cgi`)

Upload CGI accepts firmware at `/tmp/etc/custom/config.tgz` and `/tmp/etc/custom/config_default.tgz`.

## CGI Architecture

`web.cgi` is a shell script that chains:

```
proccgi → parses HTTP query/post into FORM_* env vars
wagent  → forwards FORM_mod + FORM_cmd + params to ipcam via IPC
ipcam   → actual request handler, returns JSON
```

| CGI Binary | Purpose |
|------------|---------|
| `web.cgi` | Shell wrapper script |
| `proccgi` | HTTP parameter parser (GET/POST → FORM_* variables) |
| `wagent` | IPC bridge to main `ipcam` process |
| `snapshot.cgi` | Direct JPEG snapshot capture |
| `upload_file.cgi` | Firmware/config upload handler |

## AI / Neural Network

| File | Size | Description |
|------|------|-------------|
| `persondet.nbg` | 1.3MB | Person detection model for NNA accelerator |

The NNA (Neural Network Accelerator) hardware runs person detection and face detection via the `nna.ko` module. The model format is `.nbg` (Fullhan proprietary neural binary graph).

## ISP Tuning Files

Each sensor has 3 ISP tuning profiles (hex files with register values):

| Profile | Description |
|---------|-------------|
| `*_attr.hex` | Day mode (color, normal lighting) |
| `*_night_attr.hex` | Night mode (IR illumination, B&W) |
| `*_whitelight_attr.hex` | White LED mode (color night vision) |

Sensors with tuning files: CV2003, GC2053, GC2063, GC2083.

## Voice Prompts

Audio prompts in 3 languages (English, Chinese, tones):

| File | Description |
|------|-------------|
| `alarm0` | Alarm sound |
| `ding` | Notification tone |
| `dual_lamp` | "Dual lamp mode" announcement |
| `ir_lamp` | "IR lamp mode" announcement |
| `white_lamp` | "White lamp mode" announcement |
| `reset` | "Factory reset" announcement |

## Web UI Languages

- `en-us.js` — English
- `zh-cn.js` — Simplified Chinese
- `zh-tw.js` — Traditional Chinese
- `ko-kr.js` — Korean
- `ru-ru.js` — Russian
- `tr-tr.js` — Turkish

## Squashfs Contents (239 inodes)

```
/usr/app/
├── bin/
│   ├── ipcam.lzma         # Main app (compressed, decompressed to /tmp at boot)
│   ├── thttpd             # Web server
│   ├── system_wapper      # Watchdog
│   ├── coredt             # Core dump tool
│   ├── flash_erase        # MTD flash eraser
│   ├── mkfs.exfat         # exFAT formatter (for SD card)
│   ├── import_custom      # Import custom config
│   └── export_config      # Export config
├── cert/
│   └── ca-certificates.crt  # CA bundle for TLS
├── driver/
│   ├── *.ko               # Kernel modules
│   ├── rtthread_arc.bin   # ARC co-processor firmware
│   └── load_driver.sh    # Module loading script
├── model/
│   └── persondet.nbg      # Person detection AI model (1.3MB)
├── res/
│   ├── font.bin           # OSD font
│   └── zh-cn.lan          # Chinese language pack
├── sensor/
│   └── *_attr.hex         # ISP tuning files (day/night/whitelight × 4 sensors)
├── voice/
│   ├── en/                # English voice prompts
│   ├── zh/                # Chinese voice prompts
│   └── tone/              # Tone-only prompts
├── www/
│   ├── cgi-bin/           # CGI handlers (proccgi, wagent, web.cgi, snapshot.cgi, upload_file.cgi)
│   ├── css/               # Stylesheets
│   ├── img/               # Icons
│   ├── js/                # JavaScript (player, settings, layui framework)
│   ├── view/              # HTML pages (35 pages)
│   ├── onvif/             # ONVIF device service
│   ├── index.html         # Entry point
│   └── version            # Build timestamp: 20240814143500
├── wifi/                  # WiFi drivers and wpa_supplicant (symlinked per model)
├── network.sh             # Network configuration script
├── setmac.sh              # MAC address configuration
├── run.sh                 # Main startup script
└── product                # Product identification + OTA URL
```

## P2P Protocol Details

The P2P system uses a custom protocol stack:

- **PPRPC** — proprietary RPC over KCP (reliable UDP)
- **PPMQ** — proprietary message queue (pub/sub topics)
- **KCP** — reliable UDP transport (ikcp.c from https://github.com/skywind3000/kcp)
- **xiaocao SDK** — handles device registration, video relay, alarm notifications

The camera's P2P ID format is `PPXCAO` + device serial (e.g., `PPXCAO0AA55FD6CC15`).

## Ghidra Reverse Engineering

Decompiled using Ghidra. Source path: `/mnt/hgfs/SVNIPC/ipc_release/server/ipcam/ipcam.c`

### Vendor Identification

License magic header: `HTLK@$^%` — vendor is **HTLK** (华利科).
License data stored at flash offset `0xF000`, contains: P2P type, P2P ID, MAC address, serial number, build time.
If license validation fails (`m_authentication_request`), the binary exits immediately.

### Subsystem Init Order (from `main()`)

| Order | Function | Purpose |
|-------|----------|---------|
| 1 | `m_platform_sync_product_info` | Hardware identification |
| 2 | `sysctrl_read_hw_config` | Read hardware config |
| 3 | `m_storage_init` | SD card / storage |
| 4 | `m_authentication_read/request` | License check (exits if fails) |
| 5 | `cloud_p2p_set_attr` | Cloud P2P setup |
| 6 | `m_platform_init` | Platform SDK |
| 7 | `m_net_init` | Networking (eth/wifi/cellular) |
| 8 | `m_video_init` / `m_audio_init` | Media pipeline |
| 9 | `m_osd_init` | On-screen display |
| 10 | `m_md_init` / `m_pd_init` / `m_tp_init` | Motion/person/tamper detect |
| 11 | `m_face_detect_init` | Face detection (NNA) |
| 12 | `m_record_init` | Recording |
| 13 | `m_ptz_init` | Pan/Tilt/Zoom motors |
| 14 | `m_onvif_init` | ONVIF protocol |
| 15 | `m_rtsp_init` | RTSP server |
| 16 | `m_gb28181_init` | GB28181 (Chinese surveillance standard) |
| 17 | `weber_start` | HTTP web server (CGI) |
| 18 | `cloud_p2p_start` | Cloud P2P connection |
| 19 | `m_rtsp_start` | Start RTSP streaming |
| 20 | `m_onvif_start` | Start ONVIF |

### CGI Command Endpoints (38 registered via `web_cmd_init`)

| Command | Purpose |
|---------|---------|
| `account` | User management |
| `device` | Device info |
| `session` | Session management |
| `video` / `audio` | Media settings |
| `stream_ability` | Stream capabilities |
| `image` | Image settings (brightness, contrast, day/night) |
| `snapshot` / `snapshot_res` | Capture snapshots |
| `privacy` | Privacy mask |
| `alarm_in` / `alarm_out` / `alarm_center` | Alarm system |
| `voice_detect` | Voice detection |
| `storage` | SD card management |
| `system` | System ops (reboot, factory reset) |
| `telnet` | Telnet enable/disable |
| `gb28181` | GB28181 config |
| `onvif` | ONVIF settings |
| `auto_reboot` | Auto-reboot schedule |
| `lamp_panel` / `lamp_hwcfg` / `lamp_control` | IR/white LED control |
| `hardware_config` | Hardware configuration |
| `timing_snapshot` | Scheduled snapshots |
| `language` | Language settings |
| `systime` | System time/NTP |

### Authentication (decompiled)

**Account auth (`m_account_check_auth`):**
- Fixed array of 10 accounts, 0x48 bytes each
- Compares username + password via `strcmp` (plaintext, no hashing)
- If password field is empty → auth bypassed (returns success)
- `m_account_need_check_auth` can disable auth entirely

**RTSP auth (`rtspAuth`):**
- Supports digest authentication
- Fallback: if no Authorization header, tries "admin" account directly
- If admin has no password → RTSP access granted without credentials

**License system (`m_authentication_read`):**
- Reads 0x200 bytes from device, decrypts with `utl_decrypt_data`
- Validates magic header: `HTLK@$^%`
- Extracts: P2P type, P2P ID, MAC, serial, build time
- Product info at flash offset 0xF000

### Cloud P2P Platforms (10 supported)

`cloud_p2p_start` selects platform based on license P2P type field:

| Type | Platform | Init Function | Status |
|------|----------|---------------|--------|
| 1 | Tuya | `tuya_init` | Stub (returns -1, not implemented) |
| 2 | Dana | `dana_init` | Active |
| 3 | HuiYun | `huiyun_init` | Active |
| 4 | XiaoCao | `xiaocao_init` | Active — **this camera** |
| 5 | QIPC | `qipc_service_init` | Active |
| 6, 9 | Closeli | `closeli_init` | Active |
| 7 | IVY | `ivy_init` | Active |
| 8 | Aliyun | `aliyun_init` | Active |
| 10 | Halou | `halou_init` | Active |

### PTZ System (decompiled)

- `m_ptz_init` → `p_ptz_init` (platform motor driver)
- Dedicated PTZ thread
- Pan max: 255 steps, Tilt max: 63 steps
- Autofocus with data callback
- Silent reboot mode skips PTZ reset

### Crypto Layers

| Library | Usage |
|---------|-------|
| AES-CBC/ECB/CTR/CCM | P2P packet encryption (`pprpc_packet_encrypt_*`) |
| MD5 | Password hashing (`md5Encrypt`, `XM_MD5Encrypt`) |
| HMAC-SHA1 | Cloud authentication |
| mbedTLS | Embedded TLS (P2P connections) |
| wolfSSL 4.8.1 | TLS 1.3 (ONVIF, HTTPS) |
| Custom `utl_decrypt_data` | License file decryption |

### Fullhan SDK Functions

| Function | Purpose |
|----------|---------|
| `FH_VENC_GetStream` / `FH_VENC_GetStream_Block` | Get encoded video frame |
| `FH_VENC_ReleaseStream` | Release video buffer |
| `FH_VENC_SetEncryptSeed` | Set stream encryption seed |
| `FH_VENC_SetRotate` | Video rotation |
| `FH_VPSS_SetVORotate` | Video processing rotation |

### SuperPasswd — Time-Based Backdoor Password Generator

`SuperPasswd01` generates a **daily backdoor password** from the current date:

```c
// Decompiled from Ghidra
int SuperPasswd01(char *output, int length) {
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    uint seed = t->tm_mday * (t->tm_mon + 1) * (t->tm_year - 100) * 0x22B8;
    for (int i = length - 1; i >= 0; i--) {
        output[i] = (seed % 10) + '0';
        seed = seed / 10;
    }
}
// Formula: day × month × (year-2000) × 8888
// Example for April 6, 2026: 6 × 4 × 26 × 8888 = 5541888
```

`SuperPasswd` generates a password from MD5 hash chains, extracting only digit characters. Uses `XM_MD5Encrypt` (XiongMai-compatible).

Both are **factory/vendor backdoor** password generators — if you know the date, you know the password.

### Sensor Probe Sequence (decompiled)

`p_image_get_sensor_id` probes sensors via I2C and assigns internal IDs:

| Sensor | Internal ID | Config |
|--------|-------------|--------|
| `gc2083_mipi` | 0x0B (11) | Standard |
| `gc2053_mipi` | 0x03 (3) | Standard |
| `gc2063_mipi` | 0x07 (7) | Standard |
| `imx307_mipi` | 0x01 (1) | Standard |
| `gc2093_mipi` | 0x06 (6) | Standard |
| `cv2003_mipi` | 0x14 (20) | Custom ISP params |

Uses `FHAdv_MS_sensor_ProbeAndCreate()` → `FHAdv_Isp_SensorInit()` to initialize sensor via Fullhan SDK.

### Fullhan SDK API (reverse-engineered)

**System (`FH_SYS_*`):**

| Function | Purpose |
|----------|---------|
| `FH_SYS_Init` | Open `/dev/media_process` device |
| `FH_SYS_BindVpu2Enc` | Bind video processing → encoder pipeline |
| `FH_SYS_BindVpu2Bgm` | Bind video processing → background model (motion detect) |
| `FH_SYS_BindVpu2Nn` | Bind video processing → neural network |
| `FH_SYS_VmmAlloc` / `VmmFree` | Video memory (MMZ) allocation |
| `FH_SYS_GetChipID` | Read SoC chip ID |
| `FH_SYS_GetVersion` | SDK version query |

**Video Encoder (`FH_VENC_*`) — 57 functions:**

| Function | Purpose |
|----------|---------|
| `FH_VENC_CreateChn` | Create encode channel (max 9 channels) |
| `FH_VENC_StartRecvPic` | Start encoding |
| `FH_VENC_GetStream` / `GetStream_Block` | Get encoded frame (async/blocking) |
| `FH_VENC_ReleaseStream` | Release frame buffer |
| `FH_VENC_RequestIDR` | Force keyframe |
| `FH_VENC_SetRCAttr` | Rate control (CBR/VBR) |
| `FH_VENC_SetRotate` | Hardware rotation |
| `FH_VENC_SetH264*` / `SetH265*` | Codec-specific params (VUI, entropy, deblock, slice, intra refresh) |

**Video Processing (`FH_VPSS_*`) — 78 functions:**

| Function | Purpose |
|----------|---------|
| `FH_VPSS_SetViAttr` | Video input attributes (from sensor) |
| `FH_VPSS_OpenChn` / `CloseChn` | Open/close processing channel |
| `FH_VPSS_SetChnAttr` | Channel attributes (resolution, format) |
| `FH_VPSS_GetChnFrame` | Get processed frame |
| `FH_VPSS_SetOsd` / `SetMask` | OSD overlay / privacy mask |
| `FH_VPSS_SetRotate` / `SetVORotate` | Video rotation |
| `FH_VPSS_SetCrop` | Crop region |
| `FH_VPSS_FreezeVideo` / `UnfreezeVideo` | Freeze frame |
| `FH_VPSS_SetLDCAttr` | Lens distortion correction |

**ISP (`FHAdv_Isp_*`):**

| Function | Purpose |
|----------|---------|
| `FHAdv_Isp_Init` | Initialize ISP with contrast/saturation/AE defaults |
| `FHAdv_Isp_SensorInit` | Initialize sensor driver |
| `FHAdv_Isp_SetColorMode` | 0=B&W (night), 1=color (day) |
| `FHAdv_Isp_SetBrightness` / `SetContrast` / `SetSaturation` / `SetSharpeness` | Image adjustment |
| `FHAdv_Isp_SetMirrorAndflip` | Mirror/flip |
| `FHAdv_Isp_SetAEMode` | Auto-exposure mode |

**Background Model / Motion Detection (`FHAdv_MD_*`, `FH_BGM_*`):**

| Function | Purpose |
|----------|---------|
| `FHAdv_MD_Init` / `FHAdv_MD_Ex_Init` | Initialize motion detection |
| `FHAdv_MD_GetResult` / `FHAdv_MD_Ex_GetResult` | Get motion detection result |
| `FHAdv_CD_Init` | Camera tamper (cover) detection |
| `FH_BGM_Enable` / `Disable` | Background model on/off |
| `FH_BGM_SubmitFrame` | Submit frame for analysis |

**Smart IR (`FHAdv_SmartIR_*`):**

| Function | Purpose |
|----------|---------|
| `FHAdv_SmartIR_Init` | Init with thresholds (day=0x1000, night=0x500, switch=0x140) |
| `FHAdv_SmartIR_GetDayNightStatus` | Query current day/night state |
| `FHAdv_SmartIR_SetCfg` / `GetCfg` | Configure thresholds |

**OSD (`FHAdv_Osd_*`):**

| Function | Purpose |
|----------|---------|
| `FHAdv_Osd_Init` / `UnInit` | OSD system |
| `FHAdv_Osd_SetText` / `SetTextLine` | Text overlay |
| `FHAdv_Osd_SetLogo` | Logo overlay |
| `FHAdv_Osd_SetMask` | Privacy mask |
| `FHAdv_Osd_LoadFontLib` / `UnLoadFontLib` | Font management |

### Session Handler (decompiled `mod=session`)

```
cmd=login / login1  → Creates session, returns Session-Id (login1 requires digest auth)
cmd=check           → Returns {"status":"ok"} if session valid
cmd=heartbeat       → Same as check — keepalive
cmd=logout          → Destroys session
cmd=get_auth_info   → Returns realm="CAMERA", nonce, qop for digest auth
```

**Critical**: The `login` command (without `1`) creates a session **without verifying digest auth** — it just generates a session ID and returns it. Only `login1` validates credentials via `FUN_0022d5fc`.

### Telnet Handler (decompiled `mod=telnet`)

```c
cmd=open  → system("telnetd -p 2360")   // Start telnet
cmd=close → system("killall telnetd")    // Stop telnet
```

Uses raw `system()` call — no sanitization. Telnet can be toggled via the web API (with valid session).

### System Handler (decompiled `mod=system`)

```
cmd=reset         → system_reset(1)      // Factory reset
cmd=reboot        → system_reboot()      // Reboot
cmd=export_config → system_export_config() // Export config to /tmp
```

### Account Handler (decompiled `mod=account`)

```
cmd=list   → Returns ALL accounts with PLAINTEXT passwords as JSON array
cmd=add    → Add account (name + password in JSON body)
cmd=delete → Delete account
cmd=modify → Modify account password
```

The `list` command iterates `m_account_get_by_index` and copies both username (0x20 bytes) and password (0x20 bytes) into the JSON response — passwords are never hashed.

### Auth Bypass: `m_account_need_check_auth` (decompiled)

```c
int m_account_need_check_auth(void) {
    // If first account password is empty OR equals "123456"
    // AND second account password is empty
    // → return 0 (NO AUTH NEEDED)
    if ((*account[0].password == '\0' || strcmp(account[0].password, "123456") == 0)
        && *account[1].password == '\0') {
        return 0;  // Auth disabled!
    }
    return 1;  // Auth required
}
```

**This means**: If the admin password is `123456` (the default) and no second account exists, **all authentication is disabled** for the web API. The camera ships in this state.

### Security Vulnerabilities (from decompilation)

1. **Auth disabled by default** — `m_account_need_check_auth` returns 0 when password is "123456" (default), meaning all CGI commands work without auth
2. **`login` vs `login1`** — `cmd=login` creates valid sessions without credential verification; only `login1` checks digest auth
3. **Plaintext passwords everywhere** — `strcmp` comparison, JSON API returns passwords, no hashing
4. **Time-based backdoor** — `SuperPasswd01` generates a predictable daily password from `day × month × (year-2000) × 8888`
5. **Raw `system()` calls** — telnet handler uses `system("telnetd -p 2360")` directly
6. **XMEye compatibility** — `XM_MD5Encrypt` indicates XiongMai protocol support (known for mass exploitation via Mirai botnet)
7. **Command injection risk** — `weber_prehandle` only strips double-quotes; `utl_system` with `snprintf` used extensively
8. **Fixed 10-account buffer** — 0x48 bytes each, potential overflow
9. **Connectivity check to Baidu** — `utl_ping4(0, "14.215.177.39", 1000)`
10. **WiFi SSID/password leak** — debug log prints `ssid = %s, password = %s` in cleartext
11. **No HTTPS** — all credentials transmitted in plain HTTP
12. **DES password hash** — root password uses obsolete DES-crypt

### Function Counts by Subsystem

| Subsystem | Functions | Key Prefix |
|-----------|-----------|------------|
| RTSP | ~90 | `rtsp_*`, `m_rtsp_*`, `zrtsp*` |
| ONVIF | ~120 | `__tds__*`, `__trt__*`, `__tptz__*`, `__timg__*` |
| PTZ | ~40 | `m_ptz_*`, `__tptz__*` |
| Cloud P2P | ~30 | `cloud_p2p_*` |
| Video | ~20 | `FH_VENC_*`, `_VENC_*` |
| Web/CGI | ~10 | `weber_*`, `web_cmd_*` |
| Auth | ~15 | `m_account_*`, `m_authentication_*` |
| Encryption | ~30 | `aes_*`, `packet_aes_*`, `pprpc_packet_encrypt_*` |

## OpenIPC Porting Status

### What This Dump Provides for OpenIPC

| Requirement | Status | Detail |
|-------------|--------|--------|
| Flash dump | **Complete** | All 5 MTD partitions |
| Kernel modules | **Complete** | 8 modules, NOT stripped (full symbols) |
| ISP tuning | **Complete** | 12 hex files (4 sensors × 3 modes) |
| RT-Thread firmware | **Complete** | ARC co-processor binary |
| Boot args & memory map | **Complete** | Kernel cmdline, `/proc/iomem`, IRQ table |
| Driver load order | **Complete** | `load_driver.sh` with GPIO sequence |
| Sensor identification | **Complete** | CV2003 MIPI 1-lane, internal ID 0x14 |
| AI model | **Complete** | `persondet.nbg` (1.3MB) |
| Fullhan SDK API | **Complete** | 135+ functions reverse-engineered (FH_VENC, FH_VPSS, FHAdv_Isp, FHAdv_Osd, FH_BGM, FHAdv_SmartIR) |
| Peripheral register map | **Complete** | 24 peripherals with addresses and IRQs |
| Video pipeline architecture | **Complete** | ISP → VPSS → VENC binding functions |
| Security analysis | **Complete** | 12 vulnerabilities, backdoor password algorithm |
| UART access | **Confirmed** | ttyS0 @ 115200, 1s boot delay |

### What's Still Needed

| Missing | Why | How To Get |
|---------|-----|------------|
| **Toolchain** | Need `arm-fullhanv3-linux-uclibcgnueabi-gcc 6.5.0` | Available in [Tuya IoTOS SDK](https://github.com/tuya/tuya-iotos-embeded-sdk-multimedia) or build via Buildroot/crosstool-ng |
| **Kernel .config** | `CONFIG_IKCONFIG` was off, no `/proc/config.gz` | OpenIPC builds their own kernel config; peripheral map above helps |
| **Board file** | No device tree — uses legacy board files | Register map from `/proc/iomem` above provides all addresses |
| **Sensor driver source** | CV2003 driver in `ipcam` binary, not separate `.ko` | Sensor probe sequence decompiled; I2C on bus 0 at `0xF0200000` |
| **Majestic streamer** | No Fullhan support in majestic yet | All FH_VENC/FH_VPSS API functions documented above |

### Current OpenIPC Fullhan Status

- **FH8852V201 not listed** on OpenIPC (only V100, V200, V210)
- All Fullhan SoCs show **"No ready solution yet"**
- FH8852V200 has a [lite firmware download](https://openipc.org/cameras/vendors/fullhan/socs/fh8852v200) but no install instructions
- Known issue: `create jpeg channel failed` on FH8852V200 ([#1633](https://github.com/OpenIPC/firmware/issues/1633))
- Kernel builder: `wilbur@Fossa` using `gcc 6.5.0 (arm_multilib_uclibc_20200924)`

### Key Resources for OpenIPC Developers

- **Issue to post on**: [OpenIPC/firmware#1633](https://github.com/OpenIPC/firmware/issues/1633)
- **Fullhan SoC page**: [openipc.org/cameras/vendors/fullhan](https://openipc.org/cameras/vendors/fullhan)
- **Tuya toolchain**: [tuya-iotos-embeded-sdk-multimedia](https://github.com/tuya/tuya-iotos-embeded-sdk-multimedia) (contains `arm-fullhanv3-linux-uclibcgnueabi`)
- **Existing Vatilon dump**: [mtrakal/ipc-vatilon](https://github.com/mtrakal/ipc-vatilon)
- **Bootlin toolchains**: [toolchains.bootlin.com](https://toolchains.bootlin.com/) (ARM/uClibc)

### Video Pipeline Architecture (for majestic integration)

```
Sensor (CV2003, I2C-0)
  │
  ▼ MIPI CSI (IRQ 50: mipi_wrap_int)
  │
  ▼ FHAdv_MS_sensor_ProbeAndCreate() → FHAdv_Isp_SensorInit()
  │
ISP (IRQ 39: ispp)
  │ FHAdv_Isp_Init() — contrast, saturation, AE defaults
  │ FHAdv_Isp_SetColorMode(0=B&W, 1=color)
  │
  ▼ FH_SYS_BindVpu2Enc / BindVpu2Bgm / BindVpu2Nn
  │
VPSS — Video Processing Subsystem (78 functions)
  │ FH_VPSS_SetViAttr() — input from sensor
  │ FH_VPSS_OpenChn() — open processing channel
  │ FH_VPSS_SetChnAttr() — resolution, format
  │ FH_VPSS_SetOsd() — text/logo overlay
  │ FH_VPSS_SetMask() — privacy mask
  │ FH_VPSS_SetRotate() — hardware rotation
  │ FH_VPSS_SetLDCAttr() — lens distortion correction
  │
  ├──▶ VENC — Video Encoder (IRQ 41: veu-sw-irq, IRQ 47: veu_loop)
  │     FH_VENC_CreateChn() — max 9 channels
  │     FH_VENC_SetChnAttr() — H.264/H.265, resolution, bitrate
  │     FH_VENC_StartRecvPic() → FH_VENC_GetStream() → FH_VENC_ReleaseStream()
  │     FH_VENC_RequestIDR() — force keyframe
  │
  ├──▶ JPEG — Snapshot Encoder (IRQ 45: jpeg, IRQ 54: jpg_loop)
  │     Snapshot and MJPEG stream
  │
  ├──▶ BGM — Background Model (motion detection)
  │     FH_BGM_Enable() → FH_BGM_SubmitFrame() → FHAdv_MD_GetResult()
  │
  └──▶ NNA — Neural Network (IRQ 42: NNA_IRQ)
        Person detection (persondet.nbg model)
        FH_SYS_BindVpu2Nn()
```

## Dump Date

2026-04-05
