#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <stdint.h>
#include <errno.h>
#include <stdlib.h>

/* MIPI ioctls — from disassembly of isp.ko mipi_ioctl */
#define MIPI_SET_SIZE   0x40036954  /* 3 bytes: frame size config */
#define MIPI_WRAP_INIT  0x40066955  /* 6 bytes: lane/WDR/VC config → mipiWrap_Init */

int main(void) {
    printf("=== MIPI INIT TEST ===\n"); fflush(stdout);

    int mp=open("/dev/media_process",O_RDWR);
    int isp=open("/dev/isp",O_RDWR);
    printf("mp=%d isp=%d\n",mp,isp); fflush(stdout);

    /* MIPI wrap init — configure 1 lane, no WDR */
    /* From mipiWrap_Init disassembly:
     *   byte[1] → bit 0 of MIPI ctrl reg (lane 0 enable)
     *   byte[2] → bit 1 (lane 1 enable)
     *   byte[3] → bit 2 (lane 2 enable)
     *   byte[4] → bits 16-17 (WDR mode)
     *   byte[5] → bits 12-13 (VC ID)
     * For 1-lane: byte[1]=1, rest=0 */
    uint8_t mipi_cfg[6] = { 0, 1, 0, 0, 0, 0 };

    printf("MIPI_WRAP_INIT(0x40066955): "); fflush(stdout);
    int ret = ioctl(isp, MIPI_WRAP_INIT, mipi_cfg);
    printf("ret=%d errno=%d\n", ret, ret<0?errno:0); fflush(stdout);

    /* Frame size config — try different byte layouts */
    /* 3 bytes could be: width_hi, width_lo, height or similar */
    uint8_t size_cfg[3];
    
    /* Try: {lanes=1, wdr=0, unused=0} */
    size_cfg[0]=1; size_cfg[1]=0; size_cfg[2]=0;
    ret = ioctl(isp, MIPI_SET_SIZE, size_cfg);
    printf("MIPI_SET_SIZE({1,0,0}): ret=%d errno=%d\n", ret, ret<0?errno:0);
    fflush(stdout);

    /* Check MIPI status */
    system("cat /proc/driver/mipi 2>/dev/null | grep -E 'lane|width|sframe|status|Data rate'");

    printf("\n=== DONE ===\n");
    return 0;
}
