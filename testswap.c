#include "types.h"
#include "stat.h"
#include "user.h"

#define PGSIZE 4096

int main(){
    int n = 630;
    char* mem = (char*)malloc(PGSIZE*n);
    if(mem == 0){
        printf(1,"alloc failed\n");
        exit();
    }
    for(int i = 0; i < n*PGSIZE; i++){
        if((i % PGSIZE) == 0)
            printf(1, "write page %d, data: %d\n", i/PGSIZE, i/PGSIZE);
        mem[i] = i/PGSIZE;
    }
    printf(1, "write success\n\n");

    for(int i = 0; i < 5*PGSIZE; i++){
        if((i % PGSIZE) == 0)
            printf(1, "read page %d, data: %d\n", i/PGSIZE, mem[i]);
    }
    exit();
}



