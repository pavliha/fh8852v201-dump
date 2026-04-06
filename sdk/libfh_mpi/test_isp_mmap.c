/**
 * ISP hardware register mmap test.
 * Maps 0xE8400000 (ISP registers) via /dev/isp and reads values.
 * Then tries to start the ISP by writing to key registers.
 */
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
#define I2C_M_RD 0x0001
struct i2c_msg { uint16_t addr; uint16_t flags; uint16_t len; uint8_t *buf; };
struct i2c_rdwr_ioctl_data { struct i2c_msg *msgs; uint32_t nmsgs; };

/* ISP register base from /proc/iomem — NOT listed there because
 * the ISP is behind the media_process kernel module.
 * From decompilation: isp_mmap(0xE8400000, ..., 0x3240)
 * Size: 0x3240 = 12864 bytes */
#define ISP_REG_BASE    0xE8400000
#define ISP_REG_SIZE    0x3240

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
#define ISP_IOCTL_HW_CFG   0x80306926
#define VMM_ALLOC          0xC0506D0A

static int vmm_alloc(int vfd, uint32_t size, const char *name, uint32_t *phys) {
    static uint8_t p[80];
    memset(p,0,80);
    *(uint32_t*)(p+8)=8; *(uint32_t*)(p+12)=size;
    strncpy((char*)(p+0x1C),name,15); strncpy((char*)(p+0x2C),"anonymous",39);
    int r=ioctl(vfd,VMM_ALLOC,p);
    *phys=(r>=0)?*(uint32_t*)p:0;
    return r;
}

int main(void)
{
    int ret;
    printf("=== ISP MMAP + FULL INIT TEST ===\n\n"); fflush(stdout);

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
    printf("[1] Config OK\n"); fflush(stdout);

    /* 2. Open devices — ORDER MATTERS */
    int mp  = open("/dev/media_process", O_RDWR);
    int isp = open("/dev/isp", O_RDWR);
    open("/dev/enc", O_RDWR);
    open("/dev/jpeg", O_RDWR);
    int vmm = open("/dev/vmm_userdev", O_RDWR);
    printf("[2] mp=%d isp=%d vmm=%d\n", mp, isp, vmm); fflush(stdout);

    /* 3. VPSS SysInit + CH0 */
    printf("[3] VPSS init\n"); fflush(stdout);
    uint32_t sz=0, phys=0;
    ioctl(isp, VPSS_SYS_QUERY, &sz);
    vmm_alloc(vmm, sz, "vpu-sys", &phys);
    uint32_t sm[3]={phys,0,sz};
    ioctl(isp, VPSS_SYS_SET_MEM, sm);

    uint32_t q[4]={0,1920,1080,0};
    ioctl(isp, VPSS_CHN_QUERY, q);
    vmm_alloc(vmm, q[3], "vpu-ch-0", &phys);
    uint32_t cs[6]={0,phys,0,q[3],1920,1080};
    ioctl(isp, VPSS_CHN_SET_MEM, cs);

    uint32_t vi[62]; memset(vi,0,sizeof(vi)); vi[0]=1920; vi[1]=1080;
    ioctl(isp, VPSS_SET_VI_ATTR, vi);
    uint32_t en=0; ioctl(isp, VPSS_ENABLE, &en);
    uint32_t ca[3]={0,1920,1080}; ioctl(isp, VPSS_SET_CHN_ATTR, ca);
    uint32_t ch=0; ioctl(isp, VPSS_OPEN_CHN, &ch);
    uint32_t vo[2]={0,1}; ioctl(isp, VPSS_SET_VO_MODE, vo);
    printf("    VPSS pipeline OK\n"); fflush(stdout);

    /* 4. Allocate ISP working memory */
    printf("[4] ISP memory\n"); fflush(stdout);
    
    /* Query ISP HW config */
    uint32_t hw_cfg[12];
    memset(hw_cfg, 0, sizeof(hw_cfg));
    ret = ioctl(isp, ISP_IOCTL_HW_CFG, hw_cfg);
    printf("    HW_CFG(0x80306926): ret=%d\n", ret); fflush(stdout);
    if (ret == 0) {
        for (int i = 0; i < 12; i++)
            printf("      [%d] = 0x%08x (%u)\n", i, hw_cfg[i], hw_cfg[i]);
        fflush(stdout);
    }

    /* Allocate ISP memory via VMM */
    /* From decompilation: isp_core_get_buf_size returns size at param_3[0x15] */
    /* For 1920x1080: approximately 3MB */
    uint32_t isp_mem_size = 3 * 1024 * 1024; /* 3MB estimate */
    uint32_t isp_phys = 0;
    vmm_alloc(vmm, isp_mem_size, "isp", &isp_phys);
    printf("    ISP mem: phys=0x%08x size=%u\n", isp_phys, isp_mem_size);
    fflush(stdout);

    /* 5. Try mmap ISP hardware registers */
    printf("[5] ISP register mmap\n"); fflush(stdout);

    /* Try mmap on /dev/isp fd with ISP register base */
    volatile uint32_t *isp_regs = mmap(NULL, ISP_REG_SIZE,
                                        PROT_READ | PROT_WRITE,
                                        MAP_SHARED, isp,
                                        ISP_REG_BASE);
    if (isp_regs == MAP_FAILED) {
        printf("    mmap /dev/isp at 0x%08x: FAIL errno=%d\n",
               ISP_REG_BASE, errno); fflush(stdout);

        /* Try /dev/mem instead */
        int mem_fd = open("/dev/mem", O_RDWR | O_SYNC);
        if (mem_fd >= 0) {
            isp_regs = mmap(NULL, ISP_REG_SIZE,
                           PROT_READ | PROT_WRITE,
                           MAP_SHARED, mem_fd,
                           ISP_REG_BASE);
            if (isp_regs != MAP_FAILED) {
                printf("    mmap /dev/mem at 0x%08x: OK!\n", ISP_REG_BASE);
            } else {
                printf("    mmap /dev/mem: FAIL errno=%d\n", errno);
                isp_regs = NULL;
            }
        }
    } else {
        printf("    mmap /dev/isp at 0x%08x: OK!\n", ISP_REG_BASE);
    }
    fflush(stdout);

    if (isp_regs && isp_regs != MAP_FAILED) {
        /* Read first 16 ISP registers */
        printf("    ISP registers:\n");
        for (int i = 0; i < 16; i++) {
            printf("      [0x%03x] = 0x%08x\n", i * 4, isp_regs[i]);
        }
        fflush(stdout);
    }

    /* 6. ISP init ioctl */
    printf("[6] ISP init\n"); fflush(stdout);
    uint32_t ip[4] = {0};
    ret = ioctl(isp, ISP_IOCTL_INIT, ip);
    printf("    INIT: %d\n", ret); fflush(stdout);
    ret = ioctl(isp, ISP_IOCTL_KICK, 0);
    printf("    KICK: %d\n", ret); fflush(stdout);

    /* 7. Sensor init via I2C */
    printf("[7] Sensor\n"); fflush(stdout);
    int i2c = open("/dev/i2c-0", O_RDWR);
    if (i2c >= 0) {
        static const uint16_t r[][2] = {
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
        uint8_t wb[3]; int ok = 0;
        struct i2c_msg m = {.addr=0x35,.flags=0,.len=3,.buf=wb};
        struct i2c_rdwr_ioctl_data x = {.msgs=&m,.nmsgs=1};
        for (int i = 0; i < 52; i++) {
            wb[0]=r[i][0]>>8; wb[1]=r[i][0]&0xFF; wb[2]=r[i][1];
            if (ioctl(i2c, I2C_RDWR, &x) >= 0) ok++;
        }
        usleep(10000);
        wb[0]=0x31; wb[1]=0x41; wb[2]=0x01;
        ioctl(i2c, I2C_RDWR, &x);
        printf("    %d/52, streaming\n", ok); fflush(stdout);
        close(i2c);
    }

    /* 8. Try GET_FRAME with longer wait */
    printf("[8] Frame attempts\n"); fflush(stdout);
    for (int attempt = 0; attempt < 10; attempt++) {
        usleep(200000);
        uint32_t f[26]; memset(f,0,sizeof(f)); f[0]=0;
        ret = ioctl(isp, VPSS_GET_CHN_FRAME, f);
        printf("    %d: 0x%08x", attempt, (uint32_t)ret); fflush(stdout);
        if (ret == 0) {
            printf(" *** FRAME! ***\n");
            for (int i = 0; i < 12; i++)
                printf("      [%2d] 0x%08x (%u)\n", i, f[i], f[i]);
            break;
        }
        printf("\n"); fflush(stdout);
    }

    /* 9. ISP status */
    printf("\n[9] Status\n"); fflush(stdout);
    system("cat /proc/driver/isp 2>/dev/null | grep -E 'FPS|Resol|PIC_START Count|OVERFLOW'");
    system("cat /proc/driver/vmm 2>/dev/null | grep total");
    system("cat /proc/driver/media_process 2>/dev/null | grep CHN");

    /* Read ISP regs again after init */
    if (isp_regs && isp_regs != MAP_FAILED) {
        printf("\n    ISP regs after init:\n");
        for (int i = 0; i < 8; i++)
            printf("      [0x%03x] = 0x%08x\n", i * 4, isp_regs[i]);
    }

    printf("\n=== DONE ===\n");
    return 0;
}
