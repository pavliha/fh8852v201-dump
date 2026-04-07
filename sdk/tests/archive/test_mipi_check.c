/* test_mipi_check.c — Ultra-fast: just check if MIPI sees frames.
 * Minimal init, sensor start, read MIPI regs. No sleep. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <stdint.h>
#define I2C_RDWR 0x0707
struct i2c_msg{uint16_t addr;uint16_t flags;uint16_t len;uint8_t*buf;};
struct i2c_rdwr_ioctl_data{struct i2c_msg*msgs;uint32_t nmsgs;};
int main(void){
    FILE*out=fopen("/tmp/etc/config/result.txt","w");
    if(!out)out=stdout;
    setvbuf(out,NULL,_IONBF,0);
    fprintf(out,"=== MIPI CHECK ===\n");

    system("killall ipcam 2>/dev/null");
    usleep(300000);

    const char*c[][2]={
        {"/proc/driver/vpu","vi_1920_1080"},{"/proc/driver/vpu","cap_0_1920_1080"},
        {"/proc/driver/isp","wdr_off"},{"/proc/driver/isp","cir_on"},
        {"/proc/driver/clock","nn_clk,enable,90000000"},{0,0}};
    for(int i=0;c[i][0];i++){int d=open(c[i][0],1);if(d>=0){write(d,c[i][1],strlen(c[i][1]));close(d);}}

    int mp=open("/dev/media_process",2);
    int isp=open("/dev/isp",2);
    fprintf(out,"DEV mp=%d isp=%d\n",mp,isp);

    /* MIPI init */
    ioctl(isp,0x40036954,(uint8_t[]){0,8,1});
    ioctl(isp,0x40066955,(uint8_t[]){0,1,0,0,0,0});
    fprintf(out,"MIPI INIT OK\n");

    int mem=open("/dev/mem",O_RDWR|O_SYNC);

    /* Read MIPI regs BEFORE sensor */
    volatile uint32_t*M=mmap(NULL,0x1000,PROT_READ,MAP_SHARED,mem,0xE0200000);
    if(M&&M!=MAP_FAILED){
        fprintf(out,"PRE: %08x %08x %08x %08x | %08x %08x %08x\n",
                M[0],M[1],M[2],M[3],M[0x80/4],M[0x84/4],M[0x90/4]);
    }

    /* Sensor start */
    int i2c=open("/dev/i2c-0",2);
    if(i2c>=0){
        static const uint16_t r[][2]={
            {0x3300,0x03},{0x3422,0xbf},{0x3401,0x00},{0x3440,0x01},{0x3442,0x00},
            {0x3806,0x00},{0x3908,0x5f},{0x3909,0x00},{0x3929,0x01},{0x3158,0x01},
            {0x3159,0x01},{0x315a,0x01},{0x315b,0x01},{0x35b3,0x15},{0x3148,0x64},
            {0x3031,0x00},{0x3118,0x01},{0x3119,0x06},{0x3670,0x00},{0x3679,0x02},
            {0x3330,0x00},{0x320e,0x02},{0x3804,0x10},{0x35a1,0x06},{0x35a8,0x06},
            {0x35a9,0x06},{0x35aa,0x06},{0x35ab,0x06},{0x35ac,0x06},{0x35ad,0x06},
            {0x35ae,0x07},{0x35af,0x07},{0x333b,0x01},{0x3338,0x08},{0x3339,0x00},
            {0x3144,0x20},{0x3030,0x01},{0x3020,0x8c},{0x3021,0x0a},{0x3024,0xd0},
            {0x3025,0x02},{0x3038,0x06},{0x3039,0x00},{0x303a,0x80},{0x303b,0x07},
            {0x3034,0x04},{0x3035,0x00},{0x3036,0x38},{0x3037,0x04},{0x3908,0x51},
            {0x390a,0x02},{0x3000,0x00},};
        uint8_t wb[3];
        struct i2c_msg m={.addr=0x35,.flags=0,.len=3,.buf=wb};
        struct i2c_rdwr_ioctl_data x={.msgs=&m,.nmsgs=1};
        for(int i=0;i<52;i++){wb[0]=r[i][0]>>8;wb[1]=r[i][0]&0xFF;wb[2]=r[i][1];ioctl(i2c,I2C_RDWR,&x);}
        usleep(10000);
        wb[0]=0x31;wb[1]=0x41;wb[2]=0x01;ioctl(i2c,I2C_RDWR,&x);
        close(i2c);
    }
    fprintf(out,"SENSOR ON\n");

    /* Read MIPI regs 3 times after sensor */
    for(int t=0;t<3;t++){
        usleep(100000);
        if(M&&M!=MAP_FAILED)
            fprintf(out,"T%d: %08x %08x %08x %08x | %08x %08x %08x\n",
                    t,M[0],M[1],M[2],M[3],M[0x80/4],M[0x84/4],M[0x90/4]);
    }

    /* Also check ISP reg 0x004 */
    volatile uint32_t*R=mmap(NULL,0x1000,PROT_READ,MAP_SHARED,mem,0xE8400000);
    if(R&&R!=MAP_FAILED)
        fprintf(out,"ISP: 004=%08x 008=%08x\n",R[1],R[2]);

    if(M&&M!=MAP_FAILED)munmap((void*)M,0x1000);
    if(R&&R!=MAP_FAILED)munmap((void*)R,0x1000);
    close(mem);
    fprintf(out,"END\n");fclose(out);
    return 0;
}
