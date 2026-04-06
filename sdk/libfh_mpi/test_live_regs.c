/* Minimal test: live ISP regs + GET_FRAME. Output to file, TFTP upload. */
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <stdint.h>
#define I2C_RDWR 0x0707
struct i2c_msg{uint16_t addr;uint16_t flags;uint16_t len;uint8_t*buf;};
struct i2c_rdwr_ioctl_data{struct i2c_msg*msgs;uint32_t nmsgs;};
static int va(int v,uint32_t s,const char*n,uint32_t*p){
    static uint8_t b[80];memset(b,0,80);*(uint32_t*)(b+8)=8;*(uint32_t*)(b+12)=s;
    strncpy((char*)(b+0x1C),n,15);strncpy((char*)(b+0x2C),"anonymous",39);
    int r=ioctl(v,0xC0506D0A,b);*p=(r>=0)?*(uint32_t*)b:0;return r;}
int main(void){
    FILE*f=fopen("/tmp/r.txt","w");if(!f)return 1;
    fprintf(f,"START\n");fflush(f);
    /* proc config */
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
    /* devices */
    int mp=open("/dev/media_process",2);int isp=open("/dev/isp",2);
    open("/dev/enc",2);open("/dev/jpeg",2);int vmm=open("/dev/vmm_userdev",2);
    fprintf(f,"DEV mp=%d isp=%d vmm=%d\n",mp,isp,vmm);fflush(f);
    /* mipi */
    ioctl(isp,0x40036954,(uint8_t[]){0,8,1});
    ioctl(isp,0x40066955,(uint8_t[]){0,1,0,0,0,0});
    /* sensor */
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
        close(i2c);}
    fprintf(f,"MIPI+SNS OK\n");fflush(f);
    sleep(1);
    /* vpss */
    uint32_t sz=0,ph=0;
    ioctl(isp,0xC0046966,&sz);va(vmm,sz,"vpu-sys",&ph);
    uint32_t sm[3]={ph,0,sz};ioctl(isp,0xC00C6964,sm);
    uint32_t q[4]={0,1920,1080,0};ioctl(isp,0xC0106967,q);
    va(vmm,q[3],"vpu-ch-0",&ph);
    uint32_t cs[6]={0,ph,0,q[3],1920,1080};ioctl(isp,0xC0186968,cs);
    uint32_t vi[62];memset(vi,0,sizeof(vi));vi[0]=1920;vi[1]=1080;
    ioctl(isp,0xC0F4696A,vi);uint32_t en=0;ioctl(isp,0xC0046971,&en);
    uint32_t ca[3]={0,1920,1080};ioctl(isp,0xC00C696C,ca);
    uint32_t ch=0;ioctl(isp,0xC0046973,&ch);
    uint32_t vo[2]={0,1};ioctl(isp,0xC00869A4,vo);
    fprintf(f,"VPSS OK\n");fflush(f);
    /* isp ioctls */
    ioctl(isp,0x80086929,(uint32_t[]){0,0});
    ioctl(isp,0x80306926,(uint32_t[12]){0});
    ioctl(isp,0x80786905,(uint8_t[120]){0});
    ioctl(isp,0x40046908,(uint32_t[]){1});
    ioctl(isp,0x40046909,(uint32_t[]){1});
    ioctl(isp,0x690A,(uint32_t[]){0});
    ioctl(isp,0x4004690F,(uint32_t[]){1});
    ioctl(isp,0x40016920,(uint8_t[]){1});
    fprintf(f,"ISP IOCTLS OK\n");fflush(f);
    /* isp hw regs — skip 0x028 to keep MIPI working */
    int mem=open("/dev/mem",2|0x80000);
    volatile uint32_t*R=0;
    if(mem>=0)R=mmap(0,0x3240,3,1,mem,0xE8400000);
    if(R&&R!=(void*)-1){
        R[0x010/4]=0x0003FFFF;R[0x014/4]=0x00001FFF;R[0x018/4]=0x0007FFFF;
        R[0x02C/4]=0x00000080;R[0x030/4]=0x00FEFF82;R[0x034/4]=0x00000005;
        R[0x044/4]=0x00000003;R[0x05C/4]=0x00007FFF;R[0x064/4]=0x0007FFFF;
        R[0x080/4]=0x60502010;R[0x084/4]=0x908F8070;
        R[0x088/4]=0xD0C0B0A0;R[0x08C/4]=0x00FFF0E0;
        R[0x090/4]=0x10301010;R[0x094/4]=0x00101010;
        R[0x098/4]=0x10101010;R[0x09C/4]=0x000F1010;
        R[0x0A0/4]=0x50482820;R[0x0A4/4]=0x69686058;
        R[0x0A8/4]=0xA8987870;R[0x0AC/4]=0xE8E0C8B8;R[0x0B0/4]=0x00FFF8F0;
        R[0x0D0/4]=0x0437077F;
        R[0x0E0/4]=0x00400000;R[0x0E4/4]=0x004A5000;R[0x0E8/4]=0x00437437;
        R[0x0F0/4]=0x00000009;R[0x0F4/4]=0x01010003;
        R[0x104/4]=0xFEDCBA98;R[0x108/4]=0x76543210;
        R[0x114/4]=0x00000C01;R[0x128/4]=0x00000500;
        R[0x130/4]=0x0437077F;R[0x138/4]=0x0437077F;
        R[0x144/4]=0x0437077F;R[0x14C/4]=0x01DF027F;
        R[0x178/4]=0x00021002;R[0x184/4]=0x00010000;
        R[0x1F0/4]=0x03FF0000;R[0x1F4/4]=0x00000001;
        R[0x1F8/4]=0x03FF0000;R[0x1FC/4]=0x00000001;
        R[0x230/4]=0x02000200;R[0x234/4]=0x02000200;
        R[0x250/4]=0x00000002;R[0x254/4]=0x00100010;R[0x258/4]=0x00100010;
        R[0x26C/4]=0x003FF000;R[0x278/4]=0x01000001;
        R[0x2AC/4]=0x000F1F1F;R[0x2B0/4]=0x00470029;
        R[0x2B4/4]=0x0000003F;R[0x2B8/4]=0x238F1F0C;R[0x2BC/4]=0x000023FF;
        R[0x2C0/4]=0x01010101;R[0x2C4/4]=0x01010101;R[0x2C8/4]=0x00000001;
        R[0x2D0/4]=0x000F1F1F;R[0x2D4/4]=0x00470029;R[0x2D8/4]=0x0000003F;
        R[0x2F0/4]=0x80000000;R[0x300/4]=0x80000000;
        R[0x310/4]=0x80000000;R[0x320/4]=0x80000000;
        fprintf(f,"ISP REGS OK\n");fflush(f);
    }
    ioctl(isp,0x6910,0);
    fprintf(f,"KICK OK\n");fflush(f);
    /* wait and check */
    sleep(3);
    if(R){
        fprintf(f,"REG 028=%08x 034=%08x 0F0=%08x 100=%08x\n",
                R[0x028/4],R[0x034/4],R[0x0F0/4],R[0x100/4]);
    }
    /* frame attempts */
    int ret;
    for(int a=0;a<10;a++){
        usleep(200000);
        uint32_t fr[26];memset(fr,0,sizeof(fr));fr[0]=0;
        ret=ioctl(isp,0xC0686970,fr);
        fprintf(f,"F%d=0x%08x\n",a,(uint32_t)ret);fflush(f);
        if(ret==0){
            fprintf(f,"***FRAME***\n");
            for(int i=0;i<12;i++)
                fprintf(f,"[%2d]=0x%08x(%u)\n",i,fr[i],fr[i]);
            fflush(f);
            break;
        }
    }
    fprintf(f,"END\n");
    fclose(f);
    /* tftp upload via exec */
    char*args[]={"tftp","-l","/tmp/r.txt","-r","result.txt","-p","192.168.8.100",0};
    execv("/usr/bin/tftp",args);
    /* if execv fails, try busybox path */
    execv("/bin/busybox",
          (char*[]){"busybox","tftp","-l","/tmp/r.txt","-r","result.txt","-p","192.168.8.100",0});
    return 0;
}
