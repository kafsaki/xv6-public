#include "types.h"
#include "stat.h"
#include "user.h"

int main(int argc, char *argv[]) {
    char *a1;
    a1 = (char *)myMalloc(30); // 分配30字节的内存
    printf(1,"a1 virtual addr:%p\n",a1); // 打印分配的内存地址
    a1[0]='A';
    printf(1, "a1[0]:%c\n\n",a1[0]); // 打印分配的内存地址

    char *a2;
    a2 = (char *)myMalloc(31); // 分配31字节的内存
    printf(1,"a2 virtual addr:%p\n",a2); // 打印分配的内存地址
    a2[0]='B';
    printf(1, "a2[0]:%c\n\n",a2[0]); // 打印分配的内存地址

    char *a3;
    a3 = (char *)myMalloc(29); // 分配29字节的内存
    printf(1,"a3 virtual addr:%p\n",a3); // 打印分配的内存地址
    a3[0]='C';
    printf(1, "a3[0]:%c\n\n",a3[0]); // 打印分配的内存地址

    myFree(a2); // 释放a2指向的内存
    printf(1, "free a2 (addr:%p)\n\n",a2); // 打印释放的内存地址

    char *a4;
    a4 = (char *)myMalloc(28); // 分配28字节的内存
    printf(1,"a4 virtual addr:%p\n",a4); // 打印分配的内存地址
    a4[0]='D';
    printf(1, "a4[0]:%c\n\n",a4[0]); // 打印分配的内存地址

    exit();
}
