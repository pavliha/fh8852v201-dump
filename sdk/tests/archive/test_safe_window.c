/* test_safe_window.c — Write ISP regs in the SAFE WINDOW:
 *   AFTER open("/dev/isp") but BEFORE ioctl 0x690A (isp_start).
 *
 * The interrupt handler is registered at open() but checks offset 0x364.
 * Since 0x364 = 0 until isp_start sets it to 1, the handler returns early.
 * This gives us a safe window to write all ISP registers.
 *
 * Strategy:
 * 1. proc config
 * 2. Open devices (kernel registers IRQ, writes some regs)
 * 3. MIPI init
 * 4. VPSS init
 * 5. ISP info ioctls (chip info, hw cfg, sensor info)
 * 6. *** SAFE WINDOW: mmap + read what kernel set + write our config ***
 * 7. isp_start (0x690A) — sets 0x364=1
 * 8. mode/start ioctls
 * 9. pkg_enable via regs (triggers first interrupt)
 * 10. ISP kick
 * 11. Sensor init
 * 12. Wait + GET_FRAME
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
struct i2c_msg { uint16_t addr; uint16_t flags; uint16_t len; uint8_t *buf; };
struct i2c_rdwr_ioctl_data { struct i2c_msg *msgs; uint32_t nmsgs; };

static int vmm_alloc(int fd, uint32_t size, const char *name, uint32_t *phys) {
    static uint8_t b[80];
    memset(b, 0, 80);
    *(uint32_t *)(b + 8) = 8;
    *(uint32_t *)(b + 12) = size;
    strncpy((char *)(b + 0x1C), name, 15);
    strncpy((char *)(b + 0x2C), "anonymous", 39);
    int r = ioctl(fd, 0xC0506D0A, b);
    *phys = (r >= 0) ? *(uint32_t *)b : 0;
    return r;
}

static void W(volatile uint32_t *R, uint32_t off, uint32_t val) { R[off / 4] = val; }
static uint32_t RD(volatile uint32_t *R, uint32_t off) { return R[off / 4]; }

int main(void) {
    FILE *out = fopen("/tmp/r.txt", "w");
    if (!out) out = stdout;
    fprintf(out, "=== SAFE WINDOW TEST ===\n"); fflush(out);

    /* Step 1: proc config */
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
    fprintf(out, "[1] proc config done\n"); fflush(out);

    /* Step 2: Open devices — kernel registers ISP IRQ here */
    int mp = open("/dev/media_process", 2);
    int isp = open("/dev/isp", 2);
    open("/dev/enc", 2);
    open("/dev/jpeg", 2);
    int vmm = open("/dev/vmm_userdev", 2);
    fprintf(out, "[2] DEV mp=%d isp=%d vmm=%d\n", mp, isp, vmm); fflush(out);

    /* Step 3: MIPI init */
    fprintf(out, "[3] MIPI\n"); fflush(out);
    ioctl(isp, 0x40036954, (uint8_t[]){0, 8, 1});
    ioctl(isp, 0x40066955, (uint8_t[]){0, 1, 0, 0, 0, 0});

    /* Step 4: VPSS init */
    fprintf(out, "[4] VPSS\n"); fflush(out);
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

    /* Step 5: ISP info ioctls (NOT isp_start yet!) */
    fprintf(out, "[5] ISP info ioctls\n"); fflush(out);
    ioctl(isp, 0x80086929, (uint32_t[]){0, 0});     /* chip info */
    ioctl(isp, 0x80306926, (uint32_t[12]){0});       /* hw cfg */
    ioctl(isp, 0x80786905, (uint8_t[120]){0});       /* sensor info */
    ioctl(isp, 0x40046908, (uint32_t[]){1});          /* set something */
    ioctl(isp, 0x40046909, (uint32_t[]){1});          /* set something */

    /* ====== SAFE WINDOW: ISP IRQ registered but 0x364=0 ====== */
    fprintf(out, "[6] === SAFE WINDOW: mmap ISP regs ===\n"); fflush(out);
    int mem = open("/dev/mem", O_RDWR | O_SYNC);
    volatile uint32_t *R = NULL;
    if (mem >= 0)
        R = mmap(NULL, 0x4000, PROT_READ | PROT_WRITE, MAP_SHARED, mem, 0xE8400000);

    if (!R || R == (void *)-1) {
        fprintf(out, "MMAP FAILED\n"); fclose(out);
        return 1;
    }
    fprintf(out, "  ISP ID=0x%08x\n", R[0]); fflush(out);

    /* Dump key registers BEFORE we write (see what kernel set) */
    fprintf(out, "BEFORE:\n");
    static const uint16_t dump_regs[] = {
        0x008, 0x010, 0x014, 0x018, 0x02C, 0x030, 0x034, 0x044,
        0x05C, 0x064, 0x080, 0x0D0, 0x0E0, 0x0E4, 0x0E8, 0x0F0,
        0x0F4, 0x100, 0x104, 0x108, 0x114, 0x128, 0x130, 0x134,
        0x138, 0x13C, 0x140, 0x144, 0x148, 0x14C, 0x150, 0x154,
        0x178, 0x184, 0x1C4, 0x1F0, 0x1F4, 0x1F8, 0x1FC,
        0x230, 0x234, 0x250, 0x254, 0x258, 0x26C, 0x278,
        0x2AC, 0x2E8, 0x2F0, 0x300, 0x308, 0x310, 0x318, 0x320,
        0x490, 0x494, 0x4E0, 0x4E4, 0x4E8, 0x500, 0x504, 0x508,
        0x578, 0x57C, 0xA30, 0xA34, 0xA38, 0xA3C, 0xA40, 0xA44,
    };
    for (int i = 0; i < (int)(sizeof(dump_regs)/sizeof(dump_regs[0])); i++)
        fprintf(out, "  %03x=%08x", dump_regs[i], RD(R, dump_regs[i]));
    fprintf(out, "\n"); fflush(out);

    /* Step 6b: Write ALL ISP registers */
    fprintf(out, "  Writing ISP regs...\n"); fflush(out);

    /* Picture size: 1920x1080 */
    uint32_t ps = (1920 - 1) | ((1080 - 1) << 16);
    W(R, 0x0D0, ps); W(R, 0x130, ps); W(R, 0x138, ps); W(R, 0x144, ps);
    W(R, 0x134, 0); W(R, 0x148, 0);  /* no crop */
    W(R, 0x14C, (640 - 1) | ((480 - 1) << 16));  /* sub res */

    /* Module enable */
    W(R, 0x010, 0x0003FFFF);
    W(R, 0x014, 0x00001FFF);
    W(R, 0x018, 0x0007FFFF);

    /* ISP gain + config */
    W(R, 0x02C, 0x80);         /* gain 1x */
    W(R, 0x030, 0x00FEFF82);   /* ISP config — THE critical register */
    W(R, 0x034, 0x05);         /* enable */
    W(R, 0x044, 0x03);         /* AE mode */

    /* Misc enables */
    W(R, 0x05C, 0x00007FFF);
    W(R, 0x064, 0x0007FFFF);
    W(R, 0x1C4, 0x0C);

    /* Tone mapping curve 1 */
    W(R, 0x080, 0x60502010);
    W(R, 0x084, 0x908F8070);
    W(R, 0x088, 0xD0C0B0A0);
    W(R, 0x08C, 0x00FFF0E0);

    /* Gamma pre-curve (linear) */
    W(R, 0x090, 0x10301010);
    W(R, 0x094, 0x00101010);
    W(R, 0x098, 0x10101010);
    W(R, 0x09C, 0x000F1010);

    /* Tone mapping curve 2 */
    W(R, 0x0A0, 0x50482820);
    W(R, 0x0A4, 0x69686058);
    W(R, 0x0A8, 0xA8987870);
    W(R, 0x0AC, 0xE8E0C8B8);
    W(R, 0x0B0, 0x00FFF8F0);

    /* Filter config */
    W(R, 0x0E0, 0x00400000);
    W(R, 0x0E4, RD(R, 0x0E4) | 2);  /* preserve kernel bits, enable filter */
    W(R, 0x0E8, ((1080 - 1) << 16) | ((1920 / 48) - 1));

    /* ISP processing control */
    W(R, 0x0F0, 0x09);
    W(R, 0x0F4, 0x01010003);

    /* Init markers */
    W(R, 0x104, 0xFEDCBA98);
    W(R, 0x108, 0x76543210);
    W(R, 0x114, 0x00000C01);
    W(R, 0x128, 0x00000500);

    /* Misc */
    W(R, 0x178, 0x00021002);
    W(R, 0x184, 0x00010000);
    W(R, 0x1F0, 0x03FF0000); W(R, 0x1F4, 1);
    W(R, 0x1F8, 0x03FF0000); W(R, 0x1FC, 1);

    /* AWB gains */
    W(R, 0x230, 0x02000200); W(R, 0x234, 0x02000200);
    W(R, 0x250, 2); W(R, 0x254, 0x00100010); W(R, 0x258, 0x00100010);
    W(R, 0x26C, 0x003FF000); W(R, 0x278, 0x01000001);

    /* CCM identity */
    W(R, 0xA30, 0x200); W(R, 0xA34, 0);
    W(R, 0xA38, 0x200); W(R, 0xA3C, 0);
    W(R, 0xA40, 0x200); W(R, 0xA44, 0);

    /* Sharpness */
    W(R, 0x2AC, 0x000F1F1F); W(R, 0x2B0, 0x00470029);
    W(R, 0x2B4, 0x0000003F); W(R, 0x2B8, 0x238F1F0C);
    W(R, 0x2BC, 0x000023FF);
    W(R, 0x2C0, 0x01010101); W(R, 0x2C4, 0x01010101); W(R, 0x2C8, 1);
    W(R, 0x2D0, 0x000F1F1F); W(R, 0x2D4, 0x00470029); W(R, 0x2D8, 0x0000003F);

    /* Buffer stride: raw 10bpp aligned to 8 */
    uint32_t stride = (((1920 * 10 + 7) / 8) + 7) & ~7;
    uint32_t q_stride = ((((1920 / 4) * 10) / 8) + 7) & ~7;
    W(R, 0x2E8, stride);
    W(R, 0x2F0, stride | 0x80000000);
    W(R, 0x300, 0x80000000);
    W(R, 0x308, q_stride);
    W(R, 0x310, q_stride | 0x80000000);
    W(R, 0x318, q_stride);
    W(R, 0x320, q_stride | 0x80000000);

    /* Statistics */
    W(R, 0x494, 0x9015);
    uint32_t bw = (1920 / 32) - 1, bh = (1080 / 32) - 1;
    W(R, 0x490, (bh << 24) | ((1920 / 48 - 1) << 16));
    W(R, 0x4E0, (bw | (bh << 8)) | 0x0F0F0000);
    W(R, 0x4E4, 0); W(R, 0x4E8, 0x1FFF1);
    W(R, 0x500, 0); W(R, 0x504, 0);
    W(R, 0x508, bw | (bh << 16) | 0x78007800);

    /* Clear accumulators */
    W(R, 0x13C, 0); W(R, 0x140, 0);
    W(R, 0x150, 0); W(R, 0x154, 0);
    W(R, 0x578, 0); W(R, 0x57C, 0);

    /* Verify critical regs stuck */
    fprintf(out, "AFTER WRITE:\n");
    fprintf(out, "  030=%08x 034=%08x 100=%08x 130=%08x 138=%08x 0F0=%08x\n",
            RD(R, 0x030), RD(R, 0x034), RD(R, 0x100), RD(R, 0x130),
            RD(R, 0x138), RD(R, 0x0F0));
    fflush(out);

    /* ====== END SAFE WINDOW ====== */

    /* Step 7: isp_start — sets offset 0x364=1, enables ISP processing */
    fprintf(out, "[7] isp_start (0x690A)\n"); fflush(out);
    ioctl(isp, 0x690A, (uint32_t[]){0});

    /* Step 8: mode + start ioctls */
    fprintf(out, "[8] mode+start\n"); fflush(out);
    ioctl(isp, 0x4004690F, (uint32_t[]){1});
    ioctl(isp, 0x40016920, (uint8_t[]){1});

    /* Step 9: pkg_enable — triggers first ISP interrupt */
    fprintf(out, "[9] pkg_enable\n"); fflush(out);
    W(R, 0x100, 4);         /* set mode */
    W(R, 0x034, 5);         /* enable processing */
    W(R, 0x100, RD(R, 0x100) | 1);  /* START — first interrupt fires */

    /* Step 10: ISP kick */
    fprintf(out, "[10] kick\n"); fflush(out);
    ioctl(isp, 0x6910, 0);

    /* Check regs right after kick */
    fprintf(out, "POST-KICK:\n");
    fprintf(out, "  030=%08x 034=%08x 100=%08x 130=%08x 0F0=%08x\n",
            RD(R, 0x030), RD(R, 0x034), RD(R, 0x100), RD(R, 0x130),
            RD(R, 0x0F0));
    fflush(out);

    /* Step 11: Sensor init — start LAST */
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
            wb[0] = regs[i][0] >> 8;
            wb[1] = regs[i][0] & 0xFF;
            wb[2] = regs[i][1];
            ioctl(i2c, I2C_RDWR, &x);
        }
        usleep(10000);
        /* Start streaming */
        wb[0] = 0x31; wb[1] = 0x41; wb[2] = 0x01;
        ioctl(i2c, I2C_RDWR, &x);
        close(i2c);
    }
    fprintf(out, "SENSOR STREAMING\n"); fflush(out);

    /* Step 12: Wait and read frames */
    fprintf(out, "[12] Wait 3s for ISP\n"); fflush(out);
    sleep(3);

    /* Check MIPI status */
    volatile uint32_t *MIPI = mmap(NULL, 0x1000, PROT_READ, MAP_SHARED, mem, 0xE0200000);
    if (MIPI && MIPI != (void *)-1) {
        fprintf(out, "MIPI: 000=%08x 004=%08x 008=%08x 010=%08x 020=%08x 080=%08x 090=%08x\n",
                MIPI[0], MIPI[1], MIPI[2], MIPI[0x10/4], MIPI[0x20/4],
                MIPI[0x80/4], MIPI[0x90/4]);
        fflush(out);
        munmap((void *)MIPI, 0x1000);
    }

    /* Check ISP regs after processing */
    fprintf(out, "FINAL ISP:\n");
    fprintf(out, "  010=%08x 030=%08x 034=%08x 0F0=%08x 100=%08x 130=%08x 138=%08x\n",
            RD(R, 0x010), RD(R, 0x030), RD(R, 0x034), RD(R, 0x0F0),
            RD(R, 0x100), RD(R, 0x130), RD(R, 0x138));
    fflush(out);

    /* Try GET_FRAME */
    fprintf(out, "[13] GET_FRAME:\n"); fflush(out);
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

            /* Save raw frame data to file */
            uint32_t phys_addr = fr[1];
            uint32_t frame_size = fr[3];
            fprintf(out, "Frame phys=0x%08x size=%u\n", phys_addr, frame_size);
            if (phys_addr && frame_size && frame_size < 0x800000) {
                volatile uint8_t *fb = mmap(NULL, frame_size, PROT_READ,
                    MAP_SHARED, mem, phys_addr & ~0xFFF);
                if (fb && fb != (void *)-1) {
                    uint32_t page_off = phys_addr & 0xFFF;
                    FILE *raw = fopen("/tmp/frame.raw", "wb");
                    if (raw) {
                        fwrite((void *)(fb + page_off), 1, frame_size, raw);
                        fclose(raw);
                        fprintf(out, "Frame saved to /tmp/frame.raw\n");
                    }
                    munmap((void *)fb, frame_size);
                }
            }
            fflush(out);
            break;
        }
    }

    munmap((void *)R, 0x4000);
    close(mem);

    fprintf(out, "END\n"); fclose(out);
    execl("/bin/busybox", "busybox", "tftp", "-l", "/tmp/r.txt", "-r", "result.txt",
          "-p", "192.168.8.100", (char *)0);
    return 0;
}
