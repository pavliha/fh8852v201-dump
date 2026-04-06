/**
 * ISP core struct allocation + sensor vtable install.
 * This is the final missing piece — allocate the ISP's internal
 * 0x1C60-byte struct, install our sensor vtable at +0x162C,
 * mmap ISP regs into +0x1378, and call isp_core_init.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <stdint.h>
#include <errno.h>

#define I2C_RDWR 0x0707
struct i2c_msg { uint16_t addr; uint16_t flags; uint16_t len; uint8_t *buf; };
struct i2c_rdwr_ioctl_data { struct i2c_msg *msgs; uint32_t nmsgs; };

#define VPSS_SET_VI_ATTR   0xC0F4696A
#define VPSS_ENABLE        0xC0046971
#define VPSS_SET_CHN_ATTR  0xC00C696C
#define VPSS_OPEN_CHN      0xC0046973
#define VPSS_SET_VO_MODE   0xC00869A4
#define VPSS_GET_CHN_FRAME 0xC0686970
#define VPSS_SYS_QUERY     0xC0046966
#define VPSS_SYS_SET_MEM   0xC00C6964
#define VPSS_CHN_QUERY     0xC0106967
#define VPSS_CHN_SET_MEM   0xC0186968
#define ISP_INIT           0x690A
#define ISP_KICK           0x6910
#define VMM_ALLOC          0xC0506D0A

#define ISP_REG_BASE       0xE8400000
#define ISP_REG_SIZE       0x3240
#define ISP_STRUCT_SIZE    0x1C60
#define SENSOR_VT_OFF      0x162C
#define ISP_MMAP_OFF       0x1378
#define ISP_FD_OFF         0x16BC
#define ISP_MODE_OFF       0x1580

static int vmm_alloc(int vfd, uint32_t sz, const char *nm, uint32_t *ph) {
    static uint8_t p[80]; memset(p,0,80);
    *(uint32_t*)(p+8)=8; *(uint32_t*)(p+12)=sz;
    strncpy((char*)(p+0x1C),nm,15); strncpy((char*)(p+0x2C),"anonymous",39);
    int r=ioctl(vfd,VMM_ALLOC,p);
    *ph=(r>=0)?*(uint32_t*)p:0; return r;
}

/* Minimal CV2003 sensor vtable — just enough for ISP to accept it */
static const char sns_name[] = "cv2003_mipi";
static int sns_get_info(uint16_t *info) {
    if(!info)return-1;
    info[0]=1920;info[1]=1080;info[2]=1920;info[3]=1080;
    info[4]=0;info[5]=0;info[6]=1920;info[7]=1080;
    *(uint32_t*)(info+10)=74250000;
    return 0;
}
static int sns_set_fmt(int fmt) { return 0; }
static int sns_kick(void) { return 0; }
static int sns_stub(void) { return 0; }

int main(void)
{
    int ret;
    printf("=== ISP STRUCT + VTABLE TEST ===\n\n"); fflush(stdout);

    /* Config */
    const char *cfg[][2]={
        {"/proc/driver/vpu","vi_1920_1080"},{"/proc/driver/vpu","cap_0_1920_1080"},
        {"/proc/driver/vpu","cap_1_800_576"},{"/proc/driver/vpu","cap_2_512_288"},
        {"/proc/driver/vpu","buf_0_2"},{"/proc/driver/vpu","buf_1_3"},
        {"/proc/driver/vpu","buf_2_1"},{"/proc/driver/vpu","cirbuf_on"},
        {"/proc/driver/vpu","cbufsize_80"},
        {"/proc/driver/isp","wdr_off"},{"/proc/driver/isp","cir_on"},
        {"/proc/driver/enc","stm_1572864"},
        {"/proc/driver/jpeg","mem_1_131072"},{"/proc/driver/jpeg","mjpg_0_0"},
        {"/proc/driver/clock","nn_clk,enable,90000000"},{NULL,NULL}
    };
    for(int i=0;cfg[i][0];i++){
        int fd=open(cfg[i][0],O_WRONLY);
        if(fd>=0){write(fd,cfg[i][1],strlen(cfg[i][1]));close(fd);}
    }

    /* Open devices */
    int mp=open("/dev/media_process",O_RDWR);
    int isp_fd=open("/dev/isp",O_RDWR);
    open("/dev/enc",O_RDWR);
    open("/dev/jpeg",O_RDWR);
    int vmm=open("/dev/vmm_userdev",O_RDWR);
    printf("[1] mp=%d isp=%d vmm=%d\n",mp,isp_fd,vmm); fflush(stdout);

    /* VPSS init */
    uint32_t sz=0,phys=0;
    ioctl(isp_fd,VPSS_SYS_QUERY,&sz);
    vmm_alloc(vmm,sz,"vpu-sys",&phys);
    uint32_t sm[3]={phys,0,sz}; ioctl(isp_fd,VPSS_SYS_SET_MEM,sm);
    uint32_t q[4]={0,1920,1080,0}; ioctl(isp_fd,VPSS_CHN_QUERY,q);
    vmm_alloc(vmm,q[3],"vpu-ch-0",&phys);
    uint32_t cs[6]={0,phys,0,q[3],1920,1080}; ioctl(isp_fd,VPSS_CHN_SET_MEM,cs);
    uint32_t vi[62]; memset(vi,0,sizeof(vi)); vi[0]=1920;vi[1]=1080;
    ioctl(isp_fd,VPSS_SET_VI_ATTR,vi);
    uint32_t en=0; ioctl(isp_fd,VPSS_ENABLE,&en);
    uint32_t ca[3]={0,1920,1080}; ioctl(isp_fd,VPSS_SET_CHN_ATTR,ca);
    uint32_t ch=0; ioctl(isp_fd,VPSS_OPEN_CHN,&ch);
    uint32_t vo[2]={0,1}; ioctl(isp_fd,VPSS_SET_VO_MODE,vo);
    printf("[2] VPSS OK\n"); fflush(stdout);

    /* Allocate ISP core struct */
    printf("[3] ISP struct\n"); fflush(stdout);
    uint8_t *isp_struct = calloc(1, ISP_STRUCT_SIZE);
    if (!isp_struct) { printf("    alloc FAIL\n"); return 1; }
    printf("    alloc: %p (%d bytes)\n", isp_struct, ISP_STRUCT_SIZE); fflush(stdout);

    /* Store ISP fd in struct */
    *(int*)(isp_struct + ISP_FD_OFF) = isp_fd;

    /* Set ISP mode */
    *(uint32_t*)(isp_struct + ISP_MODE_OFF) = 3;
    *(uint32_t*)(isp_struct + 0x15A8) = 3;
    *(uint32_t*)(isp_struct + 0x15D0) = 3;

    /* Set frame max size */
    *(uint32_t*)(isp_struct + 0x1C38) = 1920;
    *(uint32_t*)(isp_struct + 0x1C3C) = 1080;

    /* Set sensor resolution in struct (from vtable get_info) */
    *(uint16_t*)(isp_struct + 0x14) = 1920;
    *(uint16_t*)(isp_struct + 0x16) = 1080;
    *(uint16_t*)(isp_struct + 0x20) = 1920;
    *(uint16_t*)(isp_struct + 0x22) = 1080;
    *(uint16_t*)(isp_struct + 0x1C) = 0;
    *(uint16_t*)(isp_struct + 0x1E) = 0;

    /* Bayer format */
    *(uint8_t*)(isp_struct + 0x0D) = 0; /* RGGB */

    /* Install sensor vtable at +0x162C */
    printf("[4] Sensor vtable\n"); fflush(stdout);
    uint32_t *vt = (uint32_t*)(isp_struct + SENSOR_VT_OFF);
    memset(vt, 0, 124);
    vt[0]  = (uint32_t)(uintptr_t)sns_name;
    vt[1]  = (uint32_t)(uintptr_t)sns_get_info;
    vt[8]  = (uint32_t)(uintptr_t)sns_set_fmt;
    vt[9]  = (uint32_t)(uintptr_t)sns_kick;
    vt[30] = (uint32_t)(uintptr_t)sns_stub;
    printf("    installed at struct+0x%x\n", SENSOR_VT_OFF); fflush(stdout);

    /* Mmap ISP hardware registers into struct+0x1378 */
    printf("[5] ISP mmap\n"); fflush(stdout);
    int mem_fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (mem_fd < 0) { printf("    /dev/mem FAIL\n"); return 1; }
    volatile uint32_t *isp_regs = mmap(NULL, ISP_REG_SIZE,
                                        PROT_READ|PROT_WRITE,
                                        MAP_SHARED, mem_fd, ISP_REG_BASE);
    if (isp_regs == MAP_FAILED) { printf("    mmap FAIL\n"); return 1; }
    *(uint32_t*)(isp_struct + ISP_MMAP_OFF) = (uint32_t)(uintptr_t)isp_regs;
    printf("    regs=%p stored at struct+0x%x\n", isp_regs, ISP_MMAP_OFF);
    printf("    ID=0x%08x\n", isp_regs[0]); fflush(stdout);

    /* Chip info ioctl */
    ioctl(isp_fd, 0x80086929, isp_struct + 0x13EC);
    printf("    chip_info at +0x13EC\n"); fflush(stdout);

    /* ISP init ioctl */
    printf("[6] ISP init\n"); fflush(stdout);
    uint32_t ip[4]={0};
    ret = ioctl(isp_fd, ISP_INIT, ip);
    printf("    INIT=%d\n", ret); fflush(stdout);

    /* ISP hardware register init (from decompilation) */
    printf("[7] ISP hw init\n"); fflush(stdout);

    /* Reset */
    isp_regs[0xF0/4] = (isp_regs[0xF0/4] & 0xFFFFFFCF) | 0x10;
    isp_regs[0xF4/4] = (isp_regs[0xF4/4] & 0xFFFFFFFC) | 0x02;
    usleep(1000);

    /* Enable all modules */
    isp_regs[0x08/4] = 0x07FFFFFF;

    /* Picture size */
    uint32_t pic = (1920-1) | ((1080-1) << 16);
    isp_regs[0x130/4] = pic;
    isp_regs[0x144/4] = pic;
    isp_regs[0x138/4] = pic;
    isp_regs[0x14C/4] = pic;
    isp_regs[0x0D0/4] = pic;

    /* Tone mapping */
    isp_regs[0x080/4]=0x50402010; isp_regs[0x084/4]=0x90807060;
    isp_regs[0x088/4]=0xD0C0B0A0; isp_regs[0x08C/4]=0x00FFF0E0;
    isp_regs[0x090/4]=0x10101010; isp_regs[0x094/4]=0x10101010;
    isp_regs[0x098/4]=0x10101010; isp_regs[0x09C/4]=0x000F1010;

    /* ISP filter init — minimal */
    isp_regs[0x0E0/4] = 0x400102;
    isp_regs[0x0E4/4] = isp_regs[0x0E4/4] | 2;
    isp_regs[0x400/4] = 1;
    isp_regs[0x1C4/4] = 0x0C;
    isp_regs[0x250/4] = 0xFF3983;
    isp_regs[0x254/4] = 0x1000100;
    isp_regs[0x258/4] = 0x1000100;
    /* CCM identity */
    isp_regs[0xA30/4]=0x200; isp_regs[0xA34/4]=0;
    isp_regs[0xA38/4]=0x200; isp_regs[0xA3C/4]=0;
    isp_regs[0xA40/4]=0x200; isp_regs[0xA44/4]=0;

    /* Buffer strides */
    uint32_t stride = (((1920*10+7)>>3)+7) & 0xFFFFFFF8;
    isp_regs[0x2E8/4] = stride;
    isp_regs[0x2F0/4] = stride | 0x80000000;

    /* Bayer pattern */
    isp_regs[0x028/4] = isp_regs[0x028/4] & 0xFFFFFFE1;

    /* PkgEnable — START ISP! */
    printf("    STARTING ISP...\n"); fflush(stdout);
    isp_regs[0x100/4] = 4;
    isp_regs[0x034/4] = 5;
    isp_regs[0x100/4] = isp_regs[0x100/4] | 1;
    printf("    ISP RUNNING\n"); fflush(stdout);

    /* Kick */
    ioctl(isp_fd, ISP_KICK, 0);

    /* Sensor init */
    printf("[8] Sensor\n"); fflush(stdout);
    int i2c=open("/dev/i2c-0",O_RDWR);
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
            {0x3037,0x04},{0x3908,0x51},{0x390a,0x02},{0x3000,0x00},
        };
        uint8_t wb[3]; int ok=0;
        struct i2c_msg m={.addr=0x35,.flags=0,.len=3,.buf=wb};
        struct i2c_rdwr_ioctl_data x={.msgs=&m,.nmsgs=1};
        for(int i=0;i<52;i++){
            wb[0]=r[i][0]>>8;wb[1]=r[i][0]&0xFF;wb[2]=r[i][1];
            if(ioctl(i2c,I2C_RDWR,&x)>=0)ok++;
        }
        usleep(10000);
        wb[0]=0x31;wb[1]=0x41;wb[2]=0x01;
        ioctl(i2c,I2C_RDWR,&x);
        printf("    %d/52\n",ok); fflush(stdout);
        close(i2c);
    }

    /* Get frame */
    printf("[9] Frame\n"); fflush(stdout);
    for(int a=0;a<15;a++){
        usleep(200000);
        uint32_t f[26]; memset(f,0,sizeof(f)); f[0]=0;
        ret=ioctl(isp_fd,VPSS_GET_CHN_FRAME,f);
        printf("    %02d: 0x%08x",a,(uint32_t)ret); fflush(stdout);
        if(ret==0){
            printf(" *** FRAME! ***\n");
            for(int i=0;i<12;i++)
                printf("      [%2d] 0x%08x (%u)\n",i,f[i],f[i]);
            break;
        }
        printf("\n"); fflush(stdout);
    }

    printf("\n[10]\n"); fflush(stdout);
    system("cat /proc/driver/isp 2>/dev/null | grep -E 'FPS|PIC_START Count'");
    system("cat /proc/driver/mipi 2>/dev/null | grep -E 'sframe|width|lane|status'");

    free(isp_struct);
    printf("\n=== DONE ===\n");
    return 0;
}
