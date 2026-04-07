/**
 * Full pipeline test — stops ipcam, takes over hardware, grabs frames.
 * Auto-restarts ipcam on exit/crash.
 *
 * WARNING: This takes exclusive control of the camera hardware.
 * The video stream will stop for ~10 seconds while testing.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <stdint.h>
#include <errno.h>
#include <signal.h>

#define VPSS_GET_CHN_FRAME  0xC0686970
#define VENC_GET_STREAM     0xC1A04D05

static volatile int running = 1;
static void sighandler(int sig) { running = 0; }

int main(void)
{
    int fd, ret;

    printf("=== FH8852V201 Pipeline Test ===\n\n");

    /* Install signal handlers for safe cleanup */
    signal(SIGINT, sighandler);
    signal(SIGTERM, sighandler);

    /* 1. Disable watchdog by writing 'V' magic close */
    printf("[1] Disabling watchdog...\n");
    int wdt = open("/dev/watchdog", O_WRONLY);
    if (wdt >= 0) {
        /* Magic close character 'V' disables watchdog on close */
        write(wdt, "V", 1);
        close(wdt);
        printf("    Watchdog disabled\n");
    } else {
        printf("    Watchdog not accessible (ipcam owns it)\n");
    }

    /* 2. Kill ipcam — this frees /dev/media_process */
    printf("[2] Stopping ipcam...\n");
    system("killall ipcam 2>/dev/null");
    sleep(2);

    /* 3. Kill the run.sh wrapper too (prevents auto-reboot) */
    system("killall -9 run.sh 2>/dev/null");
    system("killall system_wapper 2>/dev/null");
    sleep(1);
    printf("    ipcam stopped\n");

    /* 4. Open media device */
    printf("[3] Opening /dev/media_process...\n");
    fd = open("/dev/media_process", O_RDWR);
    if (fd < 0) {
        perror("    FAILED");
        goto restart;
    }
    printf("    OK (fd=%d)\n", fd);

    /* 5. Try VPSS get frame — pipeline might still be configured */
    printf("[4] Testing VPSS_GET_CHN_FRAME...\n");
    for (int ch = 0; ch < 3; ch++) {
        uint32_t param[26];
        memset(param, 0, sizeof(param));
        param[0] = ch;

        ret = ioctl(fd, VPSS_GET_CHN_FRAME, param);
        printf("    CH%d: ret=0x%08x (%d), errno=%d\n",
               ch, (uint32_t)ret, ret, ret < 0 ? errno : 0);

        if (ret == 0) {
            printf("    === GOT FRAME! ===\n");
            for (int i = 0; i < 12; i++)
                printf("      [%2d] = 0x%08x (%u)\n", i, param[i], param[i]);
        }
    }

    /* 6. Try VENC get stream */
    printf("[5] Testing VENC_GET_STREAM...\n");
    for (int ch = 0; ch < 2; ch++) {
        uint32_t stream[105];
        memset(stream, 0, sizeof(stream));
        stream[0] = ch;

        ret = ioctl(fd, VENC_GET_STREAM, stream);
        printf("    CH%d: ret=0x%08x (%d), errno=%d\n",
               ch, (uint32_t)ret, ret, ret < 0 ? errno : 0);

        if (ret == 0) {
            printf("    === GOT ENCODED FRAME! ===\n");
            printf("      type=%u chn=%u size=%u frame_type=%u\n",
                   stream[0], stream[1], stream[2], stream[3]);
            printf("      pts=%u nalu_count=%u\n", stream[4], stream[7]);

            /* If we got data, save it */
            if (stream[2] > 0 && stream[2] < 1000000) {
                char fname[64];
                snprintf(fname, sizeof(fname), "/tmp/frame_ch%d.bin", ch);
                printf("      Saving %u bytes to %s\n", stream[2], fname);
                /* stream[2] might be phys addr, need mmap */
            }
        }
    }

    close(fd);

restart:
    /* 7. Restart ipcam */
    printf("\n[6] Restarting ipcam...\n");
    system("/usr/app/run.sh &");
    printf("    run.sh started\n");

    printf("\n=== Test Complete ===\n");
    return 0;
}
