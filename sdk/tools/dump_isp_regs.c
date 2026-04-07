#include <stdio.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <stdint.h>
int main(void){
    int fd=open("/dev/mem",2|0x80000);
    if(fd<0)return 1;
    volatile uint32_t*R=mmap(0,0x3240,3,1,fd,0xE8400000);
    if(R==(void*)-1)return 1;
    printf("ISP regs from working camera:\n");
    for(int i=0;i<0x200;i+=4){
        if(i%32==0)printf("\n0x%03x:",i*4);
        printf(" %08x",R[i]);
    }
    printf("\n");
    return 0;
}
