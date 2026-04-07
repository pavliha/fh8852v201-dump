#include <stdio.h>
#include <signal.h>
#include <unistd.h>
#include <stdlib.h>
int main(void){
    signal(SIGHUP,SIG_IGN);
    signal(SIGPIPE,SIG_IGN);
    setsid();
    FILE*f=fopen("/tmp/r.txt","w");
    fprintf(f,"HELLO FROM TEST pid=%d\n",getpid());
    fclose(f);
    system("busybox tftp -l /tmp/r.txt -r result.txt -p 192.168.8.100");
    return 0;
}
