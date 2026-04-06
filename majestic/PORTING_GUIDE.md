# Majestic Porting Guide — Fullhan FH8852V201

## Status: 15/21 requirements met

Everything is documented. The 4 missing items all stem from one root cause:
**Fullhan SDK is statically linked into the vendor binary — there are no `.so` files.**

## What Majestic Needs (and what we have)

### 1. Kernel Modules — READY

All modules dumped with full symbols (not stripped):

```bash
insmod vmm.ko mmz=anonymous,0,0xA2480000,28160K anony=1
echo 13 > /sys/class/gpio/export
echo out > /sys/class/gpio/GPIO13/direction
echo 0 > /sys/class/gpio/GPIO13/value   # reset sensor
insmod xbus_rpc.ko fn=rtthread_arc.bin fa=0xbff80000
echo 1 > /sys/class/gpio/GPIO13/value   # release sensor
insmod media_process.ko
insmod isp.ko
insmod enc.ko
insmod jpeg.ko
insmod nna.ko
```

These `.ko` files contain the hardware abstraction layer. They expose device nodes
(`/dev/media_process`, `/dev/isp`, `/dev/nna`, etc.) and ioctl interfaces.

### 2. Vendor SDK Libraries — NOT AVAILABLE AS .so

The Fullhan SDK functions (456+) are statically linked into the `ipcam` binary.
No separate `libmpi.so`, `libsns.so`, or `libmedia.so` exists on the filesystem.

**Options for OpenIPC:**

A. **Contact Fullhan** — Request SDK libraries for FH8852V201. Phone: 021-61121558.
   The Tuya IoTOS SDK includes Fullhan toolchain and may include SDK `.so` files:
   https://github.com/tuya/tuya-iotos-embeded-sdk-multimedia

B. **Use kernel module ioctl directly** — The `.ko` modules ARE the vendor SDK.
   Majestic could call ioctl() directly on `/dev/media_process` instead of through
   wrapper libraries. All ioctl codes are documented in our SDK headers:
   - `0xC0F4696A` — VPSS set VI attributes
   - `0xC0046971` — VPSS enable
   - `0xC00C696C` — VPSS set channel attributes
   - `0xC0046973` — VPSS open channel
   - `0xC00869A4` — VPSS set VO mode
   - `0xC0086978` — VPSS set frame control
   - `0xC0686970` — VPSS get frame
   - `0xC1A04D05` — VENC get stream
   - `0xC01C5403` — VENC create channel
   - `0xC06C540C` — VENC set rate control

C. **Extract from Tuya/other SDK** — If another Fullhan SDK package has the `.so`
   files built for FH8852V200, they should work for V201 (same API).

### 3. Sensor Driver — CREATE FROM EXTRACTED DATA

The CV2003 sensor driver is embedded in the ipcam binary. We extracted:

- **I2C protocol**: slave 0x1A, 16-bit addr, 8-bit data, on `/dev/i2c-0`
- **Register init tables**: 52 registers × 4 fps modes (see `sdk/cv2003_sensor.h`)
- **Sensor ID**: GC4201AC (Galaxycore rebranded as CV2003)
- **Init sequence**: reset GPIO13 → probe I2C → write regs → enable streaming

To create `libsns_cv2003.so`, implement these functions:
```c
int sensor_init(int bus, int addr);           // I2C init
int sensor_set_format(int width, int height, int fps);  // Write reg table
int sensor_start_streaming(void);             // Write 0x3141=1
int sensor_set_exposure(int value);           // Write exposure regs
int sensor_set_gain(int value);               // Write gain regs
```

Alternatively, majestic could probe the sensor directly using the data in
`cv2003_sensor.h` — all register addresses and values are documented.

### 4. ISP Tuning — READY

Three ISP tuning hex files (4984 bytes each) in `runtime/sensor/`:
- `cv2003_day.hex` — daytime color mode
- `cv2003_night.hex` — night IR mode (941 bytes different)
- `cv2003_white.hex` — white LED mode (51 bytes different)

Load via `API_ISP_LoadIspParam()` or write directly to ISP registers.

### 5. majestic.yaml — CREATED

See `majestic/majestic.yaml` — all values mapped from live `ipcam.conf`.

### 6. Sensor Config — CREATED

See `majestic/sensor-cv2003_mipi-1080p.ini`.

## Quick Start: Init Sequence for Majestic

Based on the decompiled SDK init from `p_platform_sdk_init`:

```c
// 1. Load kernel modules (load_driver.sh)

// 2. Init system
FH_SYS_Init();  // opens /dev/media_process

// 3. Configure media pipeline
_media_driver_config();
// Internally sets: vi_1920_1080, cap_0_1920_1080, cap_1_800_576, cap_2_512_288
//                  buf_0_2, buf_1_3, buf_2_1, cirbuf_on, cbufsize_80
//                  stm_1572864, nn_clk,enable,90000000

// 4. Init VPSS
FH_VPSS_SetViAttr({1920, 1080});
FH_VPSS_Enable(0);
FH_VPSS_SetLDCAttr({0, 960, 540, 50});  // LDC center + strength

// Channel 0: main stream 1920x1080
FH_VPSS_SetChnAttr(0, {1920, 1080});
FH_VPSS_OpenChn(0);
FH_VPSS_SetVOMode(0, 1);  // normal output

// Channel 1: sub stream 704x576
FH_VPSS_SetChnAttr(1, {704, 576});
FH_VPSS_OpenChn(1);
FH_VPSS_SetVOMode(1, 1);

// Channel 2: NNA 512x288 (optional)
FH_VPSS_SetChnAttr(2, {512, 288});
FH_VPSS_OpenChn(2);
FH_VPSS_SetVOMode(2, 5);  // RGB888 for NNA
FH_VPSS_SetFramectrl(2, {5, 1});  // 1/5 fps

// 5. Init ISP + sensor
FHAdv_MS_sensor_ProbeAndCreate("cv2003_mipi");
API_ISP_MemInit(1920, 1080);
API_ISP_SensorRegCb(0, sensor_ctrl);
API_ISP_SensorInit();
API_ISP_SetSensorFmt(0x203);  // Bayer
API_ISP_Init();
API_ISP_SensorKick();

// 6. Init encoder
// Channel 0: H.265 main
FH_VENC_CreateChn(0, {H265, 1920, 1080});
FH_VENC_SetChnAttr(0, {H265, 1920, 1080});
FH_VENC_SetRCAttr(0, {VBR, bitrate=810, gop=100});
FH_VENC_StartRecvPic(0);

// Channel 1: H.265 sub
FH_VENC_CreateChn(1, {H265, 704, 576});
FH_VENC_SetChnAttr(1, {H265, 704, 576});
FH_VENC_SetRCAttr(1, {VBR, bitrate=512, gop=100});
FH_VENC_StartRecvPic(1);

// 7. Bind pipeline
FH_SYS_BindVpu2Enc();  // VPU CH0 → ENC CH0, VPU CH1 → ENC CH1

// 8. Init audio
FH_AC_Init();
FH_AC_AI_Set_Algo_Param(params, 0xEA);
FH_AC_AI_Enable();

// 9. Stream loop
while (running) {
    FH_VENC_GetStream(0, &stream);  // get encoded frame
    // Send via RTSP/HLS/etc.
    FH_VENC_ReleaseStream(0);
    FH_VENC_GetStream(1, &stream);  // sub stream
    FH_VENC_ReleaseStream(1);
    m_wdt_feed();  // feed watchdog every <30s
}
```

## Hardware Reference

| Component | Detail |
|-----------|--------|
| SoC | FH8852V201, Cortex-A7 750MHz |
| DDR | 512Mbit (64MB), 606MHz |
| ARC core | 500MHz co-processor (H.265 encoding) |
| Sensor | CV2003/GC4201AC, 1920x1080, MIPI 1-lane |
| I2C | /dev/i2c-0, slave 0x1A, 50MHz |
| ISP | 214MHz, 1920x1080 |
| Encoder | H.264/H.265 hardware, 300MHz VEU |
| NNA | 91MHz, person detection |
| Audio | Internal codec, 480kHz AC clock |
| Flash | 8MB SPI NOR, 200MHz |
| Ethernet | 100Mbps RMII |
| VMM | 28MB at 0xA2480000 |
| Watchdog | /dev/watchdog, 30s timeout |
| Console | ttyS0 @ 115200 |

## Files in This Directory

| File | Purpose |
|------|---------|
| `majestic.yaml` | Complete majestic config derived from live camera |
| `sensor-cv2003_mipi-1080p.ini` | Sensor configuration for majestic |
| `PORTING_GUIDE.md` | This file |
