#include "fcntl.h"
#include "types.h"
#include "uthread.h"
#include "user.h"

volatile int global = 1;

int F(int n) {
  if (n <= 2)
    return n;
  else {
    return F(n - 1) + F(n - 2);
  }
}
void func(void* t_num) {
    int i;
    for(i=0;i<5;i++){
      int num=999999999;
      while(num--);
      global++;
      int n = 5*(int)t_num+i+2+num;
      printf(0,"thread num: %d, global: %d, F(%d)=%d\n",(int)t_num,global,n,F(n));
    }
    exit();
}

int main(int argc, char* argv[]){
    int i;
    int tids[5];
    for(i=0;i<3;i++){
      void* stack = malloc(4096);
      tids[i] = clone(func,(void*)i,stack);
    }
    for(i=0;i<3;i++){
      join((void**)tids[i]);
    }
    exit();
}
