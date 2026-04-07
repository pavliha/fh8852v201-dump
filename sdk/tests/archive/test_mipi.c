#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <stdint.h>
#include <errno.h>

int main(void) {
    printf("=== MIPI CONFIG TEST ===\n"); fflush(stdout);

    /* Write proc config first */
    const char *cfg[][2]={
        {"/proc/driver/vpu","vi_1920_1080"},
        {"/proc/driver/isp","wdr_off"},{"/proc/driver/isp","cir_on"},
        {NULL,NULL}
    };
    for(int i=0;cfg[i][0];i++){
        int fd=open(cfg[i][0],O_WRONLY);
        if(fd>=0){write(fd,cfg[i][1],strlen(cfg[i][1]));close(fd);}
    }

    int mp=open("/dev/media_process",O_RDWR);
    int isp=open("/dev/isp",O_RDWR);
    printf("mp=%d isp=%d\n",mp,isp); fflush(stdout);

    /* Try different ISP_INIT parameters to see if any configure MIPI */
    /* Maybe the ioctl expects {lane_count, width, height, format} */
    uint32_t params[8];

    /* Attempt 1: just 0 (what we've been doing) */
    memset(params,0,sizeof(params));
    int ret=ioctl(isp, 0x690A, params);
    printf("ISP_INIT([0]): ret=%d out=[0x%x,0x%x]\n",ret,params[0],params[1]);
    fflush(stdout);

    /* Check MIPI status */
    system("cat /proc/driver/mipi 2>/dev/null | grep -E 'lane|width|sframe'");
    printf("---\n"); fflush(stdout);

    /* Try writing MIPI config via /proc/driver/mipi */
    int mfd=open("/proc/driver/mipi",O_WRONLY);
    if(mfd>=0){
        /* Try various config strings */
        const char *mipi_cfgs[] = {
            "lane_1", "1lane", "lane1", "mipi_lane_1",
            "sf_1920_1080", "1920_1080_1", NULL
        };
        for(int i=0;mipi_cfgs[i];i++){
            lseek(mfd,0,SEEK_SET);
            ret=write(mfd,mipi_cfgs[i],strlen(mipi_cfgs[i]));
            printf("mipi write '%s': ret=%d errno=%d\n",mipi_cfgs[i],ret,errno);
            fflush(stdout);
        }
        close(mfd);
    } else {
        printf("mipi proc: not writable (errno=%d)\n",errno); fflush(stdout);
    }

    /* Check MIPI again */
    system("cat /proc/driver/mipi 2>/dev/null | grep -E 'lane|width|sframe'");

    printf("=== DONE ===\n");
    return 0;
}
