/**
 * Complete pipeline test with clean-room CV2003 sensor driver.
 * Combines: /proc config → device opens → VMM alloc → VPSS init
 *          → sensor vtable registration → ISP init → GET_FRAME
 */
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <stdint.h>
#include <errno.h>

/* From cv2003_driver.c */
extern void cv2003_build_vtable(uint32_t vtable[31]);
extern int cv2003_init(void);
extern void cv2003_deinit(void);

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
#define ISP_SENSOR_REG     0x6906  /* Guess: sensor register callback */
#define VMM_ALLOC          0xC0506D0A

static int vmm_alloc(int vfd, uint32_t size, const char *name, uint32_t *phys) {
    static uint8_t p[80]; /* MUST be static — stack corruption otherwise */
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
    printf("=== COMPLETE PIPELINE + SENSOR DRIVER TEST ===\n\n");

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
    int enc = open("/dev/enc", O_RDWR);
    int jpg = open("/dev/jpeg", O_RDWR);
    int vmm = open("/dev/vmm_userdev", O_RDWR);
    printf("[2] mp=%d isp=%d enc=%d jpg=%d vmm=%d\n", mp, isp, enc, jpg, vmm);
    fflush(stdout);

    /* 3. SysInitMem */
    printf("[3] SysInit\n"); fflush(stdout);
    uint32_t sz=0, phys=0;
    ioctl(isp, VPSS_SYS_QUERY, &sz);
    vmm_alloc(vmm, sz, "vpu-sys", &phys);
    uint32_t sm[3]={phys,0,sz};
    ret=ioctl(isp, VPSS_SYS_SET_MEM, sm);
    printf("    %u bytes phys=0x%08x set=%d\n", sz, phys, ret); fflush(stdout);

    /* 4. ChnInitMem CH0 */
    printf("[4] CH0\n"); fflush(stdout);
    uint32_t q[4]={0,1920,1080,0};
    ioctl(isp, VPSS_CHN_QUERY, q);
    vmm_alloc(vmm, q[3], "vpu-ch-0", &phys);
    uint32_t cs[6]={0,phys,0,q[3],1920,1080};
    ret=ioctl(isp, VPSS_CHN_SET_MEM, cs);
    printf("    %u bytes phys=0x%08x set=%d\n", q[3], phys, ret); fflush(stdout);

    /* 5. VPSS pipeline */
    printf("[5] VPSS\n"); fflush(stdout);
    uint32_t vi[62]; memset(vi,0,sizeof(vi)); vi[0]=1920; vi[1]=1080;
    printf("    VI=%d ", ioctl(isp, VPSS_SET_VI_ATTR, vi)); fflush(stdout);
    uint32_t en=0;
    printf("EN=%d ", ioctl(isp, VPSS_ENABLE, &en)); fflush(stdout);
    uint32_t ca[3]={0,1920,1080};
    printf("CA=%d ", ioctl(isp, VPSS_SET_CHN_ATTR, ca)); fflush(stdout);
    uint32_t ch=0;
    printf("OC=%d ", ioctl(isp, VPSS_OPEN_CHN, &ch)); fflush(stdout);
    uint32_t vo[2]={0,1};
    printf("VM=%d\n", ioctl(isp, VPSS_SET_VO_MODE, vo)); fflush(stdout);

    /* 6. Sensor driver */
    printf("[6] Sensor driver\n"); fflush(stdout);
    ret = cv2003_init();
    printf("    init: %d\n", ret); fflush(stdout);

    uint32_t vtable[31];
    cv2003_build_vtable(vtable);
    printf("    vtable: name='%s' funcs=%d\n",
           (char*)(uintptr_t)vtable[0],
           31 - (vtable[1]==0)-(vtable[2]==0)-(vtable[3]==0)); fflush(stdout);

    /* Try to register vtable with ISP via ioctl.
     * From decompilation: isp_core_sensor_install does memcpy
     * to offset 0x162C in ISP struct. The ISP struct is in userspace
     * (mmap'd). Without knowing the struct base address, we can't
     * memcpy directly. But the ISP might accept a "register sensor"
     * ioctl. Let's try a few candidates. */

    /* Try writing vtable via known ISP ioctl patterns */
    /* The ISP uses ioctl type 0x69 */
    uint32_t sensor_reg_param[32];
    sensor_reg_param[0] = 0; /* sensor index */
    memcpy(&sensor_reg_param[1], vtable, 124);

    /* Try various ioctl codes for sensor registration */
    uint32_t try_ioctls[] = {
        0xC0806900,  /* type 'i', nr 0, size 128 */
        0xC07C6901,  /* type 'i', nr 1, size 124 */
        0xC07C6902,  /* nr 2 */
        0xC0806903,  /* nr 3 */
        0xC07C6906,  /* nr 6 */
        0xC07C690B,  /* nr 11 */
        0xC07C6950,  /* nr 0x50 */
    };
    for (int i = 0; i < 7; i++) {
        ret = ioctl(isp, try_ioctls[i], sensor_reg_param);
        if (ret == 0) {
            printf("    SENSOR REG ioctl 0x%08x: ret=0 SUCCESS!\n", try_ioctls[i]);
            fflush(stdout);
            break;
        }
    }

    /* Init sensor via I2C regardless */
    extern int cv2003_set_format(int);
    cv2003_set_format(0x203);
    printf("    sensor streaming\n"); fflush(stdout);

    /* 7. ISP init */
    printf("[7] ISP\n"); fflush(stdout);
    uint32_t ip[4]={0};
    ret=ioctl(isp, ISP_INIT, ip);
    printf("    INIT=%d ", ret); fflush(stdout);
    ret=ioctl(isp, ISP_KICK, 0);
    printf("KICK=%d\n", ret); fflush(stdout);

    /* 8. Get frame — try multiple times */
    printf("[8] Frame\n"); fflush(stdout);
    for (int attempt = 0; attempt < 10; attempt++) {
        usleep(200000); /* 200ms */
        uint32_t f[26]; memset(f,0,sizeof(f)); f[0]=0;
        ret = ioctl(isp, VPSS_GET_CHN_FRAME, f);
        printf("    %d: 0x%08x", attempt, (uint32_t)ret); fflush(stdout);
        if (ret == 0) {
            printf(" *** GOT FRAME! ***\n");
            for (int i = 0; i < 12; i++)
                printf("      [%2d] 0x%08x (%u)\n", i, f[i], f[i]);
            fflush(stdout);

            /* Save raw frame data if we got physical address */
            if (f[2] > 0xA0000000 && f[6] > 0) {
                printf("    Frame: phys=0x%08x size=%u\n", f[2], f[6]);
            }
            break;
        }
        printf("\n"); fflush(stdout);
    }

    /* 9. Status */
    printf("\n[9]\n"); fflush(stdout);
    system("cat /proc/driver/isp 2>/dev/null | grep -E 'FPS|Resol|PIC_START Count|OVERFLOW'");
    system("cat /proc/driver/vmm 2>/dev/null | grep total");
    system("cat /proc/driver/media_process 2>/dev/null | grep CHN");

    cv2003_deinit();
    printf("\n=== DONE ===\n");
    return 0;
}
