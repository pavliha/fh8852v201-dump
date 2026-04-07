/**
 * Fullhan FH8852V201 — Minimal Media Processing Interface
 * Clean-room ioctl wrapper — no proprietary code.
 */

#ifndef __FH_MPI_H__
#define __FH_MPI_H__

#include <stdint.h>

/* System */
int  fh_sys_init(void);
void fh_sys_exit(void);

/* VPSS */
int fh_vpss_set_vi_attr(uint32_t width, uint32_t height);
int fh_vpss_enable(uint32_t group);
int fh_vpss_set_chn_attr(uint32_t chn, uint32_t width, uint32_t height);
int fh_vpss_open_chn(uint32_t chn);
int fh_vpss_set_vo_mode(uint32_t chn, uint32_t mode);
int fh_vpss_set_framectrl(uint32_t chn, uint16_t src_rate, uint16_t dst_rate);
int fh_vpss_get_frame(uint32_t chn, uint32_t *frame_out, uint32_t timeout_ms);

/* VENC */
int fh_venc_get_stream(uint32_t chn, uint32_t *stream_out);
int fh_venc_create_chn(uint32_t chn, uint32_t width, uint32_t height,
                       uint32_t codec_type);

/* Sensor I2C */
int  fh_sensor_init(uint8_t slave_addr_7bit, int addr_mode);
int  fh_sensor_write(uint16_t reg, uint8_t val);
int  fh_sensor_read(uint16_t reg, uint8_t *val);
void fh_sensor_close(void);
int  fh_sensor_reset(void);

/* Watchdog */
int  fh_wdt_open(void);
int  fh_wdt_feed(void);
void fh_wdt_close(void);

/* GPIO */
int fh_gpio_export(int gpio);
int fh_gpio_set_direction(int gpio, int output);
int fh_gpio_write(int gpio, int value);
int fh_gpio_read(int gpio);

#endif /* __FH_MPI_H__ */
