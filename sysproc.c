#include "types.h"
#include "x86.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"

int
sys_fork(void)
{
  return fork();
}

int
sys_exit(void)
{
  exit();
  return 0;  // not reached
}

int
sys_wait(void)
{
  return wait();
}

int
sys_kill(void)
{
  int pid;

  if(argint(0, &pid) < 0)
    return -1;
  return kill(pid);
}

int
sys_getpid(void)
{
  return proc->pid;
}

int
sys_sbrk(void)
{
  int addr;
  int n;

  if(argint(0, &n) < 0)
    return -1;
  addr = proc->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

int
sys_sleep(void)
{
  int n;
  uint ticks0;

  if(argint(0, &n) < 0)
    return -1;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(proc->killed){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

// return how many clock tick interrupts have occurred
// since start.
int
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

int
sys_getcpuid()
{
  return getcpuid();
}

int
sys_chpri(void)
{
  int pid,pr;
  if (argint(0,&pid)<0)
    return -1;
  if (argint(1,&pr)<0)
    return -1;
  return chpri (pid,pr);
}

int
sys_sh_var_read()
{
  return sh_var_for_sem_demo;
}
int sys_sh_var_write()
{
  int n;
  if(argint(0, &n) < 0)
    return -1;
  sh_var_for_sem_demo = n;
  return sh_var_for_sem_demo;
}

int sys_myMalloc(){
    int size;
    if(argint(0, &size) < 0) // 获取参数 size
        return 0;
    void * res = myMalloc(size); // 调用 myMalloc 函数分配slab
    return (int)res; // 返回分配的内存地址
}

int sys_myFree(){
    int va;
    if(argint(0, &va) < 0) // 获取参数 va
        return 0;
    int res = myFree((void*)va); // 调用 myFree 函数释放slab
    return res; // 返回释放结果
}
