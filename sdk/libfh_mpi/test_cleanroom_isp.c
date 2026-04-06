/**
 * Clean-room ISP init test.
 * Order: mmap regs → write ALL → ioctl isp_start → pkg_enable → sensor
 */
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <stdint.h>
#define I2C_RDWR 0x0707
struct i2c_msg{uint16_t addr;uint16_t flags;uint16_t len;uint8_t*buf;};
struct i2c_rdwr_ioctl_data{struct i2c_msg*msgs;uint32_t nmsgs;};

/* From isp_init.c */
typedef struct{volatile uint32_t*regs;int mem_fd;uint32_t width;uint32_t height;} isp_ctx_t;
extern int isp_hw_init(isp_ctx_t*ctx,uint32_t w,uint32_t h);
extern void isp_start_processing(isp_ctx_t*ctx);
extern void isp_cleanup(isp_ctx_t*ctx);

static int va(int v,uint32_t s,const char*n,uint32_t*p){
    static uint8_t b[80];memset(b,0,80);*(uint32_t*)(b+8)=8;*(uint32_t*)(b+12)=s;
    strncpy((char*)(b+0x1C),n,15);strncpy((char*)(b+0x2C),"anonymous",39);
    int r=ioctl(v,0xC0506D0A,b);*p=(r>=0)?*(uint32_t*)b:0;return r;}

int main(void){
    FILE*out=fopen("/tmp/r.txt","w");
    if(!out)out=stdout;
    fprintf(out,"=== CLEAN-ROOM ISP TEST ===\n");fflush(out);

    /* 1. Proc config */
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

    /* 2. Open devices — media_process FIRST */
    int mp=open("/dev/media_process",2);
    int isp_fd=open("/dev/isp",2);
    open("/dev/enc",2);open("/dev/jpeg",2);
    int vmm=open("/dev/vmm_userdev",2);
    fprintf(out,"DEV mp=%d isp=%d vmm=%d\n",mp,isp_fd,vmm);fflush(out);

    /* 3. MIPI init — BEFORE ISP hw regs, BEFORE sensor */
    fprintf(out,"MIPI\n");fflush(out);
    ioctl(isp_fd,0x40036954,(uint8_t[]){0,8,1});
    ioctl(isp_fd,0x40066955,(uint8_t[]){0,1,0,0,0,0});

    /* 4. VPSS init */
    fprintf(out,"VPSS\n");fflush(out);
    uint32_t sz=0,ph=0;
    ioctl(isp_fd,0xC0046966,&sz);va(vmm,sz,"vpu-sys",&ph);
    uint32_t sm[3]={ph,0,sz};ioctl(isp_fd,0xC00C6964,sm);
    uint32_t q[4]={0,1920,1080,0};ioctl(isp_fd,0xC0106967,q);
    va(vmm,q[3],"vpu-ch-0",&ph);
    uint32_t cs[6]={0,ph,0,q[3],1920,1080};ioctl(isp_fd,0xC0186968,cs);
    uint32_t vi[62];memset(vi,0,sizeof(vi));vi[0]=1920;vi[1]=1080;
    ioctl(isp_fd,0xC0F4696A,vi);uint32_t en=0;ioctl(isp_fd,0xC0046971,&en);
    uint32_t ca[3]={0,1920,1080};ioctl(isp_fd,0xC00C696C,ca);
    uint32_t ch=0;ioctl(isp_fd,0xC0046973,&ch);
    uint32_t vo[2]={0,1};ioctl(isp_fd,0xC00869A4,vo);

    /* 5. ISP ioctls (BEFORE hw regs, BEFORE isp_start) */
    fprintf(out,"ISP_IOCTLS\n");fflush(out);
    ioctl(isp_fd,0x80086929,(uint32_t[]){0,0}); /* chip info */
    ioctl(isp_fd,0x80306926,(uint32_t[12]){0}); /* hw cfg */
    ioctl(isp_fd,0x80786905,(uint8_t[120]){0}); /* sensor info */
    ioctl(isp_fd,0x40046908,(uint32_t[]){1});
    ioctl(isp_fd,0x40046909,(uint32_t[]){1});

    /* 6. ISP hardware registers — BEFORE isp_start! */
    fprintf(out,"ISP_HW_INIT\n");fflush(out);
    isp_ctx_t ctx = {0};
    int ret = isp_hw_init(&ctx, 1920, 1080);
    fprintf(out,"isp_hw_init=%d id=0x%08x\n",ret,ctx.regs?ctx.regs[0]:0);fflush(out);
    if(ret<0){fprintf(out,"ISP HW INIT FAILED\n");fclose(out);return 1;}

    /* 7. NOW call isp_start — kernel registers interrupt handler
     * and calls isp_start which sets offset 0x364 = 1.
     * The first ISP interrupt will call init_isp which reads
     * our pre-configured registers. */
    fprintf(out,"ISP_START\n");fflush(out);
    ioctl(isp_fd,0x690A,(uint32_t[]){0}); /* isp_start */
    ioctl(isp_fd,0x4004690F,(uint32_t[]){1}); /* mode */
    ioctl(isp_fd,0x40016920,(uint8_t[]){1}); /* start */

    /* 8. Enable ISP frame processing — triggers first interrupt */
    fprintf(out,"PKG_ENABLE\n");fflush(out);
    isp_start_processing(&ctx);

    /* 9. ISP kick */
    ioctl(isp_fd,0x6910,0);
    fprintf(out,"KICK\n");fflush(out);

    /* 10. Sensor — start LAST, after ISP is ready */
    fprintf(out,"SENSOR\n");fflush(out);
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
    fprintf(out,"STREAMING\n");fflush(out);

    /* 11. Wait for ISP to process frames */
    sleep(3);

    /* 12. Check results */
    if(ctx.regs){
        fprintf(out,"REGS: 034=%08x 0F0=%08x 100=%08x\n",
                ctx.regs[0x034/4],ctx.regs[0x0F0/4],ctx.regs[0x100/4]);fflush(out);
    }

    /* 13. Try GET_FRAME */
    for(int a=0;a<10;a++){
        usleep(200000);
        uint32_t fr[26];memset(fr,0,sizeof(fr));fr[0]=0;
        ret=ioctl(isp_fd,0xC0686970,fr);
        fprintf(out,"F%d=0x%08x\n",a,(uint32_t)ret);fflush(out);
        if(ret==0){
            fprintf(out,"*** GOT FRAME! ***\n");
            for(int i=0;i<12;i++)
                fprintf(out,"[%2d]=0x%08x(%u)\n",i,fr[i],fr[i]);
            fflush(out);break;
        }
    }

    fprintf(out,"END\n");fclose(out);
    isp_cleanup(&ctx);
    /* Upload result */
    execl("/bin/busybox","busybox","tftp","-l","/tmp/r.txt","-r","result.txt",
          "-p","192.168.8.100",(char*)0);
    return 0;
}
