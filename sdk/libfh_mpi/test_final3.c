#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <stdint.h>
#include <errno.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>

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
#define VMM_ALLOC          0xC0506D0A

/* Simple VMM alloc — just get phys addr, no mmap */
static int vmm_alloc(int vfd, uint32_t size, const char *name, uint32_t *phys)
{
    uint8_t p[80];
    memset(p, 0, 80);
    *(uint32_t *)(p + 0x08) = 8;
    *(uint32_t *)(p + 0x0C) = size;
    strncpy((char *)(p + 0x1C), name, 15);
    strncpy((char *)(p + 0x2C), "anonymous", 39);
    int ret = ioctl(vfd, VMM_ALLOC, p);
    *phys = (ret >= 0) ? *(uint32_t *)p : 0;
    return ret;
}

int main(void)
{
    int isp, vmm, ret;
    printf("=== PIPELINE TEST v3 ===\n\n");

    /* Config */
    const char *cfgs[][2] = {
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
    for (int i = 0; cfgs[i][0]; i++) {
        int fd = open(cfgs[i][0], O_WRONLY);
        if (fd >= 0) { write(fd, cfgs[i][1], strlen(cfgs[i][1])); close(fd); }
    }
    printf("[1] Config OK\n");

    /* Open */
    isp = open("/dev/isp", O_RDWR);
    vmm = open("/dev/vmm_userdev", O_RDWR);
    open("/dev/media_process", O_RDWR);
    open("/dev/enc", O_RDWR);
    open("/dev/jpeg", O_RDWR);
    printf("[2] isp=%d vmm=%d\n", isp, vmm);

    /* SysInitMem */
    printf("[3] SysInitMem:\n");
    uint32_t sys_sz = 0, phys = 0;
    ioctl(isp, VPSS_SYS_QUERY, &sys_sz);
    printf("    need=%u\n", sys_sz);
    vmm_alloc(vmm, sys_sz, "vpu-sys", &phys);
    printf("    phys=0x%08x\n", phys);
    uint32_t sm[3] = { phys, 0, sys_sz };  /* virt=0, kernel handles */
    ret = ioctl(isp, VPSS_SYS_SET_MEM, sm);
    printf("    set=0x%08x %s\n", (uint32_t)ret, ret == 0 ? "OK" : "FAIL");

    /* ChnInitMem */
    printf("[4] ChnInitMem:\n");
    uint32_t res[][2] = { {1920, 1080}, {800, 576}, {512, 288} };
    for (int ch = 0; ch < 3; ch++) {
        uint32_t q[4] = { ch, res[ch][0], res[ch][1], 0 };
        ret = ioctl(isp, VPSS_CHN_QUERY, q);
        uint32_t need = q[3];
        printf("    CH%d: need=%u ", ch, need);

        if (ret != 0 || need == 0) { printf("query fail\n"); continue; }

        uint32_t cp = 0;
        ret = vmm_alloc(vmm, need, ch == 0 ? "vpu-ch-0" :
                                    ch == 1 ? "vpu-ch-1" : "vpu-ch-2", &cp);
        if (ret < 0 || cp == 0) { printf("alloc fail\n"); continue; }

        printf("phys=0x%08x ", cp);
        uint32_t s[6] = { ch, cp, 0, need, res[ch][0], res[ch][1] };
        ret = ioctl(isp, VPSS_CHN_SET_MEM, s);
        printf("set=0x%08x %s\n", (uint32_t)ret, ret == 0 ? "OK" : "FAIL");
    }

    /* Pipeline */
    printf("[5] Pipeline:\n");
    uint32_t vi[62]; memset(vi, 0, sizeof(vi));
    vi[0] = 1920; vi[1] = 1080;
    ret = ioctl(isp, VPSS_SET_VI_ATTR, vi);
    printf("    SET_VI:    0x%08x %s\n", (uint32_t)ret, ret == 0 ? "PASS" : "FAIL");

    uint32_t en = 0;
    ret = ioctl(isp, VPSS_ENABLE, &en);
    printf("    ENABLE:    0x%08x %s\n", (uint32_t)ret, ret == 0 ? "PASS" : "FAIL");

    uint32_t ca[3] = { 0, 1920, 1080 };
    ret = ioctl(isp, VPSS_SET_CHN_ATTR, ca);
    printf("    CHN_ATTR:  0x%08x %s\n", (uint32_t)ret, ret == 0 ? "PASS" : "FAIL");

    uint32_t c = 0;
    ret = ioctl(isp, VPSS_OPEN_CHN, &c);
    printf("    OPEN_CHN:  0x%08x %s\n", (uint32_t)ret, ret == 0 ? "PASS" : "FAIL");

    uint32_t vo[2] = { 0, 1 };
    ret = ioctl(isp, VPSS_SET_VO_MODE, vo);
    printf("    VO_MODE:   0x%08x %s\n", (uint32_t)ret, ret == 0 ? "PASS" : "FAIL");

    /* Sensor */
    printf("[6] Sensor:\n");
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
        struct i2c_msg m = { .addr = 0x35, .flags = 0, .len = 3, .buf = wb };
        struct i2c_rdwr_ioctl_data x = { .msgs = &m, .nmsgs = 1 };
        for (int i = 0; i < 52; i++) {
            wb[0] = r[i][0] >> 8; wb[1] = r[i][0] & 0xFF; wb[2] = r[i][1];
            if (ioctl(i2c, I2C_RDWR, &x) >= 0) ok++;
        }
        usleep(10000);
        wb[0] = 0x31; wb[1] = 0x41; wb[2] = 0x01;
        ioctl(i2c, I2C_RDWR, &x);
        printf("    %d/52, streaming ON\n", ok);
        close(i2c);
    }

    /* Get frame */
    printf("[7] Frame (500ms wait):\n");
    usleep(500000);
    uint32_t f[26]; memset(f, 0, sizeof(f));
    f[0] = 0;
    ret = ioctl(isp, VPSS_GET_CHN_FRAME, f);
    printf("    ret=0x%08x errno=%d\n", (uint32_t)ret, ret < 0 ? errno : 0);
    if (ret == 0) {
        printf("    *** GOT FRAME! ***\n");
        for (int i = 0; i < 12; i++)
            printf("      [%2d] 0x%08x (%u)\n", i, f[i], f[i]);
    }

    /* Status */
    printf("\n[8] VMM:\n");
    system("cat /proc/driver/vmm 2>/dev/null | grep -E 'BLOCK|total|used|free'");
    printf("[9] ISP:\n");
    system("cat /proc/driver/isp 2>/dev/null | grep -E 'Sensor FPS|Resolution|PIC_START'");

    printf("\n=== DONE ===\n");
    return 0;
}
