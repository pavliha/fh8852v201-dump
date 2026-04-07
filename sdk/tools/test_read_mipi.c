/* test_read_mipi.c — Just read MIPI and ISP registers via /dev/mem.
 * Run WHILE ipcam is running — no device opens, no ioctls.
 * Shows if MIPI frames are flowing and ISP is processing. */
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <stdint.h>
int main(void){
    FILE*out=fopen("/tmp/etc/config/result.txt","w");
    if(!out)out=stdout;
    int mem=open("/dev/mem",O_RDONLY|O_SYNC);
    if(mem<0){fprintf(out,"NO MEM\n");fclose(out);return 1;}

    /* MIPI wrapper at 0xE0200000 */
    volatile uint32_t*M=mmap(NULL,0x1000,PROT_READ,MAP_SHARED,mem,0xE0200000);
    /* ISP at 0xE8400000 */
    volatile uint32_t*R=mmap(NULL,0x1000,PROT_READ,MAP_SHARED,mem,0xE8400000);
    /* MIPI D-PHY at 0xE0210000 */
    volatile uint32_t*P=mmap(NULL,0x1000,PROT_READ,MAP_SHARED,mem,0xE0210000);

    fprintf(out,"=== LIVE REGS (ipcam running) ===\n");
    if(M&&M!=MAP_FAILED){
        fprintf(out,"MIPI_WRAP:\n");
        for(int i=0;i<16;i++)
            fprintf(out,"  %03x=%08x\n",i*4,M[i]);
        fprintf(out,"  080=%08x 084=%08x 088=%08x 08C=%08x\n",M[0x80/4],M[0x84/4],M[0x88/4],M[0x8C/4]);
        fprintf(out,"  090=%08x 094=%08x 098=%08x\n",M[0x90/4],M[0x94/4],M[0x98/4]);
    }
    if(P&&P!=MAP_FAILED){
        fprintf(out,"MIPI_DPHY:\n");
        for(int i=0;i<8;i++)
            fprintf(out,"  %03x=%08x\n",i*4,P[i]);
    }
    if(R&&R!=MAP_FAILED){
        fprintf(out,"ISP:\n");
        fprintf(out,"  000=%08x 004=%08x 008=%08x 028=%08x\n",R[0],R[1],R[2],R[0x28/4]);
        fprintf(out,"  02C=%08x 030=%08x 034=%08x\n",R[0x2C/4],R[0x30/4],R[0x34/4]);
        fprintf(out,"  0F0=%08x 0F4=%08x 100=%08x\n",R[0xF0/4],R[0xF4/4],R[0x100/4]);
        fprintf(out,"  130=%08x 138=%08x 13C=%08x 140=%08x\n",R[0x130/4],R[0x138/4],R[0x13C/4],R[0x140/4]);
        /* Read again after 200ms to check if counters change */
        usleep(200000);
        fprintf(out,"  (200ms later)\n");
        fprintf(out,"  004=%08x 13C=%08x 140=%08x\n",R[1],R[0x13C/4],R[0x140/4]);
    }

    /* Check /proc/interrupts */
    FILE*p=fopen("/proc/interrupts","r");
    if(p){char l[256];while(fgets(l,sizeof(l),p))
        if(strstr(l,"ispp")||strstr(l,"veu")||strstr(l,"mipi")||strstr(l,"jpg"))
            fprintf(out,"IRQ: %s",l);
    fclose(p);}

    if(M&&M!=MAP_FAILED)munmap((void*)M,0x1000);
    if(R&&R!=MAP_FAILED)munmap((void*)R,0x1000);
    if(P&&P!=MAP_FAILED)munmap((void*)P,0x1000);
    close(mem);
    fprintf(out,"END\n");fclose(out);
    return 0;
}
