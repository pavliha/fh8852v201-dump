#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
int main(void){
    printf("BEFORE open:\n");fflush(stdout);
    system("cat /proc/interrupts | grep -E 'ispp|veu|mipi|NNA|CPU'");
    printf("\nOpening devices...\n");fflush(stdout);
    int mp=open("/dev/media_process",2);
    int isp=open("/dev/isp",2);
    printf("mp=%d isp=%d\n",mp,isp);fflush(stdout);
    printf("\nAFTER open:\n");fflush(stdout);
    system("cat /proc/interrupts | grep -E 'ispp|veu|mipi|NNA|CPU'");
    printf("\nDone\n");
    return 0;
}
