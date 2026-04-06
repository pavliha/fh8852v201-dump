#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <stdint.h>
#include <errno.h>
#include <stdlib.h>

#define I2C_RDWR 0x0707
struct i2c_msg{uint16_t addr;uint16_t flags;uint16_t len;uint8_t*buf;};
struct i2c_rdwr_ioctl_data{struct i2c_msg*msgs;uint32_t nmsgs;};

#define VPSS_SET_VI_ATTR 0xC0F4696A
#define VPSS_ENABLE 0xC0046971
#define VPSS_SET_CHN_ATTR 0xC00C696C
#define VPSS_OPEN_CHN 0xC0046973
#define VPSS_SET_VO_MODE 0xC00869A4
#define VPSS_GET_CHN_FRAME 0xC0686970
#define VPSS_SYS_QUERY 0xC0046966
#define VPSS_SYS_SET_MEM 0xC00C6964
#define VPSS_CHN_QUERY 0xC0106967
#define VPSS_CHN_SET_MEM 0xC0186968
#define ISP_INIT 0x690A
#define ISP_KICK 0x6910
#define MIPI_CTRL 0x40036954
#define MIPI_WRAP 0x40066955
#define VMM_ALLOC 0xC0506D0A

static int vmm_alloc(int v,uint32_t sz,const char*nm,uint32_t*ph){
    static uint8_t p[80];memset(p,0,80);*(uint32_t*)(p+8)=8;*(uint32_t*)(p+12)=sz;
    strncpy((char*)(p+0x1C),nm,15);strncpy((char*)(p+0x2C),"anonymous",39);
    int r=ioctl(v,VMM_ALLOC,p);*ph=(r>=0)?*(uint32_t*)p:0;return r;}

int main(void){
    int ret;
    printf("=== FINAL FRAME CAPTURE ===\n\n");fflush(stdout);

    /* Config */
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

    /* Open devices — triggers ISP interrupt registration */
    int mp=open("/dev/media_process",2);
    int isp=open("/dev/isp",2);
    open("/dev/enc",2);open("/dev/jpeg",2);
    int vmm=open("/dev/vmm_userdev",2);
    printf("[1] Devices mp=%d isp=%d\n",mp,isp);fflush(stdout);

    /* Verify ISP interrupt registered */
    printf("[2] IRQ check\n");fflush(stdout);
    system("cat /proc/interrupts | grep ispp");

    /* MIPI init */
    printf("[3] MIPI\n");fflush(stdout);
    ioctl(isp,MIPI_CTRL,(uint8_t[]){0,8,1});
    ioctl(isp,MIPI_WRAP,(uint8_t[]){0,1,0,0,0,0});

    /* VPSS init */
    printf("[4] VPSS\n");fflush(stdout);
    uint32_t sz=0,ph=0;
    ioctl(isp,VPSS_SYS_QUERY,&sz);vmm_alloc(vmm,sz,"vpu-sys",&ph);
    uint32_t sm[3]={ph,0,sz};ioctl(isp,VPSS_SYS_SET_MEM,sm);
    uint32_t q[4]={0,1920,1080,0};ioctl(isp,VPSS_CHN_QUERY,q);
    vmm_alloc(vmm,q[3],"vpu-ch-0",&ph);
    printf("    ch0: phys=0x%08x size=%u\n",ph,q[3]);fflush(stdout);
    uint32_t cs[6]={0,ph,0,q[3],1920,1080};ioctl(isp,VPSS_CHN_SET_MEM,cs);
    uint32_t vi[62];memset(vi,0,sizeof(vi));vi[0]=1920;vi[1]=1080;
    ioctl(isp,VPSS_SET_VI_ATTR,vi);
    uint32_t en=0;ioctl(isp,VPSS_ENABLE,&en);
    uint32_t ca[3]={0,1920,1080};ioctl(isp,VPSS_SET_CHN_ATTR,ca);
    uint32_t ch=0;ioctl(isp,VPSS_OPEN_CHN,&ch);
    uint32_t vo[2]={0,1};ioctl(isp,VPSS_SET_VO_MODE,vo);

    /* Also allocate ISP memory */
    uint32_t isp_ph=0;
    vmm_alloc(vmm,3*1024*1024,"isp",&isp_ph);
    printf("    isp mem: 0x%08x\n",isp_ph);fflush(stdout);

    /* ISP ioctls */
    printf("[5] ISP init\n");fflush(stdout);
    ioctl(isp,0x80086929,(uint32_t[]){0,0});
    ioctl(isp,0x80306926,(uint32_t[12]){0});
    ioctl(isp,0x80786905,(uint8_t[120]){0}); /* sensor info */
    ioctl(isp,0x40046908,(uint32_t[]){1});
    ioctl(isp,0x40046909,(uint32_t[]){1});
    ioctl(isp,ISP_INIT,(uint32_t[]){0});
    ioctl(isp,0x4004690F,(uint32_t[]){1}); /* mode */
    ioctl(isp,0x40016920,(uint8_t[]){1}); /* start */

    /* ISP hardware registers via /dev/mem */
    printf("[6] ISP hw regs\n");fflush(stdout);
    int mem=open("/dev/mem",O_RDWR|O_SYNC);
    volatile uint32_t*R=NULL;
    if(mem>=0) R=mmap(NULL,0x3240,3,1,mem,0xE8400000);
    if(R&&R!=MAP_FAILED){
        /* Reset */
        R[0xF0/4]=(R[0xF0/4]&0xFFFFFFCF)|0x10;
        R[0xF4/4]=(R[0xF4/4]&0xFFFFFFFC)|0x02;
        usleep(1000);
        /* Enable */
        R[0x08/4]=0x07FFFFFF;
        /* Pic size */
        uint32_t ps=(1920-1)|((1080-1)<<16);
        R[0x130/4]=ps;R[0x144/4]=ps;R[0x138/4]=ps;R[0x14C/4]=ps;R[0x0D0/4]=ps;
        /* Tone map + filters */
        R[0x080/4]=0x50402010;R[0x084/4]=0x90807060;
        R[0x088/4]=0xD0C0B0A0;R[0x08C/4]=0x00FFF0E0;
        R[0x090/4]=0x10101010;R[0x094/4]=0x10101010;
        R[0x098/4]=0x10101010;R[0x09C/4]=0x000F1010;
        R[0x0E0/4]=0x400102;R[0x0E4/4]=R[0x0E4/4]|2;
        R[0x1C4/4]=0x0C;R[0x494/4]=0x9015;
        R[0x250/4]=0xFF3983;R[0x254/4]=0x1000100;R[0x258/4]=0x1000100;
        R[0xA30/4]=0x200;R[0xA34/4]=0;R[0xA38/4]=0x200;
        R[0xA3C/4]=0;R[0xA40/4]=0x200;R[0xA44/4]=0;
        /* Buffer stride */
        uint32_t stride=(((1920*10+7)>>3)+7)&0xFFFFFFF8;
        R[0x2E8/4]=stride;R[0x2F0/4]=stride|0x80000000;
        /* ISP output DMA — point to our ISP VMM buffer */
        R[0x2F8/4]=isp_ph;         /* RAW buffer phys addr */
        R[0x300/4]=isp_ph|0x80000000;
        /* Clear stats */
        R[0x13C/4]=0;R[0x140/4]=0;R[0x150/4]=0;R[0x154/4]=0;
        /* Bayer */
        R[0x028/4]=R[0x028/4]&0xFFFFFFE1;
        /* PkgEnable — START */
        R[0x100/4]=4;R[0x034/4]=5;R[0x100/4]=R[0x100/4]|1;
        printf("    Started\n");fflush(stdout);
    }

    /* Sensor */
    printf("[7] Sensor\n");fflush(stdout);
    int i2c=open("/dev/i2c-0",2);
    if(i2c>=0){
        static const uint16_t r[][2]={
            {0x3300,0x03},{0x3422,0xbf},{0x3401,0x00},{0x3440,0x01},
            {0x3442,0x00},{0x3806,0x00},{0x3908,0x5f},{0x3909,0x00},
            {0x3929,0x01},{0x3158,0x01},{0x3159,0x01},{0x315a,0x01},
            {0x315b,0x01},{0x35b3,0x15},{0x3148,0x64},{0x3031,0x00},
            {0x3118,0x01},{0x3119,0x06},{0x3670,0x00},{0x3679,0x02},
            {0x3330,0x00},{0x320e,0x02},{0x3804,0x10},{0x35a1,0x06},
            {0x35a8,0x06},{0x35a9,0x06},{0x35aa,0x06},{0x35ab,0x06},
            {0x35ac,0x06},{0x35ad,0x06},{0x35ae,0x07},{0x35af,0x07},
            {0x333b,0x01},{0x3338,0x08},{0x3339,0x00},{0x3144,0x20},
            {0x3030,0x01},{0x3020,0x8c},{0x3021,0x0a},{0x3024,0xd0},
            {0x3025,0x02},{0x3038,0x06},{0x3039,0x00},{0x303a,0x80},
            {0x303b,0x07},{0x3034,0x04},{0x3035,0x00},{0x3036,0x38},
            {0x3037,0x04},{0x3908,0x51},{0x390a,0x02},{0x3000,0x00},};
        uint8_t wb[3];
        struct i2c_msg m={.addr=0x35,.flags=0,.len=3,.buf=wb};
        struct i2c_rdwr_ioctl_data x={.msgs=&m,.nmsgs=1};
        for(int i=0;i<52;i++){wb[0]=r[i][0]>>8;wb[1]=r[i][0]&0xFF;wb[2]=r[i][1];
            ioctl(i2c,I2C_RDWR,&x);}
        usleep(10000);wb[0]=0x31;wb[1]=0x41;wb[2]=0x01;ioctl(i2c,I2C_RDWR,&x);
        close(i2c);
    }
    printf("    streaming\n");fflush(stdout);

    /* ISP kick */
    ioctl(isp,ISP_KICK,0);

    /* Wait and check */
    printf("[8] Wait 3s\n");fflush(stdout);
    sleep(3);

    printf("[9] Status:\n");fflush(stdout);
    system("cat /proc/interrupts | grep -E 'ispp|veu|mipi'");
    printf("---\n");fflush(stdout);
    system("cat /proc/driver/isp 2>/dev/null | grep -E 'FPS|PIC_START|PIC_END|OVERFLOW'");
    printf("---\n");fflush(stdout);
    system("cat /proc/driver/mipi 2>/dev/null | grep sframe");
    printf("---\n");fflush(stdout);
    system("cat /proc/driver/vpu 2>/dev/null | grep -E 'PIC_END|FINISH|Frame Rate'");

    /* Try frame */
    printf("[10] Frame:\n");fflush(stdout);
    for(int a=0;a<10;a++){
        usleep(200000);
        uint32_t f[26];memset(f,0,sizeof(f));f[0]=0;
        ret=ioctl(isp,VPSS_GET_CHN_FRAME,f);
        printf("    %d: 0x%08x",a,(uint32_t)ret);fflush(stdout);
        if(ret==0){printf(" *** FRAME ***\n");
            for(int i=0;i<12;i++)printf("      [%2d] 0x%08x (%u)\n",i,f[i],f[i]);
            break;}
        printf("\n");fflush(stdout);
    }

    printf("\n=== DONE ===\n");
    return 0;
}
