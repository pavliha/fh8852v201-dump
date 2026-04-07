/* Try mmap ISP regs via /dev/isp fd instead of /dev/mem.
 * The kernel module may have its own mmap handler that
 * provides safe access to ISP hardware registers. */
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <stdint.h>
#include <errno.h>

int main(void){
    FILE*out=fopen("/tmp/r.txt","w");
    if(!out)out=stdout;

    int isp=open("/dev/isp",O_RDWR);
    fprintf(out,"isp=%d\n",isp);fflush(out);

    /* Try mmap at various offsets on /dev/isp */
    /* The vendor SDK does: isp_mmap(0xE8400000, &ptr, 0x3240)
     * which calls mmap(NULL, 0x3240, PROT_READ|PROT_WRITE, MAP_SHARED, isp_fd, 0xE8400000) */

    volatile uint32_t *r;

    /* Try offset 0xE8400000 */
    r = mmap(NULL, 0x4000, PROT_READ|PROT_WRITE, MAP_SHARED, isp, 0xE8400000);
    fprintf(out,"mmap(isp,0xE8400000)=%p errno=%d\n",(void*)r,r==MAP_FAILED?errno:0);fflush(out);
    if(r!=MAP_FAILED){
        fprintf(out,"  reg[0]=0x%08x (ISP ID)\n",r[0]);fflush(out);
        munmap((void*)r,0x4000);
    }

    /* Try offset 0 */
    r = mmap(NULL, 0x4000, PROT_READ|PROT_WRITE, MAP_SHARED, isp, 0);
    fprintf(out,"mmap(isp,0)=%p errno=%d\n",(void*)r,r==MAP_FAILED?errno:0);fflush(out);
    if(r!=MAP_FAILED){
        fprintf(out,"  [0]=0x%08x [1]=0x%08x [2]=0x%08x\n",r[0],r[1],r[2]);fflush(out);
        munmap((void*)r,0x4000);
    }

    /* Try offset 0x1000 (page-aligned) */
    r = mmap(NULL, 0x4000, PROT_READ|PROT_WRITE, MAP_SHARED, isp, 0x1000);
    fprintf(out,"mmap(isp,0x1000)=%p errno=%d\n",(void*)r,r==MAP_FAILED?errno:0);fflush(out);
    if(r!=MAP_FAILED){
        fprintf(out,"  [0]=0x%08x\n",r[0]);fflush(out);
        munmap((void*)r,0x4000);
    }

    /* Try on /dev/media_process */
    int mp=open("/dev/media_process",O_RDWR);
    r = mmap(NULL, 0x4000, PROT_READ|PROT_WRITE, MAP_SHARED, mp, 0xE8400000);
    fprintf(out,"mmap(mp,0xE8400000)=%p errno=%d\n",(void*)r,r==MAP_FAILED?errno:0);fflush(out);
    if(r!=MAP_FAILED){
        fprintf(out,"  reg[0]=0x%08x\n",r[0]);fflush(out);
        munmap((void*)r,0x4000);
    }

    r = mmap(NULL, 0x4000, PROT_READ|PROT_WRITE, MAP_SHARED, mp, 0);
    fprintf(out,"mmap(mp,0)=%p errno=%d\n",(void*)r,r==MAP_FAILED?errno:0);fflush(out);
    if(r!=MAP_FAILED){
        fprintf(out,"  [0]=0x%08x [1]=0x%08x\n",r[0],r[1]);fflush(out);
        munmap((void*)r,0x4000);
    }

    fprintf(out,"END\n");fclose(out);
    return 0;
}
