/**
 * Direct ISP register init — writes to mmap'd hardware registers
 * to start the ISP frame processing pipeline.
 *
 * Based on decompiled SetInputPicSize, SetIspPicSize, isp_core_system_reset,
 * isp_startup_Ispp_Init from the vendor binary.
 */
#include <stdio.h>
#include <string.h>
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
#define ISP_IOCTL_INIT     0x690A
#define ISP_IOCTL_KICK     0x6910
#define VMM_ALLOC          0xC0506D0A

#define ISP_REG_BASE       0xE8400000
#define ISP_REG_SIZE       0x3240
#define WIDTH              1920
#define HEIGHT             1080

static int vmm_alloc(int vfd, uint32_t size, const char *name, uint32_t *phys) {
    static uint8_t p[80];
    memset(p,0,80);
    *(uint32_t*)(p+8)=8; *(uint32_t*)(p+12)=size;
    strncpy((char*)(p+0x1C),name,15); strncpy((char*)(p+0x2C),"anonymous",39);
    int r=ioctl(vfd,VMM_ALLOC,p);
    *phys=(r>=0)?*(uint32_t*)p:0;
    return r;
}

static void write_reg(volatile uint32_t *base, uint32_t offset, uint32_t value) {
    base[offset / 4] = value;
}

static uint32_t read_reg(volatile uint32_t *base, uint32_t offset) {
    return base[offset / 4];
}

/**
 * Configure ISP picture size registers.
 * From decompiled SetInputPicSize + SetIspPicSize.
 */
static void isp_set_pic_size(volatile uint32_t *regs, uint32_t w, uint32_t h)
{
    uint32_t input_size = (w - 1) | ((h - 1) << 16);
    uint32_t stride_raw = ((w * 10 + 7) >> 3) + 7;
    uint32_t stride_q   = (((w >> 2) * 10) >> 3) + 7;
    uint32_t blk_w = (w >> 5) - 1;
    uint32_t blk_h = (h >> 5) - 1;

    /* Input picture size */
    write_reg(regs, 0x130, input_size);
    write_reg(regs, 0x144, input_size);

    /* ISP picture size */
    write_reg(regs, 0x138, input_size);
    write_reg(regs, 0x14C, input_size);
    write_reg(regs, 0x0D0, input_size);

    /* Block size for AE/AWB statistics */
    write_reg(regs, 0x490, blk_h * 0x1000000 | ((w / 0x30 - 1) * 0x10000));

    /* Buffer strides */
    write_reg(regs, 0x2E8, stride_raw & 0xFFFFFFF8);
    write_reg(regs, 0x2F0, (stride_raw & 0x7FFFFFF8) | 0x80000000);
    write_reg(regs, 0x2F8, 0x280);
    write_reg(regs, 0x300, 0x80000280);
    write_reg(regs, 0x308, stride_q & 0xFFFFFFF8);
    write_reg(regs, 0x310, (stride_q & 0x7FFFFFF8) | 0x80000000);
    write_reg(regs, 0x318, stride_q & 0xFFFFFFF8);
    write_reg(regs, 0x320, (stride_q & 0x7FFFFFF8) | 0x80000000);
    write_reg(regs, 0x41C, stride_q & 0xFFF8);
    write_reg(regs, 0xAA8, stride_q & 0xFFF8);
    write_reg(regs, 0x648, 0x80000348);
    write_reg(regs, 0x658, 0x80000348);

    /* NR/dehaze window config */
    write_reg(regs, 0x500, 0);
    write_reg(regs, 0x504, 0);
    write_reg(regs, 0x508, blk_w | (blk_h << 16) | 0x78007800);

    /* AE config */
    uint32_t ae_cfg = read_reg(regs, 0x4E0);
    write_reg(regs, 0x4E0, (ae_cfg & 0xE0000000) | (blk_w | (blk_h << 8)) | 0x0F0F0000);
    write_reg(regs, 0x4E4, 0);
    write_reg(regs, 0x4E8, 0x1FFF1);
}

/**
 * Initialize ISP processing pipeline registers.
 * From decompiled isp_startup_Ispp_Init.
 */
static void isp_ispp_init(volatile uint32_t *regs)
{
    /* Enable all ISP modules */
    write_reg(regs, 0x008, 0x07FFFFFF);

    /* Tone mapping curve — linear ramp */
    write_reg(regs, 0x080, 0x50402010);
    write_reg(regs, 0x084, 0x90807060);
    write_reg(regs, 0x088, 0xD0C0B0A0);
    write_reg(regs, 0x08C, 0x00FFF0E0);

    /* Gamma pre-curve */
    write_reg(regs, 0x090, 0x10101010);
    write_reg(regs, 0x094, 0x10101010);
    write_reg(regs, 0x098, 0x10101010);
    write_reg(regs, 0x09C, 0x000F1010);

    /* Second tone mapping curve */
    write_reg(regs, 0x0A0, 0x30281810);
    write_reg(regs, 0x0A4, 0x50484038);
    write_reg(regs, 0x0A8, 0x80706058);
    write_reg(regs, 0x0AC, 0xC8C0A090);
    write_reg(regs, 0x0B0, 0x00E0D8D0);
    write_reg(regs, 0x0B4, 0);
    write_reg(regs, 0x0B8, 0);

    /* ISP processing config */
    write_reg(regs, 0x494, 0x9015);

    /* Clear statistic accumulators */
    write_reg(regs, 0x13C, 0);
    write_reg(regs, 0x140, 0);
    write_reg(regs, 0x150, 0);
    write_reg(regs, 0x154, 0);
    write_reg(regs, 0x578, 0);
    write_reg(regs, 0x57C, 0);
}

/**
 * ISP soft reset — from decompiled isp_core_system_reset.
 */
static void isp_soft_reset(volatile uint32_t *regs)
{
    uint32_t v;
    v = read_reg(regs, 0x0F0);
    write_reg(regs, 0x0F0, (v & 0xFFFFFFCF) | 0x10);  /* set reset bit */

    v = read_reg(regs, 0x0F4);
    write_reg(regs, 0x0F4, (v & 0xFFFFFFFC) | 0x02);   /* set reset bit 2 */
}

/**
 * Set Bayer pattern and mirror/flip in ISP.
 * From decompiled isp_startup_IspInPicCfg.
 */
static void isp_set_bayer_mode(volatile uint32_t *regs, uint32_t bayer_order)
{
    uint32_t v = read_reg(regs, 0x028);
    v = (v & 0xFFFFFFE1) | ((bayer_order & 3) << 1) | ((bayer_order & 3) << 3);
    write_reg(regs, 0x028, v);
}

int main(void)
{
    int ret;
    printf("=== ISP REGISTER INIT TEST ===\n\n"); fflush(stdout);

    /* 1. /proc config */
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
    printf("[1] Config\n"); fflush(stdout);

    /* 2. Open devices */
    int mp=open("/dev/media_process",O_RDWR);
    int isp=open("/dev/isp",O_RDWR);
    open("/dev/enc",O_RDWR);
    open("/dev/jpeg",O_RDWR);
    int vmm=open("/dev/vmm_userdev",O_RDWR);
    printf("[2] mp=%d isp=%d vmm=%d\n",mp,isp,vmm); fflush(stdout);

    /* 3. VPSS init */
    printf("[3] VPSS\n"); fflush(stdout);
    uint32_t sz=0, phys=0;
    ioctl(isp,VPSS_SYS_QUERY,&sz);
    vmm_alloc(vmm,sz,"vpu-sys",&phys);
    uint32_t sm[3]={phys,0,sz}; ioctl(isp,VPSS_SYS_SET_MEM,sm);
    uint32_t q[4]={0,1920,1080,0}; ioctl(isp,VPSS_CHN_QUERY,q);
    vmm_alloc(vmm,q[3],"vpu-ch-0",&phys);
    uint32_t cs[6]={0,phys,0,q[3],1920,1080}; ioctl(isp,VPSS_CHN_SET_MEM,cs);
    uint32_t vi[62]; memset(vi,0,sizeof(vi)); vi[0]=1920;vi[1]=1080;
    ioctl(isp,VPSS_SET_VI_ATTR,vi);
    uint32_t en=0; ioctl(isp,VPSS_ENABLE,&en);
    uint32_t ca[3]={0,1920,1080}; ioctl(isp,VPSS_SET_CHN_ATTR,ca);
    uint32_t ch=0; ioctl(isp,VPSS_OPEN_CHN,&ch);
    uint32_t vo[2]={0,1}; ioctl(isp,VPSS_SET_VO_MODE,vo);
    printf("    OK\n"); fflush(stdout);

    /* 4. Mmap ISP registers */
    printf("[4] ISP mmap\n"); fflush(stdout);
    int mem_fd = open("/dev/mem", O_RDWR | O_SYNC);
    volatile uint32_t *isp_regs = NULL;
    if (mem_fd >= 0) {
        isp_regs = mmap(NULL, ISP_REG_SIZE, PROT_READ|PROT_WRITE,
                        MAP_SHARED, mem_fd, ISP_REG_BASE);
        if (isp_regs == MAP_FAILED) { isp_regs = NULL; }
    }
    printf("    regs=%p\n", (void*)isp_regs); fflush(stdout);

    if (!isp_regs) { printf("    FAIL\n"); goto done; }

    /* Read ISP chip ID */
    printf("    ID=0x%08x\n", read_reg(isp_regs, 0x000)); fflush(stdout);

    /* 5. ISP ioctl init — full sequence */
    printf("[5] ISP ioctls\n"); fflush(stdout);

    /* Get chip info — this may trigger MIPI init in kernel */
    uint32_t chip_info[2] = {0};
    ret = ioctl(isp, 0x80086929, chip_info);
    printf("    CHIP_INFO(0x80086929): ret=%d info=[0x%08x, 0x%08x]\n",
           ret, chip_info[0], chip_info[1]); fflush(stdout);

    /* Get HW module config */
    uint32_t hw_cfg[12] = {0};
    ret = ioctl(isp, 0x80306926, hw_cfg);
    printf("    HW_CFG(0x80306926): ret=%d\n", ret); fflush(stdout);

    /* ISP init ioctl */
    uint32_t ip[4]={0};
    ret=ioctl(isp,ISP_IOCTL_INIT,ip);
    printf("    INIT(0x690A)=%d\n",ret); fflush(stdout);

    /* 6. ISP register init — write hardware registers directly */
    printf("[6] ISP hw regs\n"); fflush(stdout);

    /* Soft reset ISP */
    isp_soft_reset(isp_regs);
    usleep(1000);
    printf("    reset done\n"); fflush(stdout);

    /* Configure picture sizes */
    isp_set_pic_size(isp_regs, WIDTH, HEIGHT);
    printf("    pic size done\n"); fflush(stdout);

    /* Init ISP processing pipeline */
    isp_ispp_init(isp_regs);
    printf("    ispp init done\n"); fflush(stdout);

    /* Set Bayer order (0 = RGGB, from sensor) */
    isp_set_bayer_mode(isp_regs, 0);
    printf("    bayer done\n"); fflush(stdout);

    /* ISP filter init (from isp_startup_Ispf_Init) */
    printf("    ispf init...\n"); fflush(stdout);
    write_reg(isp_regs, 0x0E0, 0x400102);
    uint32_t e4 = read_reg(isp_regs, 0x0E4);
    write_reg(isp_regs, 0x0E4, e4 | 2);
    write_reg(isp_regs, 0x400, 1);
    write_reg(isp_regs, 0x404, 0xF001F4A0);
    write_reg(isp_regs, 0x408, 0xF001F4A0);
    write_reg(isp_regs, 0x40C, 0xF001F4A0);
    write_reg(isp_regs, 0x410, 0xF001F4A0);
    write_reg(isp_regs, 0x414, 0x1FFF009C);
    write_reg(isp_regs, 0x1C4, 0x0C);
    /* AWB gains — identity (1x) */
    write_reg(isp_regs, 0x250, 0xFF3983);
    write_reg(isp_regs, 0x254, 0x1000100);
    write_reg(isp_regs, 0x258, 0x1000100);
    /* Gamma — identity */
    write_reg(isp_regs, 0x590, 0x0D);
    write_reg(isp_regs, 0x594, 0x30303);
    /* CCM — identity matrix */
    write_reg(isp_regs, 0xA30, 0x200);
    write_reg(isp_regs, 0xA34, 0);
    write_reg(isp_regs, 0xA38, 0x200);
    write_reg(isp_regs, 0xA3C, 0);
    write_reg(isp_regs, 0xA40, 0x200);
    write_reg(isp_regs, 0xA44, 0);
    /* Bayer order in ISP input config */
    write_reg(isp_regs, 0x028, (read_reg(isp_regs, 0x028) & 0xFFFFFFE1));
    write_reg(isp_regs, 0x02C, 0x80);

    /* ISP START — the critical PkgEnable registers! */
    printf("    PkgEnable (START)...\n"); fflush(stdout);
    write_reg(isp_regs, 0x100, 4);      /* set mode */
    write_reg(isp_regs, 0x034, 5);      /* enable processing */
    write_reg(isp_regs, 0x100, read_reg(isp_regs, 0x100) | 1);  /* GO! */
    printf("    ISP STARTED!\n"); fflush(stdout);

    /* Kick ISP via ioctl too */
    ret=ioctl(isp,ISP_IOCTL_KICK,0);
    printf("    KICK=%d\n",ret); fflush(stdout);

    /* 7. Sensor init */
    printf("[7] Sensor\n"); fflush(stdout);
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
        printf("    %d/52 streaming\n",ok); fflush(stdout);
        close(i2c);
    }

    /* 8. Get frame */
    printf("[8] Frame\n"); fflush(stdout);
    for(int a=0;a<15;a++){
        usleep(200000);
        uint32_t f[26]; memset(f,0,sizeof(f)); f[0]=0;
        ret=ioctl(isp,VPSS_GET_CHN_FRAME,f);
        printf("    %02d: 0x%08x",a,(uint32_t)ret); fflush(stdout);
        if(ret==0){
            printf(" *** FRAME! ***\n");
            for(int i=0;i<12;i++)
                printf("      [%2d] 0x%08x (%u)\n",i,f[i],f[i]);
            break;
        }
        printf("\n"); fflush(stdout);
    }

    /* 9. ISP status */
    printf("\n[9]\n"); fflush(stdout);
    system("cat /proc/driver/isp 2>/dev/null | grep -E 'FPS|Resol|PIC_START Count|OVERFLOW'");
    printf("---\n");
    /* Read ISP interrupt counters directly from registers */
    printf("    ISP regs after:\n");
    printf("      [0x000]=0x%08x [0x008]=0x%08x\n",
           read_reg(isp_regs,0), read_reg(isp_regs,8));
    printf("      [0x0F0]=0x%08x [0x0F4]=0x%08x\n",
           read_reg(isp_regs,0xF0), read_reg(isp_regs,0xF4));
    printf("      [0x130]=0x%08x [0x138]=0x%08x\n",
           read_reg(isp_regs,0x130), read_reg(isp_regs,0x138));

done:
    printf("\n=== DONE ===\n");
    return 0;
}
