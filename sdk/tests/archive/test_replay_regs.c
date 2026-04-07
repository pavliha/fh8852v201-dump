/* test_replay_regs.c — Dump ISP regs from running ipcam, then replay them.
 *
 * Strategy:
 * 1. While ipcam is running, mmap ISP regs via /dev/mem and SAVE all values
 * 2. Kill ipcam (releases /dev/isp)
 * 3. Wait for devices to be released
 * 4. Do our own init: proc config, open devices, MIPI, VPSS, ISP ioctls
 * 5. ioctl 0x690A (isp_start)
 * 6. mmap ISP regs again
 * 7. Write ALL saved values back (replay)
 * 8. pkg_enable (reg 0x100=4, 0x034=5, 0x100|=1)
 * 9. Sensor init via I2C
 * 10. GET_FRAME
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <stdint.h>
#include <signal.h>

#define ISP_BASE  0xE8400000
#define ISP_SIZE  0x3240     /* vendor maps this much */

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

/* Registers to skip when replaying — these are read-only or kernel-managed */
static int skip_reg(uint32_t off) {
    /* Offset 0x000: chip ID (read-only) */
    if (off == 0x000) return 1;
    /* Offset 0x004: version (read-only) */
    if (off == 0x004) return 1;
    /* Offset 0x100: pkg control — we write this separately in pkg_enable */
    if (off == 0x100) return 1;
    /* Offset 0x034: enable — we write this separately */
    if (off == 0x034) return 1;
    return 0;
}

int main(void) {
    FILE *out = fopen("/tmp/r.txt", "w");
    if (!out) out = stdout;
    fprintf(out, "=== REPLAY REGS TEST ===\n"); fflush(out);

    /* ========== PHASE 1: DUMP from running ipcam ========== */
    fprintf(out, "[1] Dumping ISP regs from running ipcam...\n"); fflush(out);

    int mem = open("/dev/mem", O_RDWR | O_SYNC);
    if (mem < 0) {
        fprintf(out, "FATAL: cannot open /dev/mem\n"); fclose(out);
        return 1;
    }

    volatile uint32_t *R = mmap(NULL, ISP_SIZE, PROT_READ, MAP_SHARED, mem, ISP_BASE);
    if (R == MAP_FAILED) {
        fprintf(out, "FATAL: mmap failed\n"); fclose(out);
        return 1;
    }

    /* Save ALL register values */
    uint32_t num_regs = ISP_SIZE / 4;
    uint32_t *saved = malloc(num_regs * sizeof(uint32_t));
    if (!saved) {
        fprintf(out, "FATAL: malloc\n"); fclose(out);
        return 1;
    }

    for (uint32_t i = 0; i < num_regs; i++)
        saved[i] = R[i];

    fprintf(out, "  Saved %u registers (0x%x bytes)\n", num_regs, ISP_SIZE);
    fprintf(out, "  ID=0x%08x  030=%08x  034=%08x  100=%08x  130=%08x  138=%08x\n",
            saved[0], saved[0x030/4], saved[0x034/4], saved[0x100/4],
            saved[0x130/4], saved[0x138/4]);
    fflush(out);

    /* Count non-zero registers */
    int nz = 0;
    for (uint32_t i = 0; i < num_regs; i++)
        if (saved[i]) nz++;
    fprintf(out, "  Non-zero: %d / %u\n", nz, num_regs); fflush(out);

    munmap((void *)R, ISP_SIZE);
    close(mem);

    /* ========== PHASE 2: Kill ipcam ========== */
    fprintf(out, "[2] Killing ipcam...\n"); fflush(out);
    system("killall ipcam 2>/dev/null");
    sleep(3);  /* wait for cleanup */
    fprintf(out, "  Done\n"); fflush(out);

    /* ========== PHASE 3: Our init ========== */
    fprintf(out, "[3] Proc config\n"); fflush(out);
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

    /* Open devices */
    fprintf(out, "[4] Open devices\n"); fflush(out);
    int mp = open("/dev/media_process", 2);
    int isp = open("/dev/isp", 2);
    open("/dev/enc", 2);
    open("/dev/jpeg", 2);
    int vmm = open("/dev/vmm_userdev", 2);
    fprintf(out, "  mp=%d isp=%d vmm=%d\n", mp, isp, vmm); fflush(out);

    if (isp < 0) {
        fprintf(out, "FATAL: cannot open /dev/isp\n"); fclose(out);
        return 1;
    }

    /* MIPI */
    fprintf(out, "[5] MIPI\n"); fflush(out);
    ioctl(isp, 0x40036954, (uint8_t[]){0, 8, 1});
    ioctl(isp, 0x40066955, (uint8_t[]){0, 1, 0, 0, 0, 0});

    /* VPSS */
    fprintf(out, "[6] VPSS\n"); fflush(out);
    uint32_t sz = 0, ph = 0;
    ioctl(isp, 0xC0046966, &sz);
    vmm_alloc(vmm, sz, "vpu-sys", &ph);
    uint32_t sm[3] = {ph, 0, sz};
    ioctl(isp, 0xC00C6964, sm);
    uint32_t q[4] = {0, 1920, 1080, 0};
    ioctl(isp, 0xC0106967, q);
    vmm_alloc(vmm, q[3], "vpu-ch-0", &ph);
    uint32_t cs[6] = {0, ph, 0, q[3], 1920, 1080};
    ioctl(isp, 0xC0186968, cs);
    uint32_t vi[62]; memset(vi, 0, sizeof(vi));
    vi[0] = 1920; vi[1] = 1080;
    ioctl(isp, 0xC0F4696A, vi);
    uint32_t en = 0;
    ioctl(isp, 0xC0046971, &en);
    uint32_t ca[3] = {0, 1920, 1080};
    ioctl(isp, 0xC00C696C, ca);
    uint32_t ch = 0;
    ioctl(isp, 0xC0046973, &ch);
    uint32_t vo[2] = {0, 1};
    ioctl(isp, 0xC00869A4, vo);

    /* ISP info ioctls */
    fprintf(out, "[7] ISP ioctls\n"); fflush(out);
    ioctl(isp, 0x80086929, (uint32_t[]){0, 0});
    ioctl(isp, 0x80306926, (uint32_t[12]){0});
    ioctl(isp, 0x80786905, (uint8_t[120]){0});
    ioctl(isp, 0x40046908, (uint32_t[]){1});
    ioctl(isp, 0x40046909, (uint32_t[]){1});

    /* ioctl 0x690A — isp_start (BEFORE register writes, per vendor order) */
    fprintf(out, "[8] isp_start\n"); fflush(out);
    ioctl(isp, 0x690A, (uint32_t[]){0});

    /* ========== PHASE 4: REPLAY registers ========== */
    fprintf(out, "[9] Replaying ISP regs...\n"); fflush(out);

    mem = open("/dev/mem", O_RDWR | O_SYNC);
    if (mem < 0) {
        fprintf(out, "FATAL: cannot reopen /dev/mem\n"); fclose(out);
        return 1;
    }
    R = mmap(NULL, ISP_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, mem, ISP_BASE);
    if (R == MAP_FAILED) {
        fprintf(out, "FATAL: mmap2 failed\n"); fclose(out);
        return 1;
    }

    /* System reset first (per vendor: isp_core_system_reset) */
    R[0x0F0/4] = (R[0x0F0/4] & ~0x30) | 0x10;
    R[0x0F4/4] = (R[0x0F4/4] & ~0x03) | 0x02;
    usleep(1000);

    /* Write ALL saved values (except skip list) */
    int wrote = 0;
    for (uint32_t i = 0; i < num_regs; i++) {
        uint32_t off = i * 4;
        if (skip_reg(off)) continue;
        if (saved[i] == 0) continue;  /* skip zero regs — don't clobber kernel defaults */
        R[i] = saved[i];
        wrote++;
    }
    fprintf(out, "  Wrote %d non-zero registers\n", wrote); fflush(out);

    /* Verify critical regs */
    fprintf(out, "  VERIFY: 030=%08x 034=%08x 100=%08x 130=%08x 138=%08x 0F0=%08x\n",
            R[0x030/4], R[0x034/4], R[0x100/4], R[0x130/4], R[0x138/4], R[0x0F0/4]);
    fflush(out);

    /* mode/start ioctls */
    fprintf(out, "[10] mode+start\n"); fflush(out);
    ioctl(isp, 0x4004690F, (uint32_t[]){1});
    ioctl(isp, 0x40016920, (uint8_t[]){1});

    /* pkg_enable — triggers first ISP interrupt */
    fprintf(out, "[11] pkg_enable\n"); fflush(out);
    R[0x100/4] = 4;
    R[0x034/4] = 5;
    R[0x100/4] = R[0x100/4] | 1;

    /* ISP kick */
    ioctl(isp, 0x6910, 0);
    fprintf(out, "  KICKED\n"); fflush(out);

    /* ========== PHASE 5: Sensor ========== */
    fprintf(out, "[12] Sensor init\n"); fflush(out);
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
            wb[0] = regs[i][0] >> 8;
            wb[1] = regs[i][0] & 0xFF;
            wb[2] = regs[i][1];
            ioctl(i2c, I2C_RDWR, &x);
        }
        usleep(10000);
        wb[0] = 0x31; wb[1] = 0x41; wb[2] = 0x01;
        ioctl(i2c, I2C_RDWR, &x);
        close(i2c);
        fprintf(out, "  Sensor streaming\n"); fflush(out);
    } else {
        fprintf(out, "  WARN: no I2C\n"); fflush(out);
    }

    /* ========== PHASE 6: Wait and capture ========== */
    fprintf(out, "[13] Wait 3s\n"); fflush(out);
    sleep(3);

    /* Check MIPI */
    volatile uint32_t *MIPI = mmap(NULL, 0x1000, PROT_READ, MAP_SHARED, mem, 0xE0200000);
    if (MIPI && MIPI != MAP_FAILED) {
        fprintf(out, "MIPI: 000=%08x 004=%08x 010=%08x 080=%08x 090=%08x\n",
                MIPI[0], MIPI[1], MIPI[0x10/4], MIPI[0x80/4], MIPI[0x90/4]);
        munmap((void *)MIPI, 0x1000);
    }
    fflush(out);

    /* Check ISP state */
    fprintf(out, "FINAL: 030=%08x 034=%08x 0F0=%08x 100=%08x 130=%08x\n",
            R[0x030/4], R[0x034/4], R[0x0F0/4], R[0x100/4], R[0x130/4]);
    fflush(out);

    /* GET_FRAME */
    fprintf(out, "[14] GET_FRAME:\n"); fflush(out);
    int ret;
    for (int a = 0; a < 15; a++) {
        usleep(200000);
        uint32_t fr[26]; memset(fr, 0, sizeof(fr)); fr[0] = 0;
        ret = ioctl(isp, 0xC0686970, fr);
        fprintf(out, "F%d=%08x\n", a, (uint32_t)ret); fflush(out);
        if (ret == 0) {
            fprintf(out, "*** GOT FRAME! ***\n");
            for (int i = 0; i < 12; i++)
                fprintf(out, "[%2d]=0x%08x (%u)\n", i, fr[i], fr[i]);
            fflush(out);

            /* Save frame data */
            uint32_t pa = fr[1], fsz = fr[3];
            fprintf(out, "phys=0x%08x size=%u\n", pa, fsz);
            if (pa && fsz && fsz < 0x800000) {
                volatile uint8_t *fb = mmap(NULL, fsz + 0x1000,
                    PROT_READ, MAP_SHARED, mem, pa & ~0xFFF);
                if (fb && fb != MAP_FAILED) {
                    FILE *raw = fopen("/tmp/frame.raw", "wb");
                    if (raw) {
                        fwrite((void *)(fb + (pa & 0xFFF)), 1, fsz, raw);
                        fclose(raw);
                        fprintf(out, "Saved /tmp/frame.raw\n");
                    }
                    munmap((void *)fb, fsz + 0x1000);
                }
            }
            fflush(out);
            break;
        }
    }

    munmap((void *)R, ISP_SIZE);
    close(mem);

    fprintf(out, "END\n"); fclose(out);
    execl("/bin/busybox", "busybox", "tftp", "-l", "/tmp/r.txt", "-r", "result.txt",
          "-p", "192.168.8.100", (char *)0);
    return 0;
}
