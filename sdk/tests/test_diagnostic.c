/* test_diagnostic.c — Minimal diagnostic, no ISP processing.
 * Find exactly which ioctl crashes the camera by doing them one at a time.
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

int main(int argc, char **argv) {
    FILE *out = fopen("/tmp/r.txt", "w");
    if (!out) out = stdout;

    int step = 99;
    if (argc > 1) step = atoi(argv[1]);
    fprintf(out, "=== DIAGNOSTIC step=%d ===\n", step); fflush(out);

    /* Proc config (safe) */
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
    fprintf(out, "PROC OK\n"); fflush(out);

    /* Open devices */
    int mp = open("/dev/media_process", 2);
    int isp = open("/dev/isp", 2);
    open("/dev/enc", 2); open("/dev/jpeg", 2);
    int vmm = open("/dev/vmm_userdev", 2);
    fprintf(out, "DEV mp=%d isp=%d vmm=%d\n", mp, isp, vmm); fflush(out);
    if (isp < 0) { fprintf(out, "NO ISP\n"); fclose(out); return 1; }

    if (step < 1) goto done;

    /* Step 1: MIPI */
    fprintf(out, "S1 MIPI\n"); fflush(out);
    ioctl(isp, 0x40036954, (uint8_t[]){0, 8, 1});
    ioctl(isp, 0x40066955, (uint8_t[]){0, 1, 0, 0, 0, 0});
    fprintf(out, "S1 OK\n"); fflush(out);
    if (step < 2) goto done;

    /* Step 2: VPSS */
    fprintf(out, "S2 VPSS\n"); fflush(out);
    {
        uint32_t sz = 0, ph = 0;
        ioctl(isp, 0xC0046966, &sz);
        vmm_alloc(vmm, sz, "vpu-sys", &ph);
        uint32_t sm[3] = {ph, 0, sz}; ioctl(isp, 0xC00C6964, sm);
        uint32_t q[4] = {0, 1920, 1080, 0}; ioctl(isp, 0xC0106967, q);
        vmm_alloc(vmm, q[3], "vpu-ch-0", &ph);
        uint32_t cs[6] = {0, ph, 0, q[3], 1920, 1080}; ioctl(isp, 0xC0186968, cs);
        uint32_t vi[62]; memset(vi, 0, sizeof(vi)); vi[0] = 1920; vi[1] = 1080;
        ioctl(isp, 0xC0F4696A, vi);
        uint32_t en = 0; ioctl(isp, 0xC0046971, &en);
        uint32_t ca[3] = {0, 1920, 1080}; ioctl(isp, 0xC00C696C, ca);
        uint32_t ch = 0; ioctl(isp, 0xC0046973, &ch);
        uint32_t vo[2] = {0, 1}; ioctl(isp, 0xC00869A4, vo);
    }
    fprintf(out, "S2 OK\n"); fflush(out);
    if (step < 3) goto done;

    /* Step 3: ISP info ioctls */
    fprintf(out, "S3 ISP_INFO\n"); fflush(out);
    ioctl(isp, 0x80086929, (uint32_t[]){0, 0});
    ioctl(isp, 0x80306926, (uint32_t[12]){0});
    ioctl(isp, 0x80786905, (uint8_t[120]){0});
    ioctl(isp, 0x40046908, (uint32_t[]){1});
    ioctl(isp, 0x40046909, (uint32_t[]){1});
    fprintf(out, "S3 OK\n"); fflush(out);
    if (step < 4) goto done;

    /* Step 4: isp_start */
    fprintf(out, "S4 ISP_START\n"); fflush(out);
    ioctl(isp, 0x690A, (uint32_t[]){0});
    fprintf(out, "S4 OK\n"); fflush(out);
    if (step < 5) goto done;

    /* Step 5: mode + start */
    fprintf(out, "S5 MODE\n"); fflush(out);
    ioctl(isp, 0x4004690F, (uint32_t[]){1});
    ioctl(isp, 0x40016920, (uint8_t[]){1});
    fprintf(out, "S5 OK\n"); fflush(out);
    if (step < 6) goto done;

    /* Step 6: kick */
    fprintf(out, "S6 KICK\n"); fflush(out);
    ioctl(isp, 0x6910, 0);
    fprintf(out, "S6 OK\n"); fflush(out);
    if (step < 7) goto done;

    /* Step 7: sensor */
    fprintf(out, "S7 SENSOR\n"); fflush(out);
    {
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
                ioctl(i2c, 0x0707, &x);
            }
            usleep(10000);
            wb[0] = 0x31; wb[1] = 0x41; wb[2] = 0x01;
            ioctl(i2c, 0x0707, &x);
            close(i2c);
        }
    }
    fprintf(out, "S7 OK\n"); fflush(out);
    if (step < 8) goto done;

    /* Step 8: wait + GET_FRAME */
    fprintf(out, "S8 WAIT+FRAME\n"); fflush(out);
    sleep(3);
    {
        uint32_t fr[26]; memset(fr, 0, sizeof(fr));
        fr[0] = 0; fr[22] = 2000;
        int ret = ioctl(isp, 0xC0686970, fr);
        fprintf(out, "FRAME ret=%d (0x%08x)\n", ret, (uint32_t)ret);
    }
    fprintf(out, "S8 OK\n"); fflush(out);

done:
    fprintf(out, "DONE\n"); fclose(out);
    execl("/bin/busybox", "busybox", "tftp", "-l", "/tmp/r.txt", "-r", "result.txt",
          "-p", "192.168.8.100", (char *)0);
    return 0;
}
