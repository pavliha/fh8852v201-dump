# /proc/driver/ — Live Hardware State

Dumped from running KEVIEW H43 (FH8852V201) camera.

## Chip Info
```
chip_name       : FH8852V201
ddr_size        : 512Mbit
plat_id         : 0x20102801
pkg_id          : 0x8503
```

## Clock Tree (all frequencies)
```
PLL:
  osc_clk              24 MHz     (crystal)
  pll_ddr_pclk        606 MHz     (DDR)
  pll_cpu_pclk       1500 MHz     (CPU PLL)
  pll_cpu_rclk       1000 MHz

CPU/Bus:
  arm_clk             750 MHz     (ARM Cortex-A7)
  arc_clk             500 MHz     (ARC co-processor)
  ahb_clk             150 MHz     (AHB bus)
  ddr_clk             606 MHz     (DDR3/DDR4)

Video pipeline:
  isp_aclk            214 MHz     (ISP)
  pix_clk             214 MHz     (pixel clock)
  veu_clk             300 MHz     (video encoder unit)
  veu_adapt_clk       300 MHz     (encoder adapter)
  nn_clk               91 MHz     (NNA accelerator)
  bgm_clk             214 MHz     (background model, disabled when idle)
  jpeg_clk            214 MHz     (JPEG encoder, disabled when idle)
  jpeg_adapt_clk      214 MHz

Peripherals:
  spi0_clk            200 MHz     (SPI flash)
  spi1_clk            100 MHz
  spi2_clk            200 MHz     (disabled)
  i2c0_clk             50 MHz     (sensor I2C)
  i2c1_clk             50 MHz
  i2c2_clk             50 MHz
  uart0_clk            16.7 MHz   (console)
  uart1_clk            16.7 MHz
  uart2_clk            16.7 MHz
  pwm_clk              50 MHz     (LED PWM)
  eth_clk              50 MHz     (Ethernet)
  ac_clk              480 kHz     (audio codec)
  sadc_clk              5 MHz     (SAR ADC)
  wdt_clk               1 MHz     (watchdog)
  tmr0_clk              1 MHz     (timer)
  pts_clk               1 MHz     (presentation timestamp)
  cis_clk_out          24 MHz     (sensor clock output)
  mipi_dphy_clk        24 MHz     (MIPI D-PHY)
  efuse_clk            25 MHz     (eFuse/OTP)
  gpio0_db_clk          1 kHz     (GPIO debounce)
  gpio1_db_clk          1 kHz
  sdc0/sdc1_clk        50 MHz     (SD card)
```

## ISP Status
```
SDK VERSION:    V1.1.1 17.0.1(build 006)(g873427b)
ISP Resolution: 1920x1080
Sensor FPS:     12.50 fps (10000/799)
ISPP PIC_START: 25027
ISPP OVERFLOW:  0 (1 historical)
Soft Resets:    7 total (5 from overflow recovery)
```

## Encoder Status
```
SDK VERSION: V1.1.1 17.0.0(build 000)(g5a38f1b)
Channel 0: OPEN (1920x1080 main)
Channel 1: OPEN (704x576 sub)
```

## VPU (Video Processing Unit) Status
```
SDK VERSION: V1.1.1 17.0.1(build 006)(g873427b)
Video Input: 1920x1080 @ 12.50 fps
PIC_PROCESS_AVG_TIME: 31721 μs (31.7ms)
PIC_PROCESS_MAX_TIME: 31790 μs

Channel 0: 1920x1088 SCAN mode, no rotation, 12.0 fps output
Channel 1:  704x576  SCAN mode, no rotation, 12.1 fps output
Channel 2:  512x288  RGB888 mode (NNA input), 4.9 fps output
```

## NNA Status
```
SDK VERSION: V1.1.1 17.0.0(build 000)(g5a38f1b)
Input: 512x288
HW AVG TIME: 447379 μs (447ms per inference!)
Received: 4480 frames
Finished: 4476 frames
Reset: 0

NNA memory: Input at 0xA2B05000
CMD buffers: 0xA3B9CDC0 (2.25MB) + 0xA3A7CDC0 (1.12MB) + ...
```

## VMM (Video Memory Manager)
```
Total: 28160KB (27.5MB) at 0xA2480000-0xA3FFFFFF
Used:  25515KB (25.5MB)
Free:   1984KB (1.98MB)

Block layout:
  vpu-sys        88KB    0xA2480000  (YCMEAN buffers)
  vpu-ch-0     4560KB    0xA2496000  (main stream processing)
  vpu-ch-1     2028KB    0xA290A000  (sub stream processing)
  vpu-ch-2      580KB    0xA2B05000  (NNA input, 512x288 RGB888)
  venc-sys     2572KB    0xA2B96000  (encoder system)
  isp          3016KB    0xA2E20000  (ISP pipeline)
  venc-ch-0    6640KB    0xA3112000  (main stream encode buffers)
  venc-ch-1     896KB    0xA378E000  (sub stream encode buffers)
  adv_osd_0_1  3528KB    0xA386E000  (OSD overlay ch0+ch1)
  adv_osd_1_2   240KB    0xA3A3C000  (OSD overlay ch1+ch2)
  nna-ch-0     3528KB    0xA3A78000  (NNA model + workspace)
  nna-cache      132KB    0xA3DEA000  (NNA cache)
```

## MIPI CSI Status
```
Data rate: 800-999 Mbps
Lanes: 1 (single lane MIPI)
WDR: OFF
Sensor input: YES
Frame size: 1920x1080
All status: OK (no errors)
Frame count: 25651
```

## Media Process Pipeline Bindings
```
VPU CH0 ← NONE         [1920x1088]   (from ISP)
VPU CH1 ← NONE         [ 704x 576]   (downscaled)
VPU CH2 ← NONE         [ 512x 288]   (NNA input)
VPU_BGM ← NONE         [ 240x 136]   (motion detection)
ENC CH0 ← VPU CH0      [1920x1088]   Submit:24675 OK
ENC CH1 ← VPU CH1      [ 704x 576]   Submit:24740 OK
NNA CH0 ← VPU CH2      [ 512x 288]   Submit:4576, Fail:5692 (!)
```

Note: NNA has high failure rate (5692 fails vs 4576 success). Error 0x81044085 suggests buffer overflow or timeout — NNA inference (447ms) is slower than frame interval.

## PWM Channels
```
PWM0: ENABLE  duty=10000ns period=20000ns (50% duty, 50kHz) → IR LED brightness
PWM1: ENABLE  duty=0      period=10000ns (0% = off)        → White LED brightness
PWM2: ENABLE  duty=0      period=10000ns (0% = off)        → Stepper motor 0
PWM3: ENABLE  duty=0      period=10000ns (0% = off)        → Stepper motor 1
PWM4-11: DISABLED
```

## Pin Mux (PINCTRL)
```
Pin  Name           Mux   Dir  Function
 00  GPIO30         sel0  in   (unused)
 01  GPIO31         sel0  in   (unused)
 02  GPIO32         sel0  in   (unused)
 03  UART0_TX       sel0  out  Console TX
 04  UART0_RX       sel0  in   Console RX
 05  I2C0_SCL       sel0  out  Sensor I2C clock
 06  I2C0_SDA       sel0  out  Sensor I2C data
 07  SENSOR_CLK     sel0  out  24MHz sensor clock output
 08  GPIO13         sel0  out  Sensor reset
 11  GPIO39         sel1  in   IR LED feedback
 12  GPIO40         sel1  in   Speaker control
 13  PWM2           sel2  out  Stepper motor 0
 14  PWM3           sel2  out  Stepper motor 1
 15  GPIO41         sel1  in   (unused)
 16  GPIO42         sel1  in   (unused)
 17  GPIO47         sel1  in   (unused)
 18  PWM0           sel0  out  IR LED PWM
 19  PWM1           sel0  out  White LED PWM
 20  GPIO0          sel1  out  IR LED 1
 21  GPIO1          sel1  out  IR LED 2
 22  GPIO2          sel1  in   Photoresistor
 23  GPIO3          sel1  in   Light sensor
 24  GPIO4          sel1  in   (unused)
 25  SSI0_CLK       sel0  out  SPI flash clock
 26  GPIO6          sel1  out  Audio amplifier enable
 27  SSI0_TXD       sel0  out  SPI flash MOSI
 28  SSI0_RXD       sel0  in   SPI flash MISO
 29  GPIO9          sel1  out  (LED status?)
 30  GPIO10         sel1  out  (LED status?)
 31  SD0_CD         sel0  in   SD card detect
 32  SD0_CLK        sel0  out  SD card clock
 33  SD0_CMD        sel0  io   SD card command
 34-37 SD0_DATA     sel0  io   SD card data 0-3
 38  GPIO26         sel1  out  IR-cut filter control
 39  SADC_XAIN1     sel0  in   ADC input (light sensor analog)
 40  ETH_LINK_ACT   sel2  out  Ethernet LED
 41  GPIO29         sel0  in   (unused)
 42-47 MIPI_*       sel0  -    MIPI CSI sensor interface
```

## SADC (ADC Values)
```
Channel 0: 1800  (reference voltage?)
Channel 1: 1460  (light sensor - daylight reading)
Channel 2: 10    (unused)
Channel 3: 10    (unused)
Channel 4: 10    (unused)
Channel 5: 9     (unused)
Channel 6: 907   (temperature sensor?)
Channel 7: 7     (unused)
```

## Temperature Sensor
```
Raw ADC: 2382
Temperature: 66.1°C (SoC die temperature)
```

## XBus (ARC Co-processor IPC)
```
Server callback: HEVC_CB (H.265 encoder callback)
Interrupts: 199346
TX: 150006/150007
Callbacks: 49342
Drops: 0
Errors: 0
```
