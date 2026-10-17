#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "../../majestic/rtsp_server.h"

#define FRAME_INTERVAL_US 40000u
#define ODD_FRAME_JITTER_US 1037u

typedef struct {
    const uint8_t *p;
    size_t len;
} nal_t;

static nal_t g_nals[8192];
static int g_nal_count;
static int g_au_first[4096];
static int g_au_count_nals[4096];
static int g_au_count;

static uint64_t now_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000u + (uint64_t)ts.tv_nsec / 1000u;
}

static void load(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        perror(path);
        exit(1);
    }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *b = malloc((size_t)n);
    fread(b, 1, (size_t)n, f);
    fclose(f);
    for (long i = 0; i + 3 < n;) {
        int sc = b[i] == 0 && b[i + 1] == 0 && b[i + 2] == 1 ? 3
               : b[i] == 0 && b[i + 1] == 0 && b[i + 2] == 0 && b[i + 3] == 1 ? 4 : 0;
        if (!sc) {
            i++;
            continue;
        }
        if (g_nal_count)
            g_nals[g_nal_count - 1].len = (size_t)(b + i - g_nals[g_nal_count - 1].p);
        g_nals[g_nal_count++].p = b + i + sc;
        i += sc;
    }
    g_nals[g_nal_count - 1].len = (size_t)(b + n - g_nals[g_nal_count - 1].p);
    int start = 0;
    for (int k = 0; k < g_nal_count; k++) {
        int t = g_nals[k].p[0] & 0x1f;
        if (t < 1 || t > 5)
            continue;
        g_au_first[g_au_count] = start;
        g_au_count_nals[g_au_count] = k - start + 1;
        g_au_count++;
        start = k + 1;
    }
}

int main(int argc, char **argv)
{
    load(argc > 1 ? argv[1] : "input.h264");
    uint64_t pts0 = argc > 2 ? strtoull(argv[2], NULL, 10) : 1000000000u;
    rtsp_server_t srv;
    if (rtsp_server_init(&srv, 554) < 0) {
        perror("rtsp_server_init");
        return 1;
    }
    rtsp_server_use_shadow(&srv, argc > 3 ? argv[3] : "shadow");
    rtsp_server_enable_stream(&srv, 0, 25);
    rtsp_server_enable_stream(&srv, 1, 25);
    fprintf(stderr, "replay: %d NALs, %d access units, RTSP on :554\n", g_nal_count, g_au_count);
    uint64_t next = now_us();
    for (uint32_t seq = 0;; seq++) {
        while (now_us() < next) {
            rtsp_server_poll(&srv);
            usleep(1000);
        }
        next += FRAME_INTERVAL_US;
        int au = (int)(seq % (uint32_t)g_au_count);
        uint64_t pts = pts0 + (uint64_t)seq * FRAME_INTERVAL_US + ((seq & 1) ? ODD_FRAME_JITTER_US : 0);
        for (int stream = 0; stream < 2; stream++) {
            for (int k = 0; k < g_au_count_nals[au]; k++) {
                const nal_t *n = &g_nals[g_au_first[au] + k];
                rtsp_server_push_nal(&srv, stream, n->p, n->len, pts, k == g_au_count_nals[au] - 1);
            }
        }
    }
}
