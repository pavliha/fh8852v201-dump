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
    printf("=== CORRECT ISP REGS TEST ===\n\n");fflush(stdout);

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

    int mp=open("/dev/media_process",2);
    int isp=open("/dev/isp",2);
    open("/dev/enc",2);open("/dev/jpeg",2);
    int vmm=open("/dev/vmm_userdev",2);

    /* MIPI */
    ioctl(isp,MIPI_CTRL,(uint8_t[]){0,8,1});
    ioctl(isp,MIPI_WRAP,(uint8_t[]){0,1,0,0,0,0});

    /* VPSS */
    uint32_t sz=0,ph=0;
    ioctl(isp,VPSS_SYS_QUERY,&sz);vmm_alloc(vmm,sz,"vpu-sys",&ph);
    uint32_t sm[3]={ph,0,sz};ioctl(isp,VPSS_SYS_SET_MEM,sm);
    uint32_t q[4]={0,1920,1080,0};ioctl(isp,VPSS_CHN_QUERY,q);
    vmm_alloc(vmm,q[3],"vpu-ch-0",&ph);
    uint32_t cs[6]={0,ph,0,q[3],1920,1080};ioctl(isp,VPSS_CHN_SET_MEM,cs);
    uint32_t vi[62];memset(vi,0,sizeof(vi));vi[0]=1920;vi[1]=1080;
    ioctl(isp,VPSS_SET_VI_ATTR,vi);
    uint32_t en=0;ioctl(isp,VPSS_ENABLE,&en);
    uint32_t ca[3]={0,1920,1080};ioctl(isp,VPSS_SET_CHN_ATTR,ca);
    uint32_t ch=0;ioctl(isp,VPSS_OPEN_CHN,&ch);
    uint32_t vo[2]={0,1};ioctl(isp,VPSS_SET_VO_MODE,vo);
    printf("[1] VPSS OK\n");fflush(stdout);

    /* ISP ioctls */
    ioctl(isp,0x80086929,(uint32_t[]){0,0});
    ioctl(isp,0x80306926,(uint32_t[12]){0});
    ioctl(isp,0x80786905,(uint8_t[120]){0});
    ioctl(isp,0x40046908,(uint32_t[]){1});
    ioctl(isp,0x40046909,(uint32_t[]){1});
    ioctl(isp,ISP_INIT,(uint32_t[]){0});
    ioctl(isp,0x4004690F,(uint32_t[]){1});
    ioctl(isp,0x40016920,(uint8_t[]){1});

    /* ISP hardware — CORRECT values from live camera dump */
    int mem=open("/dev/mem",O_RDWR|O_SYNC);
    volatile uint32_t*R=NULL;
    if(mem>=0) R=mmap(NULL,0x3240,3,1,mem,0xE8400000);
    if(R&&R!=(void*)-1){
        /* Use EXACT values from live working camera */
        R[0x008/4]=0x0003FFFF;  /* module enable — NOT 0x07FFFFFF */
        R[0x00C/4]=0x00FEFF82;  /* CRITICAL config — was 0 */
        uint32_t ps=(1920-1)|((1080-1)<<16);
        R[0x0D0/4]=ps;
        R[0x080/4]=0x60502010;  /* tone map from live */
        R[0x084/4]=0x10301010;
        R[0x088/4]=0x50482820;
        R[0x08C/4]=0x00FFF8F0;
        R[0x138/4]=ps;
        R[0x2B0/4]=0x00470029;  /* buffer config from live */
        R[0x2B4/4]=0x01010101;
        R[0x2B8/4]=0x000F1F1F;
        R[0x2C0/4]=0x80000000;
        R[0x300/4]=0x80000000;
        R[0x308/4]=0x80000000;
        R[0x310/4]=0x80000000;
        R[0x250/4]=0x02000200;  /* AWB from live */
        /* Enable = 9 from live */
        R[0x034/4]=0x00000009;
        printf("[2] ISP regs (live values)\n");fflush(stdout);
    }

    /* ISP kick */
    ioctl(isp,ISP_KICK,0);

    /* Sensor */
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
    printf("[3] Sensor streaming\n");fflush(stdout);

    /* Wait */
    printf("[4] Wait 3s\n");fflush(stdout);
    sleep(3);

    /* Check */
    printf("[5] Status:\n");fflush(stdout);
    system("cat /proc/interrupts | grep -E 'ispp|veu'");
    printf("---\n");fflush(stdout);
    system("cat /proc/driver/isp 2>/dev/null | grep -E 'FPS|PIC_START|PIC_END|OVERFLOW'");
    printf("---\n");fflush(stdout);
    system("cat /proc/driver/mipi 2>/dev/null | grep sframe");
    printf("---\n");fflush(stdout);
    system("cat /proc/driver/vpu 2>/dev/null | grep -E 'FINISH|Frame Rate'");

    /* ISP regs after */
    if(R){
        printf("\nISP regs:\n");fflush(stdout);
        printf("  008=%08x 00C=%08x 034=%08x 100=%08x\n",
               R[0x008/4],R[0x00C/4],R[0x034/4],R[0x100/4]);
        printf("  0D0=%08x 138=%08x\n",R[0x0D0/4],R[0x138/4]);
        fflush(stdout);
    }

    /* Frame */
    printf("[6] Frame:\n");fflush(stdout);
    for(int a=0;a<5;a++){
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
