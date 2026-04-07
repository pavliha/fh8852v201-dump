#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <stdint.h>
#include <errno.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>

/* VPSS ioctls — go to /dev/isp fd! */
#define VPSS_SET_VI_ATTR    0xC0F4696A
#define VPSS_ENABLE         0xC0046971
#define VPSS_SET_CHN_ATTR   0xC00C696C
#define VPSS_OPEN_CHN       0xC0046973
#define VPSS_SET_VO_MODE    0xC00869A4
#define VPSS_GET_CHN_FRAME  0xC0686970
#define VPSS_SYS_QUERY_MEM  0xC0046966
#define VPSS_SYS_SET_MEM    0xC00C6964
#define VPSS_QUERY_CACHE    0xC00469B5
#define VPSS_CHN_QUERY_MEM  0xC0106967
#define VPSS_CHN_SET_MEM    0xC0186968

/* VMM ioctls — /dev/vmm_userdev */
#define VMM_ALLOC           0xC0506D0A
#define VMM_MMAP            0xC0506D14

#define SENSOR_ADDR 0x35

#define L(fmt, ...) printf(fmt, ##__VA_ARGS__)

static int vmm_alloc(int vfd, uint32_t size, const char *name, uint32_t *phys_out)
{
    uint8_t p[80];
    memset(p, 0, sizeof(p));
    *(uint32_t *)(p + 0x08) = size;
    *(uint32_t *)(p + 0x0C) = 8; /* align */
    strncpy((char *)(p + 0x14), name, 15);
    strncpy((char *)(p + 0x24), "anonymous", 31);
    int ret = ioctl(vfd, VMM_ALLOC, p);
    if (ret >= 0) {
        *phys_out = *(uint32_t *)p;
        L("  VMM '%s': phys=0x%08x size=%u\n", name, *phys_out, size);
    }
    return ret;
}

int main(void)
{
    int isp, vmm, ret;
    L("=== VPSS Init via /dev/isp ===\n\n");

    /* Write /proc config first */
    L("[1] /proc config...\n");
    const char *cmds[] = {
        "/proc/driver/vpu:vi_1920_1080", "/proc/driver/vpu:cap_0_1920_1080",
        "/proc/driver/vpu:cap_1_800_576", "/proc/driver/vpu:cap_2_512_288",
        "/proc/driver/vpu:buf_0_2", "/proc/driver/vpu:buf_1_3",
        "/proc/driver/vpu:buf_2_1", "/proc/driver/vpu:cirbuf_on",
        "/proc/driver/vpu:cbufsize_80",
        "/proc/driver/isp:wdr_off", "/proc/driver/isp:cir_on",
        "/proc/driver/enc:stm_1572864",
        "/proc/driver/jpeg:mem_1_131072", "/proc/driver/jpeg:mjpg_0_0",
        "/proc/driver/clock:nn_clk,enable,90000000", NULL
    };
    for (int i = 0; cmds[i]; i++) {
        char path[64], val[64];
        sscanf(cmds[i], "%[^:]:%s", path, val);
        int fd = open(path, O_WRONLY);
        if (fd >= 0) { write(fd, val, strlen(val)); close(fd); }
    }
    L("  Done\n");

    /* Open devices */
    L("\n[2] Open devices...\n");
    isp = open("/dev/isp", O_RDWR);
    vmm = open("/dev/vmm_userdev", O_RDWR);
    int mp = open("/dev/media_process", O_RDWR);
    int enc = open("/dev/enc", O_RDWR);
    int jpeg = open("/dev/jpeg", O_RDWR);
    L("  isp=%d vmm=%d mp=%d enc=%d jpeg=%d\n", isp, vmm, mp, enc, jpeg);

    if (isp < 0 || vmm < 0) { L("FATAL\n"); return 1; }

    /* VPSS SysInitMem — allocate and set system memory */
    L("\n[3] VPSS SysInitMem...\n");
    uint32_t sys_size = 0;
    ret = ioctl(isp, VPSS_SYS_QUERY_MEM, &sys_size);
    L("  QUERY: ret=%d size=%u\n", ret, sys_size);

    if (ret == 0 && sys_size > 0) {
        uint32_t sys_phys = 0;
        ret = vmm_alloc(vmm, sys_size, "vpu-sys", &sys_phys);
        if (ret >= 0) {
            uint32_t mem[3] = { sys_phys, 0 /* virt — kernel will handle */, sys_size };
            ret = ioctl(isp, VPSS_SYS_SET_MEM, mem);
            L("  SET_MEM: ret=0x%08x errno=%d\n", (uint32_t)ret, ret<0?errno:0);

            if (ret == 0) L("  *** SYS MEM INIT SUCCESS! ***\n");
        }
    }

    /* Now try VPSS ioctls on /dev/isp */
    L("\n[4] VPSS pipeline on /dev/isp...\n");

    uint32_t vi[62]; memset(vi, 0, sizeof(vi));
    vi[0] = 1920; vi[1] = 1080;
    ret = ioctl(isp, VPSS_SET_VI_ATTR, vi);
    L("  SET_VI_ATTR: 0x%08x %s\n", (uint32_t)ret, ret==0?"PASS":"FAIL");

    uint32_t en = 0;
    ret = ioctl(isp, VPSS_ENABLE, &en);
    L("  ENABLE: 0x%08x %s\n", (uint32_t)ret, ret==0?"PASS":"FAIL");

    uint32_t ca[3] = {0, 1920, 1080};
    ret = ioctl(isp, VPSS_SET_CHN_ATTR, ca);
    L("  SET_CHN(0): 0x%08x %s\n", (uint32_t)ret, ret==0?"PASS":"FAIL");

    uint32_t c = 0;
    ret = ioctl(isp, VPSS_OPEN_CHN, &c);
    L("  OPEN_CHN(0): 0x%08x %s\n", (uint32_t)ret, ret==0?"PASS":"FAIL");

    uint32_t vo[2] = {0, 1};
    ret = ioctl(isp, VPSS_SET_VO_MODE, vo);
    L("  SET_VO_MODE: 0x%08x %s\n", (uint32_t)ret, ret==0?"PASS":"FAIL");

    /* Sensor init */
    L("\n[5] Sensor init...\n");
    int i2c = open("/dev/i2c-0", O_RDWR);
    if (i2c >= 0) {
        static const uint16_t regs[][2] = {
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
        int ok=0;
        uint8_t wb[3];
        struct i2c_msg m={.addr=SENSOR_ADDR,.flags=0,.len=3,.buf=wb};
        struct i2c_rdwr_ioctl_data x={.msgs=&m,.nmsgs=1};
        for(int i=0;i<52;i++){
            wb[0]=(regs[i][0]>>8)&0xFF;wb[1]=regs[i][0]&0xFF;wb[2]=regs[i][1]&0xFF;
            if(ioctl(i2c,I2C_RDWR,&x)>=0)ok++;
        }
        L("  Regs: %d/52\n",ok);
        usleep(10000);
        wb[0]=0x31;wb[1]=0x41;wb[2]=0x01;
        ioctl(i2c,I2C_RDWR,&x);
        close(i2c);
    }

    /* Try get frame */
    L("\n[6] Get frame...\n");
    usleep(500000);
    uint32_t frame[26]; memset(frame,0,sizeof(frame));
    frame[0]=0;
    ret = ioctl(isp, VPSS_GET_CHN_FRAME, frame);
    L("  GET_FRAME: 0x%08x errno=%d\n", (uint32_t)ret, ret<0?errno:0);
    if (ret == 0) {
        L("  *** GOT FRAME! ***\n");
        for(int i=0;i<12;i++) L("    [%2d] 0x%08x\n",i,frame[i]);
    }

    /* Status */
    L("\n[7] Status:\n");
    system("cat /proc/driver/vmm 2>/dev/null | grep -E 'BLOCK|total'");
    system("cat /proc/driver/vpu 2>/dev/null | grep -E 'Width|Height|Frame|FINISH|Input'");

    close(isp); close(vmm); close(mp); close(enc); close(jpeg);
    L("\n=== Done ===\n");
    return 0;
}
