#ifndef __MAJESTIC_BITRATE_H__
#define __MAJESTIC_BITRATE_H__

#include <math.h>
#include <stdint.h>

static inline uint32_t majestic_auto_bitrate_kbps(uint32_t width, uint32_t height, uint32_t fps)
{
    double r = (double)width * height / 2073600.0;
    double kbps = (fps * 0.65 / 30.0 + 0.35) * pow(r, r < 1.0 ? 1.0 : 0.75) * 4096.0;
    int rounded = (int)(kbps * 0.015625 + 0.5) * 64;
    return rounded < 128 ? 128 : (uint32_t)rounded;
}

#endif
