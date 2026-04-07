/* test_with_regs.c — Steps 0-6 (safe), then ISP reg config via /dev/mem,
 * then sensor start. Register values from Ghidra decompilation of vendor SDK.
 *
 * Key insight: steps 0-6 pass, step 7 (sensor) crashes because ISP DMA
 * has no valid config. We must write ISP regs BETWEEN kick and sensor start.
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
struct i2c_msg { uint16_t addr; uint16_t flags; uint16_t len; uint8_t *buf; };
struct i2c_rdwr_ioctl_data { struct i2c_msg *msgs; uint32_t nmsgs; };

static int vmm_alloc(int fd, uint32_t size, const char *name, uint32_t *phys) {
    static uint8_t b[80];
    memset(b, 0, 80);
    *(uint32_t *)(b + 8) = 8;
    *(uint32_t *)(b + 12) = size;
    strncpy((char *)(b + 0x1C), name, 15);
    strncpy((char *)(b + 0x2C), "anonymous", 35);
    int r = ioctl(fd, 0xC0506D0A, b);
    *phys = (r >= 0) ? *(uint32_t *)b : 0;
    return r;
}

static void W(volatile uint32_t *R, uint32_t off, uint32_t val) { R[off/4] = val; }

int main(void) {
    FILE *out = fopen("/tmp/r.txt", "w");
    if (!out) out = stdout;
    fprintf(out, "=== WITH REGS TEST ===\n"); fflush(out);

    /* Proc config */
    const char *c[][2] = {
        {"/proc/driver/vpu","vi_1920_1080"},{"/proc/driver/vpu","cap_0_1920_1080"},
        {"/proc/driver/vpu","cap_1_800_576"},{"/proc/driver/vpu","cap_2_512_288"},
        {"/proc/driver/vpu","buf_0_2"},{"/proc/driver/vpu","buf_1_3"},
        {"/proc/driver/vpu","buf_2_1"},{"/proc/driver/vpu","cirbuf_on"},
        {"/proc/driver/vpu","cbufsize_80"},
        {"/proc/driver/isp","wdr_off"},{"/proc/driver/isp","cir_on"},
        {"/proc/driver/enc","stm_1572864"},
        {"/proc/driver/jpeg","mem_1_131072"},{"/proc/driver/jpeg","mjpg_0_0"},
        {"/proc/driver/clock","nn_clk,enable,90000000"},{0,0}};
    for (int i=0;c[i][0];i++){int d=open(c[i][0],1);if(d>=0){write(d,c[i][1],strlen(c[i][1]));close(d);}}

    /* Open devices */
    int mp=open("/dev/media_process",2);
    int isp=open("/dev/isp",2);
    open("/dev/enc",2);open("/dev/jpeg",2);
    int vmm_fd=open("/dev/vmm_userdev",2);
    fprintf(out,"DEV mp=%d isp=%d\n",mp,isp);fflush(out);
    if(isp<0){fprintf(out,"NO ISP\n");fclose(out);return 1;}

    /* MIPI */
    ioctl(isp,0x40036954,(uint8_t[]){0,8,1});
    ioctl(isp,0x40066955,(uint8_t[]){0,1,0,0,0,0});
    fprintf(out,"MIPI OK\n");fflush(out);

    /* VPSS */
    uint32_t sz=0,ph=0;
    ioctl(isp,0xC0046966,&sz);vmm_alloc(vmm_fd,sz,"vpu-sys",&ph);
    uint32_t sm[3]={ph,0,sz};ioctl(isp,0xC00C6964,sm);
    uint32_t q[4]={0,1920,1080,0};ioctl(isp,0xC0106967,q);
    vmm_alloc(vmm_fd,q[3],"vpu-ch-0",&ph);
    uint32_t cs[6]={0,ph,0,q[3],1920,1080};ioctl(isp,0xC0186968,cs);
    uint32_t vi[62];memset(vi,0,sizeof(vi));vi[0]=1920;vi[1]=1080;
    ioctl(isp,0xC0F4696A,vi);
    uint32_t en=0;ioctl(isp,0xC0046971,&en);
    uint32_t ca[3]={0,1920,1080};ioctl(isp,0xC00C696C,ca);
    uint32_t ch=0;ioctl(isp,0xC0046973,&ch);
    uint32_t vo[2]={0,1};ioctl(isp,0xC00869A4,vo);
    fprintf(out,"VPSS OK\n");fflush(out);

    /* ISP info ioctls */
    ioctl(isp,0x80086929,(uint32_t[]){0,0});
    ioctl(isp,0x80306926,(uint32_t[12]){0});
    ioctl(isp,0x80786905,(uint8_t[120]){0});
    ioctl(isp,0x40046908,(uint32_t[]){1});
    ioctl(isp,0x40046909,(uint32_t[]){1});
    fprintf(out,"ISP_INFO OK\n");fflush(out);

    /* isp_start (0x690A) */
    ioctl(isp,0x690A,(uint32_t[]){0});
    fprintf(out,"ISP_START OK\n");fflush(out);

    /* mode + start */
    ioctl(isp,0x4004690F,(uint32_t[]){1});
    ioctl(isp,0x40016920,(uint8_t[]){1});
    fprintf(out,"MODE OK\n");fflush(out);

    /* kick */
    ioctl(isp,0x6910,0);
    fprintf(out,"KICK OK\n");fflush(out);

    /* ====== ISP REGISTER CONFIG via /dev/mem ======
     * Values from Ghidra decompilation of vendor isp_startup functions.
     * Write AFTER kick, BEFORE sensor start. */
    fprintf(out,"ISP_REGS...\n");fflush(out);
    int mem=open("/dev/mem",O_RDWR|O_SYNC);
    volatile uint32_t *R=NULL;
    if(mem>=0) R=mmap(NULL,0x1000,PROT_READ|PROT_WRITE,MAP_SHARED,mem,0xE8400000);

    if(R && R!=MAP_FAILED){
        fprintf(out,"  mmap OK id=0x%08x\n",R[0]);fflush(out);

        /* System reset (from isp_core_system_reset) */
        R[0x0F0/4] = (R[0x0F0/4] & ~0x30) | 0x10;
        R[0x0F4/4] = (R[0x0F4/4] & ~0x03) | 0x02;
        usleep(1000);
        fprintf(out,"  reset OK\n");fflush(out);

        /* === isp_startup_Ispp_Init values from Ghidra === */
        /* DISABLE all ISP modules to prevent DMA crash from unconfigured buffers */
        W(R,0x008,0x00000000);  /* all modules bypassed */
        W(R,0x010,0x00000000);  /* module enable 1 = off */
        W(R,0x014,0x00000000);  /* module enable 2 = off */
        W(R,0x018,0x00000000);  /* module enable 3 = off */
        /* Tone mapping curve 1 (Ghidra exact values) */
        W(R,0x080,0x50402010); W(R,0x084,0x90807060);
        W(R,0x088,0xD0C0B0A0); W(R,0x08C,0x00FFF0E0);
        /* Gamma pre-curve */
        W(R,0x090,0x10101010); W(R,0x094,0x10101010);
        W(R,0x098,0x10101010); W(R,0x09C,0x000F1010);
        /* Tone mapping curve 2 */
        W(R,0x0A0,0x30281810); W(R,0x0A4,0x50484038);
        W(R,0x0A8,0x80706058); W(R,0x0AC,0xC8C0A090);
        W(R,0x0B0,0x00E0D8D0); W(R,0x0B4,0); W(R,0x0B8,0);
        /* Statistics */
        W(R,0x494,0x9015);
        /* Clear accumulators */
        W(R,0x13C,0); W(R,0x140,0); W(R,0x150,0);
        W(R,0x154,0); W(R,0x578,0); W(R,0x57C,0);
        fprintf(out,"  ispp OK\n");fflush(out);

        /* === isp_startup_Ispf_Init values from Ghidra === */
        W(R,0x0E0,0x00400102);
        R[0x0E4/4] = R[0x0E4/4] | 2;
        W(R,0x1C4,0x0C);
        /* CCM identity */
        W(R,0xA30,0x200); W(R,0xA34,0); W(R,0xA38,0x200);
        W(R,0xA3C,0); W(R,0xA40,0x200); W(R,0xA44,0);
        /* AWB (Ghidra exact) */
        W(R,0x250,0x00FF3983); W(R,0x254,0x01000100);
        W(R,0x258,0x01000100);
        W(R,0x25C,0x80); W(R,0x260,0x80);
        W(R,0x264,0x80); W(R,0x268,0x80);
        W(R,0x26C,0x0200C000); W(R,0x278,0xC8081A0A);
        /* ISP gain + config */
        W(R,0x02C,0x80);
        fprintf(out,"  ispf OK\n");fflush(out);

        /* === SetInputPicSize (1920x1080) === */
        uint32_t ps=(1920-1)|((1080-1)<<16);
        W(R,0x130,ps); W(R,0x144,ps);
        fprintf(out,"  input_size OK\n");fflush(out);

        /* === SetIspPicSize (1920x1080) from Ghidra === */
        uint32_t bh=(1080/32)-1, bw=(1920/32)-1;
        W(R,0x138,ps); W(R,0x14C,ps);
        W(R,0x490,(bh<<24)|((1920/48-1)<<16));
        W(R,0x0D0,ps);
        W(R,0x500,0); W(R,0x504,0);
        W(R,0x508,bw|(bh<<16)|0x78007800);
        uint32_t raw_stride=((1920*10+7)/8+7)&~7;
        uint32_t q_stride=(((1920/4)*10/8)+7)&~7;
        W(R,0x4E0,(bw|(bh<<8))&0x1FFFFFFF|(R[0x4E0/4]&0xE0000000)|0x0F0F0000);
        W(R,0x4E4,0); W(R,0x4E8,0x1FFF1);
        W(R,0x2E8,raw_stride);
        W(R,0x2F0,raw_stride|0x80000000);
        W(R,0x2F8,0x280);
        W(R,0x300,0x80000280);
        W(R,0x308,q_stride);
        W(R,0x310,q_stride|0x80000000);
        W(R,0x318,q_stride);
        W(R,0x320,q_stride|0x80000000);
        fprintf(out,"  pic_size OK\n");fflush(out);

        /* === PkgEnable (from isp_startup_Isp_PkgEnable) === */
        W(R,0x100,4);
        W(R,0x034,5);
        R[0x100/4] = R[0x100/4] | 1;
        fprintf(out,"  pkg_enable OK\n");fflush(out);

        /* Verify */
        fprintf(out,"  VERIFY: 030=%08x 034=%08x 100=%08x 130=%08x 138=%08x\n",
                R[0x030/4],R[0x034/4],R[0x100/4],R[0x130/4],R[0x138/4]);
        fflush(out);

        munmap((void*)R,0x1000);
    } else {
        fprintf(out,"  MMAP FAIL\n");fflush(out);
    }
    if(mem>=0) close(mem);

    /* Send intermediate result before risky sensor start */
    fflush(out);
    system("busybox tftp -l /tmp/r.txt -r result.txt -p 192.168.8.100 2>/dev/null");

    /* ====== SENSOR START ====== */
    fprintf(out,"SENSOR...\n");fflush(out);
    int i2c=open("/dev/i2c-0",2);
    if(i2c>=0){
        static const uint16_t regs[][2]={
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
        for(int i=0;i<52;i++){wb[0]=regs[i][0]>>8;wb[1]=regs[i][0]&0xFF;wb[2]=regs[i][1];ioctl(i2c,I2C_RDWR,&x);}
        usleep(10000);
        wb[0]=0x31;wb[1]=0x41;wb[2]=0x01;ioctl(i2c,I2C_RDWR,&x);
        close(i2c);
        fprintf(out,"SENSOR OK\n");fflush(out);
    }

    /* Wait + GET_FRAME */
    fprintf(out,"WAIT 3s\n");fflush(out);
    sleep(3);

    fprintf(out,"GET_FRAME:\n");fflush(out);
    for(int a=0;a<10;a++){
        uint32_t fr[26];memset(fr,0,sizeof(fr));
        fr[0]=0; fr[22]=2000;
        int ret=ioctl(isp,0xC0686970,fr);
        fprintf(out,"F%d=%d (0x%08x)\n",a,ret,(uint32_t)ret);fflush(out);
        if(ret==0){
            fprintf(out,"*** GOT FRAME! ***\n");
            for(int i=0;i<12;i++)fprintf(out,"[%2d]=0x%08x\n",i,fr[i]);
            fflush(out);break;
        }
    }

    fprintf(out,"END\n");fclose(out);
    /* Send result via TFTP */
    system("busybox tftp -l /tmp/r.txt -r result.txt -p 192.168.8.100");
    return 0;
}
