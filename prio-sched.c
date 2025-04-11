#include "types.h"
#include "stat.h"
#include "user.h"

int main(int argc, char *argv[]){
  int pid = getpid();
  printf(1,"This is a demo for priority shcedule!\n");
  chpri(pid, 19); //set prio to 19
  
  int i=0;
  pid = fork();
  if(pid == 0){//child
    chpri(getpid(), 5);//set child prio to 5
    i =1;
    while(i<10000){
      if(i%1000 == 0){
        printf(1,"c1 ");
      }
      i++;
    }
    printf(1,"child sleeping\n");
    sleep(10);
    i=1;
    while(i<=1000000){
        if(i%100000 == 0){
            printf(1,"c2 ");
        }
        i++;
    }
    printf(1,"child finished\n");
  }else{//parent
    sleep(1);//wait for child creat
    i=1;
    while(i>0){
        if(i%100000 == 0){
            printf(1,"p ");
        }
        i++;
        if(i>20000000)
            break;
    }
  }
  exit();

}

