#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <stdint.h>
#include <errno.h>

int main(void){
    printf("=== ISP_INIT PARAM EXPLORATION ===\n\n");fflush(stdout);

    /* Config + open */
    const char*cfg[][2]={
        {"/proc/driver/vpu","vi_1920_1080"},{"/proc/driver/vpu","cap_0_1920_1080"},
        {"/proc/driver/vpu","cap_1_800_576"},{"/proc/driver/vpu","cap_2_512_288"},
        {"/proc/driver/vpu","buf_0_2"},{"/proc/driver/vpu","buf_1_3"},
        {"/proc/driver/vpu","buf_2_1"},{"/proc/driver/vpu","cirbuf_on"},
        {"/proc/driver/vpu","cbufsize_80"},
        {"/proc/driver/isp","wdr_off"},{"/proc/driver/isp","cir_on"},
        {"/proc/driver/enc","stm_1572864"},
        {"/proc/driver/jpeg","mem_1_131072"},{"/proc/driver/jpeg","mjpg_0_0"},
        {"/proc/driver/clock","nn_clk,enable,90000000"},{NULL,NULL}};
    for(int i=0;cfg[i][0];i++){int fd=open(cfg[i][0],1);if(fd>=0){write(fd,cfg[i][1],strlen(cfg[i][1]));close(fd);}}

    int mp=open("/dev/media_process",2);
    int isp=open("/dev/isp",2);
    open("/dev/enc",2); open("/dev/jpeg",2);
    printf("mp=%d isp=%d\n",mp,isp);fflush(stdout);

    /* MIPI + sensor first */
    ioctl(isp,0x40036954,(uint8_t[]){0,8,1});
    ioctl(isp,0x40066955,(uint8_t[]){0,1,0,0,0,0});

    /* Try ISP_INIT with different sized param buffers */
    printf("[1] ISP_INIT with various params:\n");fflush(stdout);

    /* The isp_core_init code does: ioctl(fd, 0x690A, local_24)
     * where local_24 is on the stack. The ioctl format code
     * 0x690A = _IO('i', 0x0A) = no direction, no size.
     * But the kernel might still read/write the pointer arg. */

    /* Try with 0 */
    int ret = ioctl(isp, 0x690A, 0);
    printf("    arg=0: ret=%d\n",ret);fflush(stdout);

    /* Try with a buffer that shows what kernel writes back */
    static uint32_t buf[32];
    memset(buf, 0xAA, sizeof(buf));
    ret = ioctl(isp, 0x690A, buf);
    printf("    arg=buf: ret=%d\n",ret);fflush(stdout);
    printf("    output: ");
    for(int i=0;i<8;i++) printf("0x%08x ",buf[i]);
    printf("\n");fflush(stdout);

    /* Check if the ISP has allocated its own memory */
    printf("\n[2] ISP VMM after init:\n");fflush(stdout);
    system("cat /proc/driver/vmm 2>/dev/null | grep -E 'BLOCK|total'");

    /* Try all remaining ioctls from 0x6900 to 0x6950 */
    printf("\n[3] Scanning ISP ioctls 0x6900-0x6915:\n");fflush(stdout);
    for(int nr=0; nr<=0x15; nr++){
        uint32_t code;
        static uint32_t p[16];
        memset(p,0,sizeof(p));

        /* Try as _IO (no data) */
        code = 0x6900 | nr;
        ret = ioctl(isp, code, p);
        if(ret==0) printf("    _IO(0x%04x): ret=0 SUCCESS!\n",code);

        /* Try as _IOW (write 4 bytes) */
        code = 0x40040000 | (0x6900 | nr);
        p[0]=0;
        ret = ioctl(isp, code, p);
        if(ret==0) printf("    _IOW4(0x%08x): ret=0 SUCCESS!\n",code);

        /* Try as _IOR (read 4 bytes) */
        code = 0x80040000 | (0x6900 | nr);
        ret = ioctl(isp, code, p);
        if(ret==0) printf("    _IOR4(0x%08x): ret=0 out=0x%x\n",code,p[0]);

        fflush(stdout);
    }

    /* Check ISP status after all ioctls */
    printf("\n[4] Status after ioctl scan:\n");fflush(stdout);
    system("cat /proc/driver/isp 2>/dev/null | grep -E 'FPS|PIC_START|PIC_END|OVERFLOW'");
    system("cat /proc/driver/mipi 2>/dev/null | grep sframe");

    printf("\n=== DONE ===\n");
    return 0;
}
