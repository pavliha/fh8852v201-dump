/* Find which ioctl crashes the camera by testing one at a time.
 * Writes checkpoint to /tmp/etc/config/checkpoint (survives reboot).
 * Run multiple times — each run advances past the last crash. */
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <stdint.h>

static void checkpoint(const char*msg){
    int fd=open("/tmp/etc/config/checkpoint",O_WRONLY|O_CREAT|O_TRUNC,0644);
    if(fd>=0){write(fd,msg,strlen(msg));close(fd);sync();}
}
static int read_checkpoint(char*buf,int sz){
    int fd=open("/tmp/etc/config/checkpoint",O_RDONLY);
    if(fd<0)return -1;
    int n=read(fd,buf,sz-1);close(fd);
    if(n>0)buf[n]=0;
    return n;
}

int main(void){
    char ckpt[64]="";
    read_checkpoint(ckpt,sizeof(ckpt));

    /* Write to console so we see output even if crash */
    int con=open("/dev/console",O_WRONLY);
    if(con<0)con=1; /* fallback to stdout */

    char msg[128];
    int n;

    n=snprintf(msg,sizeof(msg),"=== CRASH FINDER (last=%s) ===\n",ckpt[0]?ckpt:"none");
    write(con,msg,n);

    /* Config */
    const char*c[][2]={
        {"/proc/driver/vpu","vi_1920_1080"},{"/proc/driver/vpu","cap_0_1920_1080"},
        {"/proc/driver/vpu","cap_1_800_576"},{"/proc/driver/vpu","cap_2_512_288"},
        {"/proc/driver/vpu","buf_0_2"},{"/proc/driver/vpu","buf_1_3"},
        {"/proc/driver/vpu","buf_2_1"},{"/proc/driver/vpu","cirbuf_on"},
        {"/proc/driver/vpu","cbufsize_80"},
        {"/proc/driver/isp","wdr_off"},{"/proc/driver/isp","cir_on"},
        {"/proc/driver/enc","stm_1572864"},
        {"/proc/driver/jpeg","mem_1_131072"},{"/proc/driver/jpeg","mjpg_0_0"},
        {"/proc/driver/clock","nn_clk,enable,90000000"},{0,0}};
    for(int i=0;c[i][0];i++){int d=open(c[i][0],1);if(d>=0){write(d,c[i][1],strlen(c[i][1]));close(d);}}

    int mp=open("/dev/media_process",2);
    int isp=open("/dev/isp",2);
    open("/dev/enc",2);open("/dev/jpeg",2);

    checkpoint("OPEN");
    n=snprintf(msg,sizeof(msg),"OPEN mp=%d isp=%d\n",mp,isp);write(con,msg,n);

    /* Test each ioctl one by one */
    int ret;

    checkpoint("MIPI_CTRL");
    ret=ioctl(isp,0x40036954,(uint8_t[]){0,8,1});
    n=snprintf(msg,sizeof(msg),"MIPI_CTRL=%d\n",ret);write(con,msg,n);

    checkpoint("MIPI_WRAP");
    ret=ioctl(isp,0x40066955,(uint8_t[]){0,1,0,0,0,0});
    n=snprintf(msg,sizeof(msg),"MIPI_WRAP=%d\n",ret);write(con,msg,n);

    checkpoint("CHIP_INFO");
    ret=ioctl(isp,0x80086929,(uint32_t[]){0,0});
    n=snprintf(msg,sizeof(msg),"CHIP_INFO=%d\n",ret);write(con,msg,n);

    checkpoint("HW_CFG");
    ret=ioctl(isp,0x80306926,(uint32_t[12]){0});
    n=snprintf(msg,sizeof(msg),"HW_CFG=%d\n",ret);write(con,msg,n);

    checkpoint("SNS_INFO");
    uint8_t si[120];memset(si,0,120);
    ret=ioctl(isp,0x80786905,si);
    n=snprintf(msg,sizeof(msg),"SNS_INFO=%d\n",ret);write(con,msg,n);

    checkpoint("CFG06");
    uint32_t v=0;
    ret=ioctl(isp,0x40046906,&v);
    n=snprintf(msg,sizeof(msg),"CFG06=%d\n",ret);write(con,msg,n);

    checkpoint("CFG07");
    v=0x203;
    ret=ioctl(isp,0x40046907,&v);
    n=snprintf(msg,sizeof(msg),"CFG07=%d\n",ret);write(con,msg,n);

    checkpoint("CFG08");
    v=1;ret=ioctl(isp,0x40046908,&v);
    n=snprintf(msg,sizeof(msg),"CFG08=%d\n",ret);write(con,msg,n);

    checkpoint("CFG09");
    v=1;ret=ioctl(isp,0x40046909,&v);
    n=snprintf(msg,sizeof(msg),"CFG09=%d\n",ret);write(con,msg,n);

    checkpoint("ISP_INIT");
    ret=ioctl(isp,0x690A,(uint32_t[]){0});
    n=snprintf(msg,sizeof(msg),"ISP_INIT=%d\n",ret);write(con,msg,n);

    checkpoint("MODE");
    v=1;ret=ioctl(isp,0x4004690F,&v);
    n=snprintf(msg,sizeof(msg),"MODE=%d\n",ret);write(con,msg,n);

    checkpoint("START");
    ret=ioctl(isp,0x40016920,(uint8_t[]){1});
    n=snprintf(msg,sizeof(msg),"START=%d\n",ret);write(con,msg,n);

    checkpoint("KICK");
    ret=ioctl(isp,0x6910,0);
    n=snprintf(msg,sizeof(msg),"KICK=%d\n",ret);write(con,msg,n);

    checkpoint("DONE");
    n=snprintf(msg,sizeof(msg),"ALL PASSED\n");write(con,msg,n);

    /* Clean up checkpoint */
    unlink("/tmp/etc/config/checkpoint");
    return 0;
}
