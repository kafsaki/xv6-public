#include "types.h"
#include "stat.h"
#include "user.h"

int main(int argc, char *argv[])
{
    char *buffer = malloc(4096 * 8);
    buffer[0] = 'A';
    int pid = myFork();
    if (pid)
    { // 父进程
        printf(1, "parent pid:%d,data:%c\n", getpid(), buffer[0]);
        wait();
        printf(1, "parent pid:%d,data:%c\n", getpid(), buffer[0]);
    }
    else
    { // 子进程
        buffer[0] = 'B';
        printf(1, "child pid:%d,data:%c\n", getpid(), buffer[0]);
    }
    exit();
}

