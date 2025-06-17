#include "types.h"
#include "user.h"
#include "fcntl.h"

int main(){
    char buf[128];
    int f = open_fifo("fifo", O_RDWR | O_CREATE);
    int pid = fork();
    f = open("fifo", O_RDWR | O_CREATE);
    if(pid){
        wait();
        read(f, buf, 128);
        printf(1, "pid: %d read: %s\n", getpid(), buf);
    }else{
        write(f, "hello\n", 6);
        printf(1, "pid: %d write: hello\n", getpid());
    }
    exit();
}

