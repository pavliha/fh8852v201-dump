/**
 * Grab a single video frame from the running VPSS pipeline.
 * Does NOT reinit anything — attaches to ipcam's active pipeline.
 * Saves raw frame data to /tmp/frame.bin for analysis.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <stdint.h>
#include <errno.h>

#define VPSS_GET_CHN_FRAME  0xC0686970
#define VENC_GET_STREAM     0xC1A04D05

int main(int argc, char **argv)
{
    int fd, ret;
    int test_venc = (argc > 1 && argv[1][0] == 'v');

    printf("=== Frame Grab Test (read-only, no reinit) ===\n\n");

    fd = open("/dev/media_process", O_RDWR);
    if (fd < 0) {
        perror("open /dev/media_process");
        return 1;
    }
    printf("Opened /dev/media_process (fd=%d)\n", fd);

    if (test_venc) {
        /* Try to get an encoded stream from channel 0 */
        printf("\nTrying VENC_GET_STREAM on channel 0...\n");
        uint32_t stream[105];
        memset(stream, 0, sizeof(stream));
        stream[0] = 0; /* channel 0 */

        ret = ioctl(fd, VENC_GET_STREAM, stream);
        printf("VENC_GET_STREAM: ret=%d errno=%d\n", ret, ret < 0 ? errno : 0);

        if (ret == 0) {
            printf("  type=%u chn=%u size=%u frame_type=%u\n",
                   stream[0], stream[1], stream[2], stream[3]);
            printf("  pts=%u nalu_count=%u\n", stream[4], stream[7]);
            printf("  SUCCESS — got encoded frame!\n");
        }
    } else {
        /* Try to get a raw frame from VPSS channel 0 */
        printf("\nTrying VPSS_GET_CHN_FRAME on channel 0...\n");
        uint32_t param[26];
        memset(param, 0, sizeof(param));
        param[0] = 0;  /* channel 0 */
        /* No timeout — just try once */

        ret = ioctl(fd, VPSS_GET_CHN_FRAME, param);
        printf("VPSS_GET_CHN_FRAME: ret=%d errno=%d\n", ret, ret < 0 ? errno : 0);

        if (ret == 0) {
            printf("  Raw ioctl output (first 12 uint32s):\n");
            for (int i = 0; i < 12; i++) {
                printf("    [%2d] = 0x%08x (%u)\n", i, param[i], param[i]);
            }

            /* Try to interpret as frame info */
            uint32_t phys_addr = param[2];
            uint32_t width = param[1]; /* might be swapped */
            uint32_t height = param[3];
            printf("\n  Possible frame: phys=0x%08x %ux%u\n",
                   phys_addr, width, height);
            printf("  SUCCESS — got raw frame!\n");
        }

        /* Also try channel 1 (sub stream) */
        printf("\nTrying VPSS_GET_CHN_FRAME on channel 1...\n");
        memset(param, 0, sizeof(param));
        param[0] = 1;
        ret = ioctl(fd, VPSS_GET_CHN_FRAME, param);
        printf("VPSS ch1: ret=%d errno=%d\n", ret, ret < 0 ? errno : 0);
        if (ret == 0) {
            for (int i = 0; i < 6; i++)
                printf("    [%2d] = 0x%08x\n", i, param[i]);
        }

        /* Channel 2 (NNA, 512x288) */
        printf("\nTrying VPSS_GET_CHN_FRAME on channel 2...\n");
        memset(param, 0, sizeof(param));
        param[0] = 2;
        ret = ioctl(fd, VPSS_GET_CHN_FRAME, param);
        printf("VPSS ch2: ret=%d errno=%d\n", ret, ret < 0 ? errno : 0);
        if (ret == 0) {
            for (int i = 0; i < 6; i++)
                printf("    [%2d] = 0x%08x\n", i, param[i]);
        }
    }

    close(fd);
    printf("\n=== Done ===\n");
    return 0;
}
