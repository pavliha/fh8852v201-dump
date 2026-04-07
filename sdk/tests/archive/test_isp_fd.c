#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>

int main(void) {
    printf("=== Testing ioctls on different device fds ===\n\n");

    int mp = open("/dev/media_process", O_RDWR);
    int isp = open("/dev/isp", O_RDWR);
    int enc = open("/dev/enc", O_RDWR);
    int jpeg = open("/dev/jpeg", O_RDWR);

    printf("media_process=%d isp=%d enc=%d jpeg=%d\n\n", mp, isp, enc, jpeg);

    uint32_t vi[62]; memset(vi, 0, sizeof(vi));
    vi[0] = 1920; vi[1] = 1080;

    /* Try SET_VI_ATTR on each fd */
    int fds[] = { mp, isp, enc, jpeg };
    const char *names[] = { "media_process", "isp", "enc", "jpeg" };

    for (int i = 0; i < 4; i++) {
        if (fds[i] < 0) continue;
        int ret = ioctl(fds[i], 0xC0F4696A, vi);
        printf("  SET_VI_ATTR on %-15s: ret=0x%08x errno=%d\n",
               names[i], (uint32_t)ret, ret<0?errno:0);
    }

    printf("\n");
    /* Try VPSS_ENABLE on each */
    uint32_t en = 0;
    for (int i = 0; i < 4; i++) {
        if (fds[i] < 0) continue;
        int ret = ioctl(fds[i], 0xC0046971, &en);
        printf("  ENABLE on %-15s: ret=0x%08x errno=%d\n",
               names[i], (uint32_t)ret, ret<0?errno:0);
    }

    printf("\n");
    /* Try SYS_QUERY_MEM on each */
    for (int i = 0; i < 4; i++) {
        if (fds[i] < 0) continue;
        uint32_t sz = 0;
        int ret = ioctl(fds[i], 0xC0046966, &sz);
        printf("  SYS_QUERY on %-15s: ret=0x%08x size=%u errno=%d\n",
               names[i], (uint32_t)ret, sz, ret<0?errno:0);
    }

    /* Also try the VPSS ioctl type 'i' (0x69) prefix 
     * with EINVAL check — maybe the ioctl is going to wrong driver */
    printf("\n  Checking ioctl compatibility:\n");
    for (int i = 0; i < 4; i++) {
        if (fds[i] < 0) continue;
        int ret = ioctl(fds[i], 0xC0046971, &en); /* VPSS_ENABLE */
        int e = errno;
        int ret2 = ioctl(fds[i], 0x12345678, &en); /* garbage ioctl */
        int e2 = errno;
        printf("  %-15s: valid_ioctl=0x%08x/%d, garbage=0x%08x/%d\n",
               names[i], (uint32_t)ret, e, (uint32_t)ret2, e2);
    }

    for (int i = 0; i < 4; i++)
        if (fds[i] >= 0) close(fds[i]);

    return 0;
}
