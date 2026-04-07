/* test_irq_debug.c — Why aren't ISP interrupts firing?
 * Check MIPI status, try different pkg_enable ordering,
 * and test if the ISP-MIPI connection works.
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

static int vmm_alloc(int fd,uint32_t sz,const char*n,uint32_t*p){
    static uint8_t b[80];memset(b,0,80);*(uint32_t*)(b+8)=8;*(uint32_t*)(b+12)=sz;
    strncpy((char*)(b+0x1C),n,15);strncpy((char*)(b+0x2C),"anonymous",35);
    int r=ioctl(fd,0xC0506D0A,b);*p=(r>=0)?*(uint32_t*)b:0;return r;}

#define W(off,val) do{R[(off)/4]=(val);}while(0)
#define RD(off) R[(off)/4]

static void dump_irq(FILE*out){
    FILE*p=fopen("/proc/interrupts","r");
    if(!p)return;
    char line[256];
    while(fgets(line,sizeof(line),p))
        if(strstr(line,"ispp")||strstr(line,"veu"))
            fprintf(out,"  %s",line);
    fclose(p);fflush(out);
}

static void dump_mipi(FILE*out,int mem){
    volatile uint32_t*M=mmap(NULL,0x1000,PROT_READ,MAP_SHARED,mem,0xE0200000);
    if(M&&M!=MAP_FAILED){
        fprintf(out,"MIPI: 000=%08x 004=%08x 008=%08x 00C=%08x\n",M[0],M[1],M[2],M[3]);
        fprintf(out,"      010=%08x 014=%08x 018=%08x 01C=%08x\n",M[4],M[5],M[6],M[7]);
        fprintf(out,"      020=%08x 080=%08x 084=%08x 088=%08x\n",M[8],M[0x80/4],M[0x84/4],M[0x88/4]);
        fprintf(out,"      090=%08x 094=%08x 098=%08x\n",M[0x90/4],M[0x94/4],M[0x98/4]);
        munmap((void*)M,0x1000);
    }
    /* Also check MIPI D-PHY at 0xE0210000 */
    volatile uint32_t*P=mmap(NULL,0x1000,PROT_READ,MAP_SHARED,mem,0xE0210000);
    if(P&&P!=MAP_FAILED){
        fprintf(out,"DPHY: 000=%08x 004=%08x 008=%08x 00C=%08x 010=%08x\n",
                P[0],P[1],P[2],P[3],P[4]);
        munmap((void*)P,0x1000);
    }
    fflush(out);
}

int main(void){
    FILE*out=fopen("/tmp/etc/config/result.txt","w");
    if(!out)out=fopen("/tmp/r.txt","w");
    if(!out)out=stdout;
    fprintf(out,"=== IRQ DEBUG ===\n");fflush(out);

    int wd=open("/dev/watchdog",1);
    if(wd>=0)write(wd,"\0",1);

    system("killall ipcam 2>/dev/null");
    usleep(500000);
    if(wd>=0)write(wd,"\0",1);

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
    int vmm_fd=open("/dev/vmm_userdev",2);
    fprintf(out,"DEV mp=%d isp=%d\n",mp,isp);fflush(out);
    if(isp<0){fprintf(out,"NO ISP\n");fclose(out);return 1;}
    if(wd>=0)write(wd,"\0",1);

    /* MIPI */
    ioctl(isp,0x40036954,(uint8_t[]){0,8,1});
    ioctl(isp,0x40066955,(uint8_t[]){0,1,0,0,0,0});

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

    ioctl(isp,0x80086929,(uint32_t[]){0,0});
    ioctl(isp,0x80306926,(uint32_t[12]){0});
    ioctl(isp,0x80786905,(uint8_t[120]){0});
    ioctl(isp,0x40046908,(uint32_t[]){1});
    ioctl(isp,0x40046909,(uint32_t[]){1});

    /* DON'T call 0x690A yet — start sensor first, THEN isp_start */
    fprintf(out,"INIT OK (no isp_start yet)\n");fflush(out);
    if(wd>=0)write(wd,"\0",1);

    int mem=open("/dev/mem",O_RDWR|O_SYNC);
    volatile uint32_t*R=mmap(NULL,0x4000,PROT_READ|PROT_WRITE,MAP_SHARED,mem,0xE8400000);
    if(!R||R==MAP_FAILED){fprintf(out,"MMAP FAIL\n");fclose(out);return 1;}

    /* Check MIPI before sensor */
    fprintf(out,"MIPI BEFORE SENSOR:\n");
    dump_mipi(out,mem);

    /* Start sensor FIRST — let MIPI frames flow before ISP is started */
    fprintf(out,"STARTING SENSOR...\n");fflush(out);
    int i2c=open("/dev/i2c-0",2);
    if(i2c>=0){
        static const uint16_t r[][2]={
            {0x3300,0x03},{0x3422,0xbf},{0x3401,0x00},{0x3440,0x01},{0x3442,0x00},
            {0x3806,0x00},{0x3908,0x5f},{0x3909,0x00},{0x3929,0x01},{0x3158,0x01},
            {0x3159,0x01},{0x315a,0x01},{0x315b,0x01},{0x35b3,0x15},{0x3148,0x64},
            {0x3031,0x00},{0x3118,0x01},{0x3119,0x06},{0x3670,0x00},{0x3679,0x02},
            {0x3330,0x00},{0x320e,0x02},{0x3804,0x10},{0x35a1,0x06},{0x35a8,0x06},
            {0x35a9,0x06},{0x35aa,0x06},{0x35ab,0x06},{0x35ac,0x06},{0x35ad,0x06},
            {0x35ae,0x07},{0x35af,0x07},{0x333b,0x01},{0x3338,0x08},{0x3339,0x00},
            {0x3144,0x20},{0x3030,0x01},{0x3020,0x8c},{0x3021,0x0a},{0x3024,0xd0},
            {0x3025,0x02},{0x3038,0x06},{0x3039,0x00},{0x303a,0x80},{0x303b,0x07},
            {0x3034,0x04},{0x3035,0x00},{0x3036,0x38},{0x3037,0x04},{0x3908,0x51},
            {0x390a,0x02},{0x3000,0x00},};
        uint8_t wb[3];
        struct i2c_msg m={.addr=0x35,.flags=0,.len=3,.buf=wb};
        struct i2c_rdwr_ioctl_data x={.msgs=&m,.nmsgs=1};
        for(int i=0;i<52;i++){wb[0]=r[i][0]>>8;wb[1]=r[i][0]&0xFF;wb[2]=r[i][1];ioctl(i2c,I2C_RDWR,&x);}
        usleep(10000);
        wb[0]=0x31;wb[1]=0x41;wb[2]=0x01;ioctl(i2c,I2C_RDWR,&x);
        close(i2c);
    }
    fprintf(out,"SENSOR ON\n");fflush(out);
    if(wd>=0)write(wd,"\0",1);
    usleep(500000); /* let frames flow */
    if(wd>=0)write(wd,"\0",1);

    /* Check MIPI after sensor — are frames arriving? */
    fprintf(out,"MIPI AFTER SENSOR:\n");
    dump_mipi(out,mem);

    /* NOW write ISP registers */
    fprintf(out,"WRITING ISP REGS...\n");fflush(out);
    R[0x0F0/4]=(R[0x0F0/4]&~0x30)|0x10;
    R[0x0F4/4]=(R[0x0F4/4]&~0x03)|0x02;
    usleep(1000);
    /* Full register set (abbreviated — same as test_full_regs) */
    W(0x008,0x07FFFFFF);
    W(0x080,0x50402010);W(0x084,0x90807060);W(0x088,0xD0C0B0A0);W(0x08C,0x00FFF0E0);
    W(0x090,0x10101010);W(0x094,0x10101010);W(0x098,0x10101010);W(0x09C,0x000F1010);
    W(0x0A0,0x30281810);W(0x0A4,0x50484038);W(0x0A8,0x80706058);W(0x0AC,0xC8C0A090);
    W(0x0B0,0x00E0D8D0);W(0x0B4,0);W(0x0B8,0);W(0x494,0x9015);
    W(0x13C,0);W(0x140,0);W(0x150,0);W(0x154,0);W(0x578,0);W(0x57C,0);
    W(0x0E0,0x00400102);R[0x0E4/4]=R[0x0E4/4]|2;
    W(0x400,1);W(0x404,0xF001F4A0);W(0x408,0xF001F4A0);W(0x40C,0xF001F4A0);
    W(0x410,0xF001F4A0);W(0x414,0x1FFF009C);W(0x1C4,0x0C);
    W(0x510,0x400);W(0x514,0x3FFFFBFF);W(0x518,0x3FF00001);W(0x51C,0x3FEFFC00);
    W(0x520,0x000FFBFE);W(0x50C,0x100FFF);
    W(0x548,0xFFF);W(0x54C,0x0FFF0000);W(0x550,0xFFF);W(0x554,0x0FFF0FFF);
    W(0x558,0x0FFF0000);W(0x55C,0x0FFF0FFF);W(0x538,0x40);W(0x53C,0);
    W(0x570,0x020002E4);W(0x574,0x035C0200);
    W(0xC2C,0x0404);W(0xC30,0x10000404);W(0xC34,0x786E6868);W(0xC38,0x7E7A7A78);
    W(0xC3C,0x08000000);W(0xC40,0x0A140204);W(0xC44,0x0A141325);W(0xC48,0x10101325);
    W(0xC4C,0x03FF0000);W(0xC50,0x01400C04);W(0xC54,0x7C000606);W(0xC58,0x7F7D7D7C);
    W(0xC5C,0x06000000);W(0xC60,0x1A140204);W(0xC64,0x1A143425);W(0xC68,0x48483425);
    W(0xC6C,0x03FC0000);W(0xC70,0x4000);
    W(0xAAC,0);W(0xAB0,0x160000);W(0xAB4,0x100010);W(0xAB8,0x801);
    W(0xABC,0x1000805B);W(0xAC0,0x80030000);W(0xAC4,0x11100A10);
    for(int i=0;i<24;i++)W(0xAC8+i*4,0x400040);
    W(0xB28,0xAA00A0);W(0xB2C,0x02800280);W(0xB30,0x07800780);W(0xB34,0x01200120);
    W(0xB38,0x00360036);W(0xB3C,0x00260026);W(0xB40,0x30181414);W(0xB44,0x4000);W(0xB48,0x80404000);
    W(0xE00,0x40);W(0xE04,0x800000);W(0xE08,0xCC011C);W(0xE0C,0x10101);
    W(0xE10,0xFF0000);W(0xE14,0);W(0xE18,0x029C0054);W(0xE1C,0x029C0260);
    W(0xE20,0x01640260);W(0xE24,0x016403AC);W(0xE28,0x016401A4);W(0xE2C,0x036401A4);
    W(0xA30,0x200);W(0xA34,0);W(0xA38,0x200);W(0xA3C,0);W(0xA40,0x200);W(0xA44,0);
    W(0xC74,0x105);
    W(0xA60,0x02590132);W(0xA64,0x0F530074);W(0xA68,0x02000EAD);W(0xA6C,0x0E530200);
    W(0xA70,0x00000FAD);W(0xA74,0x04000400);W(0xA78,0x3FF3FF11);
    W(0x250,0x00FF3983);W(0x254,0x01000100);W(0x258,0x01000100);
    W(0x25C,0x80);W(0x260,0x80);W(0x264,0x80);W(0x268,0x80);
    W(0x26C,0x0200C000);W(0x278,0xC8081A0A);W(0x27C,0x02408C23);
    W(0x280,0x02D0A026);W(0x284,0x06511C37);W(0x288,0x1603A898);W(0x28C,0x30DA29F2);
    W(0x290,0x3D0EAF6D);W(0x294,0x3F8FC7E5);W(0x298,0x3FEFF7FB);W(0x29C,0x3FFFFFFE);
    W(0x2A8,2);W(0x2C0,0x3523120A);W(0x2C4,0x9B5E9B5B);W(0x2C8,0xFF);
    W(0x590,0x0D);W(0x594,0x30303);W(0x598,0x1400A0);W(0x59C,0xFC0DE0);
    W(0x5A0,0x1800C00);W(0x5A4,0x334);W(0x5A8,0x3E6);W(0x5AC,0xFF);
    W(0x02C,0x80);
    /* Write vendor 030 value */
    W(0x030,0x00FEFF82);
    uint32_t ps=(1920-1)|((1080-1)<<16);
    W(0x130,ps);W(0x144,ps);
    uint32_t bh=(1080/32)-1,bw=(1920/32)-1;
    W(0x138,ps);W(0x14C,ps);W(0x490,(bh<<24)|((1920/48-1)<<16));W(0x0D0,ps);
    W(0x500,0);W(0x504,0);W(0x508,bw|(bh<<16)|0x78007800);
    uint32_t rs=((1920*10+7)/8+7)&~7,qs=(((1920/4)*10/8)+7)&~7;
    R[0x4E0/4]=(RD(0x4E0)&0xE0000000)|((bw|(bh<<8))&0x1FFFFFFF)|0x0F0F0000;
    W(0x4E4,0);W(0x4E8,0x1FFF1);W(0x2E8,rs);W(0x2F0,rs|0x80000000);
    W(0x2F8,0x280);W(0x300,0x80000280);W(0x308,qs);W(0x310,qs|0x80000000);
    W(0x318,qs);W(0x320,qs|0x80000000);W(0x41C,qs&0xFFF8);W(0xAA8,qs&0xFFF8);
    W(0x648,0x80000348);W(0x658,0x80000348);W(0x134,0);W(0x148,0);
    R[0x02C/4]=R[0x02C/4]|0x400;
    fprintf(out,"ISP REGS DONE 030=%08x\n",RD(0x030));fflush(out);
    if(wd>=0)write(wd,"\0",1);

    /* NOW call isp_start — AFTER regs AND after sensor */
    fprintf(out,"ISP_START...\n");fflush(out);
    ioctl(isp,0x690A,(uint32_t[]){0});
    ioctl(isp,0x4004690F,(uint32_t[]){1});
    ioctl(isp,0x40016920,(uint8_t[]){1});

    /* PkgEnable — this should trigger first ISP interrupt */
    W(0x100,4);W(0x034,5);R[0x100/4]=R[0x100/4]|1;
    ioctl(isp,0x6910,0);
    fprintf(out,"PKG+KICK\n");fflush(out);
    if(wd>=0)write(wd,"\0",1);

    /* Wait for ISP to process */
    usleep(500000);
    if(wd>=0)write(wd,"\0",1);

    /* Check IRQs and frame counters */
    fprintf(out,"AFTER PKG:\n");
    dump_irq(out);
    fprintf(out,"030=%08x 034=%08x 100=%08x 0F0=%08x\n",RD(0x030),RD(0x034),RD(0x100),RD(0x0F0));
    fprintf(out,"13C=%08x 140=%08x 150=%08x 154=%08x\n",RD(0x13C),RD(0x140),RD(0x150),RD(0x154));
    fflush(out);

    /* GET_FRAME */
    uint32_t fr[26];memset(fr,0,sizeof(fr));fr[0]=0;fr[22]=2000;
    int ret=ioctl(isp,0xC0686970,fr);
    fprintf(out,"FRAME=%08x\n",(uint32_t)ret);
    if(ret==0){
        fprintf(out,"*** GOT FRAME! ***\n");
        for(int i=0;i<8;i++)fprintf(out,"[%d]=%08x\n",i,fr[i]);
    }
    fflush(out);
    if(wd>=0)write(wd,"\0",1);

    /* Final IRQ check */
    fprintf(out,"FINAL IRQ:\n");
    dump_irq(out);

    munmap((void*)R,0x4000);close(mem);
    fprintf(out,"END\n");fflush(out);fclose(out);
    system("cp /tmp/etc/config/result.txt /tmp/r.txt 2>/dev/null");
    system("busybox tftp -l /tmp/r.txt -r result.txt -p 192.168.8.100 2>/dev/null");
    if(wd>=0){write(wd,"V",1);close(wd);}
    return 0;
}
