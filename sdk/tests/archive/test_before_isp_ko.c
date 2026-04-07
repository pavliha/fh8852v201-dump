/* Write ISP regs BEFORE isp.ko is loaded.
 *
 * Strategy:
 * 1. Unload isp.ko (loaded by run.sh during boot)
 * 2. Write ISP registers via /dev/mem (no kernel module conflict!)
 * 3. Reload isp.ko (it finds pre-configured registers)
 * 4. Open /dev/isp, do VPSS init, etc.
 * 5. Start sensor → frames should flow
 *
 * The ISP hardware retains register values when the kernel module
 * is unloaded/reloaded. The module's init reads from the hardware.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
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

static void w(volatile uint32_t*R,uint32_t off,uint32_t val){R[off/4]=val;}

int main(void){
    FILE*out=fopen("/tmp/r.txt","w");
    if(!out)out=stdout;
    fprintf(out,"=== BEFORE ISP.KO TEST ===\n");fflush(out);

    /* Step 1: DON'T unload modules — they're already loaded from boot.
     * Instead, write ISP regs BEFORE opening /dev/isp.
     * fh81_isp_open registers the ISP interrupt on first open.
     * Since we haven't opened /dev/isp yet, NO interrupt handler is running.
     * This is our safe window to write registers! */
    fprintf(out,"[1] Modules already loaded, /dev/isp NOT opened yet\n");fflush(out);

    /* Step 2: Write ISP registers — ISP interrupt NOT active yet! */
    fprintf(out,"[2] Writing ISP regs (before /dev/isp open)\n");fflush(out);
    int mem=open("/dev/mem",O_RDWR|O_SYNC);
    volatile uint32_t*R=NULL;
    if(mem>=0) R=mmap(NULL,0x4000,PROT_READ|PROT_WRITE,MAP_SHARED,mem,0xE8400000);

    if(R&&R!=(void*)-1){
        fprintf(out,"    mmap OK, ID=0x%08x\n",R[0]);fflush(out);

        /* Picture size: 1920x1080 */
        uint32_t ps=(1920-1)|((1080-1)<<16);
        w(R,0x0D0,ps); w(R,0x130,ps); w(R,0x138,ps); w(R,0x144,ps);
        w(R,0x134,0); w(R,0x148,0); /* no crop */
        w(R,0x14C,(640-1)|((480-1)<<16)); /* sub res */

        /* Module enable */
        w(R,0x010,0x0003FFFF); w(R,0x014,0x00001FFF); w(R,0x018,0x0007FFFF);

        /* Critical config */
        w(R,0x02C,0x80); /* ISP gain 1x */
        w(R,0x030,0x00FEFF82); /* ISP config */
        w(R,0x034,0x05); /* enable */
        w(R,0x044,0x03); /* AE mode */

        /* Tone mapping */
        w(R,0x080,0x60502010); w(R,0x084,0x908F8070);
        w(R,0x088,0xD0C0B0A0); w(R,0x08C,0x00FFF0E0);
        w(R,0x090,0x10301010); w(R,0x094,0x00101010);
        w(R,0x098,0x10101010); w(R,0x09C,0x000F1010);
        w(R,0x0A0,0x50482820); w(R,0x0A4,0x69686058);
        w(R,0x0A8,0xA8987870); w(R,0x0AC,0xE8E0C8B8); w(R,0x0B0,0x00FFF8F0);

        /* ISP filter */
        w(R,0x0E0,0x00400000); w(R,0x0E4,0x004A5000);
        w(R,0x0E8,0x00437437);
        w(R,0x0F0,0x09); w(R,0x0F4,0x01010003);

        /* Init markers */
        w(R,0x104,0xFEDCBA98); w(R,0x108,0x76543210);
        w(R,0x114,0x00000C01); w(R,0x128,0x00000500);

        /* Misc */
        w(R,0x178,0x00021002); w(R,0x184,0x00010000);
        w(R,0x1F0,0x03FF0000); w(R,0x1F4,1);
        w(R,0x1F8,0x03FF0000); w(R,0x1FC,1);

        /* AWB */
        w(R,0x230,0x02000200); w(R,0x234,0x02000200);
        w(R,0x250,2); w(R,0x254,0x00100010); w(R,0x258,0x00100010);
        w(R,0x26C,0x003FF000); w(R,0x278,0x01000001);

        /* CCM identity */
        w(R,0xA30,0x200); w(R,0xA34,0); w(R,0xA38,0x200);
        w(R,0xA3C,0); w(R,0xA40,0x200); w(R,0xA44,0);

        /* Sharpness */
        w(R,0x2AC,0x000F1F1F); w(R,0x2B0,0x00470029);
        w(R,0x2B4,0x0000003F); w(R,0x2B8,0x238F1F0C); w(R,0x2BC,0x000023FF);
        w(R,0x2C0,0x01010101); w(R,0x2C4,0x01010101); w(R,0x2C8,1);
        w(R,0x2D0,0x000F1F1F); w(R,0x2D4,0x00470029); w(R,0x2D8,0x0000003F);

        /* Buffer enables */
        w(R,0x2F0,0x80000000); w(R,0x300,0x80000000);
        w(R,0x310,0x80000000); w(R,0x320,0x80000000);

        /* Statistics */
        w(R,0x494,0x9015);
        uint32_t bw=(1920/32)-1, bh=(1080/32)-1;
        w(R,0x490,(bh<<24)|((1920/48-1)<<16));
        w(R,0x4E0,(bw|(bh<<8))|0x0F0F0000);
        w(R,0x4E4,0); w(R,0x4E8,0x1FFF1);
        w(R,0x500,0); w(R,0x504,0); w(R,0x508,bw|(bh<<16)|0x78007800);

        /* Buffer stride */
        uint32_t stride=(((1920*10+7)/8)+7)&~7;
        w(R,0x2E8,stride); w(R,0x2F0,stride|0x80000000);

        /* Clear accumulators */
        w(R,0x13C,0); w(R,0x140,0); w(R,0x150,0); w(R,0x154,0);
        w(R,0x578,0); w(R,0x57C,0);

        munmap((void*)R,0x4000);
        fprintf(out,"    All regs written (60+ registers)\n");fflush(out);
    } else {
        fprintf(out,"    mmap FAIL\n");fflush(out);
    }
    if(mem>=0) close(mem);

    /* Step 3: Modules already loaded, skip reload */

    /* Step 4: NOW open devices — this registers ISP interrupt.
     * ISP registers are pre-configured from step 2. */
    fprintf(out,"[4] Opening devices (ISP interrupt registers HERE)\n");fflush(out);
    int mp=open("/dev/media_process",2);
    int isp=open("/dev/isp",2);
    open("/dev/enc",2);open("/dev/jpeg",2);
    int vmm=open("/dev/vmm_userdev",2);
    fprintf(out,"    mp=%d isp=%d\n",mp,isp);fflush(out);

    /* MIPI */
    ioctl(isp,0x40036954,(uint8_t[]){0,8,1});
    ioctl(isp,0x40066955,(uint8_t[]){0,1,0,0,0,0});

    /* Step 5: VPSS */
    fprintf(out,"[5] VPSS\n");fflush(out);
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

    /* Step 6: ISP ioctls */
    fprintf(out,"[6] ISP ioctls\n");fflush(out);
    ioctl(isp,0x80086929,(uint32_t[]){0,0});
    ioctl(isp,0x80306926,(uint32_t[12]){0});
    ioctl(isp,0x80786905,(uint8_t[120]){0});
    ioctl(isp,0x40046908,(uint32_t[]){1});
    ioctl(isp,0x40046909,(uint32_t[]){1});
    ioctl(isp,0x690A,(uint32_t[]){0});
    ioctl(isp,0x4004690F,(uint32_t[]){1});
    ioctl(isp,0x40016920,(uint8_t[]){1});
    ioctl(isp,0x6910,0);

    /* Step 7: Sensor */
    fprintf(out,"[7] Sensor\n");fflush(out);
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
        for(int i=0;i<52;i++){wb[0]=r[i][0]>>8;wb[1]=r[i][0]&0xFF;wb[2]=r[i][1];ioctl(i2c,I2C_RDWR,&x);}
        usleep(10000);wb[0]=0x31;wb[1]=0x41;wb[2]=0x01;ioctl(i2c,I2C_RDWR,&x);close(i2c);}

    /* Step 8: Wait and check */
    fprintf(out,"[8] Wait 3s\n");fflush(out);
    sleep(3);

    /* Step 9: Check ISP/MIPI status */
    fprintf(out,"[9] Status:\n");fflush(out);
    /* Read ISP regs to check state */
    mem=open("/dev/mem",O_RDWR|O_SYNC);
    if(mem>=0){
        R=mmap(NULL,0x1000,PROT_READ,MAP_SHARED,mem,0xE8400000);
        if(R&&R!=(void*)-1){
            fprintf(out,"  ISP: 010=%08x 030=%08x 034=%08x 0F0=%08x 100=%08x 130=%08x\n",
                    R[0x010/4],R[0x030/4],R[0x034/4],R[0x0F0/4],R[0x100/4],R[0x130/4]);fflush(out);
            munmap((void*)R,0x1000);
        }
        close(mem);
    }

    /* Step 10: Try frame */
    fprintf(out,"[10] Frames:\n");fflush(out);
    int ret;
    for(int a=0;a<10;a++){
        usleep(200000);
        uint32_t fr[26];memset(fr,0,sizeof(fr));fr[0]=0;
        ret=ioctl(isp,0xC0686970,fr);
        fprintf(out,"F%d=0x%08x\n",a,(uint32_t)ret);fflush(out);
        if(ret==0){
            fprintf(out,"*** GOT FRAME! ***\n");
            for(int i=0;i<12;i++)
                fprintf(out,"[%2d]=0x%08x(%u)\n",i,fr[i],fr[i]);
            fflush(out);break;
        }
    }

    fprintf(out,"END\n");fclose(out);
    execl("/bin/busybox","busybox","tftp","-l","/tmp/r.txt","-r","result.txt",
          "-p","192.168.8.100",(char*)0);
    return 0;
}
