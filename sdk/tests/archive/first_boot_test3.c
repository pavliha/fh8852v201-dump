/**
 * First-boot pipeline test v3.
 * Complete init: /proc config → open all devices → VMM alloc → VPSS init.
 */
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

/* VPSS ioctls on /dev/media_process (via vpu fd) */
#define VPSS_SET_VI_ATTR      0xC0F4696A
#define VPSS_ENABLE           0xC0046971
#define VPSS_SET_CHN_ATTR     0xC00C696C
#define VPSS_OPEN_CHN         0xC0046973
#define VPSS_SET_VO_MODE      0xC00869A4
#define VPSS_GET_CHN_FRAME    0xC0686970
#define VPSS_SYS_QUERY_MEM   0xC0046966
#define VPSS_SYS_SET_MEM     0xC00C6964
#define VPSS_QUERY_USE_CACHE  0xC00469B5
#define VPSS_CHN_QUERY_MEM   0xC0106967
#define VPSS_CHN_SET_MEM     0xC0186968
#define VPSS_GET_CHN_CAP     0xC0086965   /* FH_VPSS_GetChnCapality */

/* VMM ioctls on /dev/vmm_userdev */
#define VMM_ALLOC            0xC0506D0A
#define VMM_MMAP             0xC0506D14
#define VMM_FREE             0xC0506D0B   /* guessed */

/* VENC ioctls */
#define VENC_GET_STREAM      0xC1A04D05

#define SENSOR_ADDR 0x35

static FILE *logf;
#define L(fmt, ...) do { printf(fmt, ##__VA_ARGS__); \
    if(logf){fprintf(logf, fmt, ##__VA_ARGS__);fflush(logf);} } while(0)

static void write_proc(const char *p, const char *v) {
    int fd = open(p, O_WRONLY);
    if (fd >= 0) { write(fd, v, strlen(v)); close(fd); L("  %s <- %s\n", p, v); }
    else L("  %s FAIL\n", p);
}

static int vmm_alloc(int vmm_fd, uint32_t size, uint32_t align,
                     const char *name, uint32_t *phys, void **virt)
{
    uint8_t param[80];
    memset(param, 0, sizeof(param));
    /* param layout from decompilation:
     * [0x00] phys_addr_out (uint64)
     * [0x08] size
     * [0x0C] alignment
     * [0x14] name string (16 bytes)
     * [0x24] zone name (32 bytes) */
    *(uint32_t *)(param + 0x08) = size;
    *(uint32_t *)(param + 0x0C) = align;
    strncpy((char *)(param + 0x14), name, 15);
    strncpy((char *)(param + 0x24), "anonymous", 31);

    int ret = ioctl(vmm_fd, VMM_ALLOC, param);
    if (ret < 0) return ret;

    *phys = *(uint32_t *)(param + 0x00);
    L("    VMM alloc '%s': phys=0x%08x size=%u\n", name, *phys, size);

    /* mmap it */
    uint32_t flags = 0x103; /* from decompilation: PROT_READ|PROT_WRITE + shared */
    *(uint32_t *)(param + 0x18) = flags;
    ret = ioctl(vmm_fd, VMM_MMAP, param);
    if (ret < 0) {
        L("    VMM mmap FAIL errno=%d\n", errno);
        *virt = NULL;
        return 0; /* alloc succeeded, mmap failed — still usable for ioctl */
    }

    /* Virtual addr from mmap response */
    *virt = (void *)(uintptr_t)*(uint32_t *)(param + 0x1C);
    return 0;
}

int main(void)
{
    int mfd, isp_fd, enc_fd, jpeg_fd, vmm_fd, i2c_fd;
    int ret;
    int pass = 0, fail = 0;

    logf = fopen("/tmp/pipeline_results.txt", "w");
    L("=== FH8852V201 Pipeline Test v3 (full init) ===\n");
    L("PID: %d\n\n", getpid());

    /* === PHASE 1: /proc config === */
    L("[1] /proc/driver config...\n");
    write_proc("/proc/driver/vpu", "vi_1920_1080");
    write_proc("/proc/driver/vpu", "cap_0_1920_1080");
    write_proc("/proc/driver/vpu", "cap_1_800_576");
    write_proc("/proc/driver/vpu", "cap_2_512_288");
    write_proc("/proc/driver/vpu", "buf_0_2");
    write_proc("/proc/driver/vpu", "buf_1_3");
    write_proc("/proc/driver/vpu", "buf_2_1");
    write_proc("/proc/driver/vpu", "cirbuf_on");
    write_proc("/proc/driver/vpu", "cbufsize_80");
    write_proc("/proc/driver/isp", "wdr_off");
    write_proc("/proc/driver/isp", "cir_on");
    write_proc("/proc/driver/enc", "stm_1572864");
    write_proc("/proc/driver/jpeg", "mem_1_131072");
    write_proc("/proc/driver/jpeg", "mjpg_0_0");
    write_proc("/proc/driver/clock", "nn_clk,enable,90000000");

    /* === PHASE 2: Open all devices === */
    L("\n[2] Opening devices...\n");

    mfd = open("/dev/media_process", O_RDWR);
    L("  /dev/media_process: fd=%d %s\n", mfd, mfd >= 0 ? "OK" : strerror(errno));

    isp_fd = open("/dev/isp", O_RDONLY);
    L("  /dev/isp: fd=%d %s\n", isp_fd, isp_fd >= 0 ? "OK" : strerror(errno));

    enc_fd = open("/dev/enc", O_RDONLY);
    L("  /dev/enc: fd=%d %s\n", enc_fd, enc_fd >= 0 ? "OK" : strerror(errno));

    jpeg_fd = open("/dev/jpeg", O_RDONLY);
    L("  /dev/jpeg: fd=%d %s\n", jpeg_fd, jpeg_fd >= 0 ? "OK" : strerror(errno));

    vmm_fd = open("/dev/vmm_userdev", O_RDWR);
    L("  /dev/vmm_userdev: fd=%d %s\n", vmm_fd, vmm_fd >= 0 ? "OK" : strerror(errno));

    if (mfd < 0 || vmm_fd < 0) {
        L("FATAL: cannot open core devices\n");
        fail++;
        goto done;
    }
    pass++;

    /* === PHASE 3: VPSS SysInitMem === */
    L("\n[3] VPSS_SysInitMem...\n");

    /* Query system memory size */
    uint32_t sys_mem_size = 0;
    ret = ioctl(mfd, VPSS_SYS_QUERY_MEM, &sys_mem_size);
    L("  SYS_QUERY_MEM: ret=%d size=%u\n", ret, sys_mem_size);

    if (ret == 0 && sys_mem_size > 0) {
        /* Allocate VMM for VPU system */
        uint32_t sys_phys = 0;
        void *sys_virt = NULL;
        ret = vmm_alloc(vmm_fd, sys_mem_size, 8, "vpu-sys", &sys_phys, &sys_virt);
        if (ret == 0) {
            pass++;
            /* Set system memory base */
            uint32_t mem_info[3] = { sys_phys, (uint32_t)(uintptr_t)sys_virt, sys_mem_size };
            ret = ioctl(mfd, VPSS_SYS_SET_MEM, mem_info);
            L("  SYS_SET_MEM: ret=%d (phys=0x%08x)\n", ret, sys_phys);
            if (ret == 0) pass++; else fail++;
        } else {
            L("  VMM alloc failed\n");
            fail++;
        }
    } else {
        fail++;
    }

    /* Query cache mode */
    uint32_t use_cache = 0;
    ioctl(mfd, VPSS_QUERY_USE_CACHE, &use_cache);
    L("  USE_CACHE: %u\n", use_cache);

    /* Init channel memory */
    for (int ch = 0; ch < 3; ch++) {
        uint32_t cap[2] = { 0, 0 };
        /* GetChnCapality returns {width, height} for each channel */
        uint32_t cap_query[2] = { (uint32_t)ch, 0 };
        ret = ioctl(mfd, VPSS_GET_CHN_CAP, cap_query);
        L("  CH%d capability: ret=%d w=%u h=%u\n", ch, ret, cap_query[0], cap_query[1]);

        if (ret == 0 && cap_query[0] > 0 && cap_query[1] > 0) {
            /* Query channel memory */
            uint32_t chn_query[4] = { (uint32_t)ch, cap_query[0], cap_query[1], 0 };
            ret = ioctl(mfd, VPSS_CHN_QUERY_MEM, chn_query);
            uint32_t chn_size = chn_query[3];
            L("  CH%d mem query: ret=%d size=%u\n", ch, ret, chn_size);

            if (ret == 0 && chn_size > 0) {
                char name[16];
                snprintf(name, sizeof(name), "vpu-ch-%d", ch);
                uint32_t chn_phys = 0;
                void *chn_virt = NULL;
                ret = vmm_alloc(vmm_fd, chn_size, 8, name, &chn_phys, &chn_virt);
                if (ret == 0) {
                    uint32_t chn_set[6] = { (uint32_t)ch, 0, 0, 0, chn_phys, chn_size };
                    ret = ioctl(mfd, VPSS_CHN_SET_MEM, chn_set);
                    L("  CH%d set mem: ret=%d\n", ch, ret);
                    if (ret == 0) pass++; else fail++;
                }
            }
        }
    }

    /* === PHASE 4: VPSS pipeline setup === */
    L("\n[4] VPSS pipeline...\n");

    uint32_t vi[62]; memset(vi, 0, sizeof(vi));
    vi[0] = 1920; vi[1] = 1080;
    ret = ioctl(mfd, VPSS_SET_VI_ATTR, vi);
    L("  SET_VI_ATTR: 0x%08x %s\n", (uint32_t)ret, ret==0?"PASS":"FAIL");
    if (ret == 0) pass++; else fail++;

    uint32_t en = 0;
    ret = ioctl(mfd, VPSS_ENABLE, &en);
    L("  ENABLE: 0x%08x %s\n", (uint32_t)ret, ret==0?"PASS":"FAIL");
    if (ret == 0) pass++; else fail++;

    uint32_t ca[3] = { 0, 1920, 1080 };
    ret = ioctl(mfd, VPSS_SET_CHN_ATTR, ca);
    L("  SET_CHN_ATTR(0): 0x%08x %s\n", (uint32_t)ret, ret==0?"PASS":"FAIL");
    if (ret == 0) pass++; else fail++;

    uint32_t chn = 0;
    ret = ioctl(mfd, VPSS_OPEN_CHN, &chn);
    L("  OPEN_CHN(0): 0x%08x %s\n", (uint32_t)ret, ret==0?"PASS":"FAIL");
    if (ret == 0) pass++; else fail++;

    uint32_t vo[2] = { 0, 1 };
    ret = ioctl(mfd, VPSS_SET_VO_MODE, vo);
    L("  SET_VO_MODE: 0x%08x %s\n", (uint32_t)ret, ret==0?"PASS":"FAIL");
    if (ret == 0) pass++; else fail++;

    /* === PHASE 5: Sensor init === */
    L("\n[5] Sensor...\n");
    i2c_fd = open("/dev/i2c-0", O_RDWR);
    if (i2c_fd >= 0) {
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
        int ok = 0;
        uint8_t wb[3];
        struct i2c_msg m = { .addr=SENSOR_ADDR, .flags=0, .len=3, .buf=wb };
        struct i2c_rdwr_ioctl_data x = { .msgs=&m, .nmsgs=1 };
        for (int i = 0; i < 52; i++) {
            wb[0]=(regs[i][0]>>8)&0xFF; wb[1]=regs[i][0]&0xFF; wb[2]=regs[i][1]&0xFF;
            if (ioctl(i2c_fd, I2C_RDWR, &x) >= 0) ok++;
        }
        L("  Regs: %d/52\n", ok);
        usleep(10000);
        wb[0]=0x31; wb[1]=0x41; wb[2]=0x01;
        ioctl(i2c_fd, I2C_RDWR, &x);
        if (ok == 52) pass++; else fail++;
        close(i2c_fd);
    }

    /* === PHASE 6: Get frame === */
    L("\n[6] Getting frame...\n");
    usleep(500000);

    uint32_t frame[26]; memset(frame, 0, sizeof(frame));
    frame[0] = 0;
    ret = ioctl(mfd, VPSS_GET_CHN_FRAME, frame);
    L("  GET_FRAME(0): 0x%08x errno=%d\n", (uint32_t)ret, ret<0?errno:0);
    if (ret == 0) {
        pass++;
        L("  *** GOT FRAME! ***\n");
        for (int i = 0; i < 12; i++)
            L("    [%2d] 0x%08x\n", i, frame[i]);
    } else {
        fail++;
    }

    /* === PHASE 7: Status dump === */
    L("\n[7] Status:\n");
    system("cat /proc/driver/vmm 2>/dev/null | grep -E 'BLOCK|total|used|free' >> /tmp/pipeline_results.txt");
    system("cat /proc/driver/vpu 2>/dev/null | grep -E 'Width|Height|Frame|FINISH' >> /tmp/pipeline_results.txt");

    if (mfd >= 0) close(mfd);
    if (isp_fd >= 0) close(isp_fd);
    if (enc_fd >= 0) close(enc_fd);
    if (jpeg_fd >= 0) close(jpeg_fd);
    if (vmm_fd >= 0) close(vmm_fd);

done:
    L("\n========================================\n");
    L("RESULTS: %d PASS, %d FAIL\n", pass, fail);
    L("========================================\n");
    if (logf) fclose(logf);
    system("tftp -l /tmp/pipeline_results.txt -r pipeline_results.txt -p 192.168.8.100 2>/dev/null");
    return 0;
}
