#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

#define PACK_SIZE 0x88
#define FRAME_INTERVAL_US 40000u
#define ODD_FRAME_JITTER_US 1037u

typedef struct {
    const uint8_t *p;
    uint32_t len;
} nal_t;

typedef struct {
    int first;
    int count;
    int idr;
} access_unit_t;

static uint8_t *g_buf;
static nal_t g_nals[8192];
static int g_nal_count;
static access_unit_t g_aus[4096];
static int g_au_count;
static int g_au;
static uint32_t g_seq;
static uint64_t g_pts0;
static int g_pipe[2] = {-1, -1};
static pthread_once_t g_once = PTHREAD_ONCE_INIT;

static int nal_type(const nal_t *n)
{
    return n->p[n->p[2] == 1 ? 3 : 4] & 0x1f;
}

static void load_stream(void)
{
    const char *path = getenv("ORACLE_H264");
    FILE *f = fopen(path ? path : "/oracle/input.h264", "rb");
    if (!f) {
        perror("oracle: open h264");
        exit(1);
    }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    g_buf = malloc(n);
    fread(g_buf, 1, n, f);
    fclose(f);
    for (long i = 0; i + 3 < n;) {
        int sc3 = g_buf[i] == 0 && g_buf[i + 1] == 0 && g_buf[i + 2] == 1;
        int sc4 = g_buf[i] == 0 && g_buf[i + 1] == 0 && g_buf[i + 2] == 0 && g_buf[i + 3] == 1;
        if (!sc3 && !sc4) {
            i++;
            continue;
        }
        if (g_nal_count)
            g_nals[g_nal_count - 1].len = (uint32_t)(g_buf + i - g_nals[g_nal_count - 1].p);
        g_nals[g_nal_count++].p = g_buf + i;
        i += sc4 ? 4 : 3;
    }
    g_nals[g_nal_count - 1].len = (uint32_t)(g_buf + n - g_nals[g_nal_count - 1].p);
    int start = 0;
    for (int k = 0; k < g_nal_count; k++) {
        int t = nal_type(&g_nals[k]);
        if (t < 1 || t > 5)
            continue;
        g_aus[g_au_count].first = start;
        g_aus[g_au_count].count = k - start + 1;
        g_aus[g_au_count].idr = t == 5;
        g_au_count++;
        start = k + 1;
    }
    const char *pts0 = getenv("ORACLE_PTS0");
    if (pts0) {
        g_pts0 = strtoull(pts0, NULL, 10);
    } else {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        g_pts0 = (uint64_t)ts.tv_sec * 1000000u + ts.tv_nsec / 1000;
    }
    fprintf(stderr, "oracle: %d NALs, %d access units\n", g_nal_count, g_au_count);
}

static void *frame_clock(void *arg)
{
    (void)arg;
    for (;;) {
        usleep(FRAME_INTERVAL_US);
        char c = 1;
        write(g_pipe[1], &c, 1);
    }
    return NULL;
}

static void init_once(void)
{
    load_stream();
    pipe(g_pipe);
    pthread_t t;
    pthread_create(&t, NULL, frame_clock, NULL);
}

int HI_MPI_VENC_GetFd(int chn)
{
    pthread_once(&g_once, init_once);
    return chn == 0 ? g_pipe[0] : -1;
}

int HI_MPI_VENC_QueryStatus(int chn, uint32_t *status)
{
    memset(status, 0, 64);
    if (chn != 0)
        return 0;
    status[2] = 1;
    status[3] = (uint32_t)g_aus[g_au].count;
    return 0;
}

int HI_MPI_VENC_GetStream(int chn, uint32_t *stream, int timeout)
{
    (void)timeout;
    if (chn != 0)
        return -1;
    char c;
    read(g_pipe[0], &c, 1);
    const access_unit_t *au = &g_aus[g_au];
    uint8_t *packs = (uint8_t *)(uintptr_t)stream[0];
    uint64_t pts = g_pts0 + (uint64_t)g_seq * FRAME_INTERVAL_US + ((g_seq & 1) ? ODD_FRAME_JITTER_US : 0);
    for (int k = 0; k < au->count; k++) {
        const nal_t *n = &g_nals[au->first + k];
        uint8_t *pk = packs + k * PACK_SIZE;
        memset(pk, 0, PACK_SIZE);
        *(uint32_t *)(pk + 0x08) = (uint32_t)(uintptr_t)n->p;
        *(uint32_t *)(pk + 0x0c) = n->len;
        memcpy(pk + 0x10, &pts, 8);
        *(uint32_t *)(pk + 0x18) = k == au->count - 1;
        *(uint32_t *)(pk + 0x1c) = (uint32_t)nal_type(n);
    }
    if (au->idr)
        fprintf(stderr, "oracle: idr seq=%u pts=%llu\n", g_seq, (unsigned long long)pts);
    stream[1] = (uint32_t)au->count;
    stream[2] = g_seq++;
    stream[9] = au->idr ? 0 : 2;
    return 0;
}

int HI_MPI_VENC_ReleaseStream(int chn, uint32_t *stream)
{
    (void)stream;
    if (chn == 0)
        g_au = (g_au + 1) % g_au_count;
    return 0;
}

int HI_MPI_SYS_GetVersion(char *version)
{
    memset(version, 0, 64);
    strcpy(version, "Gk7205V200_MPP_V1.0.1.0.B020 Release");
    return 0;
}

void *HI_MPI_SYS_Mmap(uint64_t phys, uint32_t size)
{
    (void)phys;
    return calloc(1, size);
}

void *HI_MPI_SYS_MmapCache(uint64_t phys, uint32_t size)
{
    (void)phys;
    return calloc(1, size);
}

int HI_MPI_SYS_Munmap(void *p, uint32_t size)
{
    (void)p;
    (void)size;
    return 0;
}

int ioctl(int fd, int req, ...)
{
    va_list ap;
    va_start(ap, req);
    void *arg = va_arg(ap, void *);
    va_end(ap);
    char proc[32];
    char link[64] = {0};
    snprintf(proc, sizeof(proc), "/proc/self/fd/%d", fd);
    if (readlink(proc, link, sizeof(link) - 1) > 0 && strcmp(link, "/dev/null") == 0)
        return 0;
    return (int)syscall(SYS_ioctl, fd, req, arg);
}
