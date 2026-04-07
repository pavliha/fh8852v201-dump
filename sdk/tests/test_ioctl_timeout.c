/* test_ioctl_timeout.c — Pure ioctl test with CORRECT timeout.
 *
 * Key fixes vs previous tests:
 * 1. GET_FRAME timeout at fr[22] = 1000ms (was 0!)
 * 2. Added ioctl 0x40046906 (SetSensorFmt = 0x203)
 * 3. NO /dev/mem writes at all — let kernel handle ISP config
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
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

int main(void) {
    FILE *out = fopen("/tmp/r.txt", "w");
    if (!out) out = stdout;
    fprintf(out, "=== IOCTL TIMEOUT TEST ===\n"); fflush(out);

    /* 1. Proc config */
    const char *c[][2] = {
        {"/proc/driver/vpu", "vi_1920_1080"},
        {"/proc/driver/vpu", "cap_0_1920_1080"},
        {"/proc/driver/vpu", "cap_1_800_576"},
        {"/proc/driver/vpu", "cap_2_512_288"},
        {"/proc/driver/vpu", "buf_0_2"},
        {"/proc/driver/vpu", "buf_1_3"},
        {"/proc/driver/vpu", "buf_2_1"},
        {"/proc/driver/vpu", "cirbuf_on"},
        {"/proc/driver/vpu", "cbufsize_80"},
        {"/proc/driver/isp", "wdr_off"},
        {"/proc/driver/isp", "cir_on"},
        {"/proc/driver/enc", "stm_1572864"},
        {"/proc/driver/jpeg", "mem_1_131072"},
        {"/proc/driver/jpeg", "mjpg_0_0"},
        {"/proc/driver/clock", "nn_clk,enable,90000000"},
        {0, 0}
    };
    for (int i = 0; c[i][0]; i++) {
        int d = open(c[i][0], 1);
        if (d >= 0) { write(d, c[i][1], strlen(c[i][1])); close(d); }
    }
    fprintf(out, "[1] proc OK\n"); fflush(out);

    /* 2. Open devices */
    int mp = open("/dev/media_process", 2);
    int isp = open("/dev/isp", 2);
    open("/dev/enc", 2);
    open("/dev/jpeg", 2);
    int vmm = open("/dev/vmm_userdev", 2);
    fprintf(out, "[2] DEV mp=%d isp=%d vmm=%d\n", mp, isp, vmm); fflush(out);
    if (isp < 0) { fprintf(out, "FATAL: /dev/isp\n"); fclose(out); return 1; }

    /* 3. MIPI init */
    fprintf(out, "[3] MIPI\n"); fflush(out);
    int r;
    r = ioctl(isp, 0x40036954, (uint8_t[]){0, 8, 1});
    fprintf(out, "  ctrl=%d\n", r);
    r = ioctl(isp, 0x40066955, (uint8_t[]){0, 1, 0, 0, 0, 0});
    fprintf(out, "  wrap=%d\n", r); fflush(out);

    /* 4. VPSS init */
    fprintf(out, "[4] VPSS\n"); fflush(out);
    uint32_t sz = 0, ph = 0;
    r = ioctl(isp, 0xC0046966, &sz);
    fprintf(out, "  sysmem_size=%u r=%d\n", sz, r);
    vmm_alloc(vmm, sz, "vpu-sys", &ph);
    fprintf(out, "  sys_phys=0x%08x\n", ph);
    uint32_t sm[3] = {ph, 0, sz};
    r = ioctl(isp, 0xC00C6964, sm);
    fprintf(out, "  set_sys=%d\n", r);
    uint32_t q[4] = {0, 1920, 1080, 0};
    r = ioctl(isp, 0xC0106967, q);
    fprintf(out, "  create_grp=%d bufsz=%u\n", r, q[3]);
    vmm_alloc(vmm, q[3], "vpu-ch-0", &ph);
    fprintf(out, "  ch0_phys=0x%08x\n", ph);
    uint32_t cs[6] = {0, ph, 0, q[3], 1920, 1080};
    r = ioctl(isp, 0xC0186968, cs);
    fprintf(out, "  set_chn=%d\n", r);
    uint32_t vi[62]; memset(vi, 0, sizeof(vi));
    vi[0] = 1920; vi[1] = 1080;
    r = ioctl(isp, 0xC0F4696A, vi);
    fprintf(out, "  set_vi=%d\n", r);
    uint32_t en = 0;
    r = ioctl(isp, 0xC0046971, &en);
    fprintf(out, "  enable=%d\n", r);
    uint32_t ca[3] = {0, 1920, 1080};
    r = ioctl(isp, 0xC00C696C, ca);
    fprintf(out, "  set_cap=%d\n", r);
    uint32_t ch = 0;
    r = ioctl(isp, 0xC0046973, &ch);
    fprintf(out, "  enable_ch=%d\n", r);
    uint32_t vo[2] = {0, 1};
    r = ioctl(isp, 0xC00869A4, vo);
    fprintf(out, "  set_vo=%d\n", r);
    fflush(out);

    /* 5. ISP info ioctls */
    fprintf(out, "[5] ISP info\n"); fflush(out);
    uint32_t chip[2] = {0, 0};
    r = ioctl(isp, 0x80086929, chip);
    fprintf(out, "  chip=%d [%08x %08x]\n", r, chip[0], chip[1]);
    uint32_t hwcfg[12]; memset(hwcfg, 0, sizeof(hwcfg));
    r = ioctl(isp, 0x80306926, hwcfg);
    fprintf(out, "  hwcfg=%d [%08x %08x %08x %08x]\n", r, hwcfg[0], hwcfg[1], hwcfg[2], hwcfg[3]);
    uint8_t sinfo[120]; memset(sinfo, 0, sizeof(sinfo));
    r = ioctl(isp, 0x80786905, sinfo);
    fprintf(out, "  sinfo=%d\n", r);
    fflush(out);

    /* 6. Sensor format (NEW! — was missing before) */
    fprintf(out, "[6] SensorFmt\n"); fflush(out);
    uint32_t fmt = 0x203;
    r = ioctl(isp, 0x40046906, &fmt);
    fprintf(out, "  fmt=%d\n", r);

    /* 7. ISP enable/mode */
    fprintf(out, "[7] ISP enable\n"); fflush(out);
    r = ioctl(isp, 0x40046908, (uint32_t[]){1});
    fprintf(out, "  908=%d\n", r);
    r = ioctl(isp, 0x40046909, (uint32_t[]){1});
    fprintf(out, "  909=%d\n", r);
    fflush(out);

    /* 8. isp_start */
    fprintf(out, "[8] isp_start\n"); fflush(out);
    uint32_t start_arg[2] = {0, 0};
    r = ioctl(isp, 0x690A, start_arg);
    fprintf(out, "  start=%d\n", r);

    /* 9. mode + processing start */
    fprintf(out, "[9] mode+start\n"); fflush(out);
    r = ioctl(isp, 0x4004690F, (uint32_t[]){1});
    fprintf(out, "  mode=%d\n", r);
    r = ioctl(isp, 0x40016920, (uint8_t[]){1});
    fprintf(out, "  start20=%d\n", r);

    /* 10. ISP kick */
    fprintf(out, "[10] kick\n"); fflush(out);
    r = ioctl(isp, 0x6910, 0);
    fprintf(out, "  kick=%d\n", r);
    fflush(out);

    /* 11. Sensor init via I2C */
    fprintf(out, "[11] Sensor\n"); fflush(out);
    int i2c = open("/dev/i2c-0", 2);
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
        uint8_t wb[3];
        struct i2c_msg m = {.addr = 0x35, .flags = 0, .len = 3, .buf = wb};
        struct i2c_rdwr_ioctl_data x = {.msgs = &m, .nmsgs = 1};
        for (int i = 0; i < 52; i++) {
            wb[0] = regs[i][0] >> 8; wb[1] = regs[i][0] & 0xFF; wb[2] = regs[i][1];
            ioctl(i2c, I2C_RDWR, &x);
        }
        usleep(10000);
        wb[0] = 0x31; wb[1] = 0x41; wb[2] = 0x01;
        ioctl(i2c, I2C_RDWR, &x);
        close(i2c);
        fprintf(out, "  sensor streaming\n");
    } else {
        fprintf(out, "  WARN: no I2C\n");
    }
    fflush(out);

    /* 12. Wait for ISP to process */
    fprintf(out, "[12] Wait 3s\n"); fflush(out);
    sleep(3);

    /* 13. GET_FRAME with CORRECT timeout */
    fprintf(out, "[13] GET_FRAME (timeout=1000ms):\n"); fflush(out);
    int ret;
    for (int a = 0; a < 10; a++) {
        uint32_t fr[26]; memset(fr, 0, sizeof(fr));
        fr[0] = 0;     /* channel 0 */
        fr[22] = 1000;  /* timeout = 1000ms *** THIS WAS MISSING! *** */
        fr[23] = 0;     /* flags */
        ret = ioctl(isp, 0xC0686970, fr);
        fprintf(out, "F%d ret=%d (0x%08x)\n", a, ret, (uint32_t)ret); fflush(out);
        if (ret == 0) {
            fprintf(out, "*** GOT FRAME! ***\n");
            for (int i = 0; i < 16; i++)
                fprintf(out, "[%2d]=0x%08x (%u)\n", i, fr[i], fr[i]);
            fflush(out);
            break;
        }
        /* If error, try with different timeout/channel */
        if (a == 4) {
            fprintf(out, "  Trying timeout=3000...\n"); fflush(out);
            fr[22] = 3000;
        }
    }

    /* 14. Also try GET_FRAME with channel 1 */
    fprintf(out, "[14] Try ch1:\n"); fflush(out);
    {
        uint32_t fr[26]; memset(fr, 0, sizeof(fr));
        fr[0] = 1;     /* channel 1 */
        fr[22] = 2000;
        ret = ioctl(isp, 0xC0686970, fr);
        fprintf(out, "ch1 ret=%d (0x%08x)\n", ret, (uint32_t)ret); fflush(out);
    }

    fprintf(out, "END\n"); fclose(out);
    execl("/bin/busybox", "busybox", "tftp", "-l", "/tmp/r.txt", "-r", "result.txt",
          "-p", "192.168.8.100", (char *)0);
    return 0;
}
