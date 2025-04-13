// Mutual exclusion lock.
struct spinlock {
  uint locked;       // Is the lock held?

  // For debugging:
  char *name;        // Name of lock.
  struct cpu *cpu;   // The cpu holding the lock.
  uint pcs[10];      // The call stack (an array of program counters)
                     // that locked the lock.
};

#define SEM_MAX_NUM 128  // 信号量最大数量
extern int sem_used_count; //当前使用的信号量数量
struct sem{
    struct spinlock lock;
    int resource_count; //资源数量

    int allocated; //是否分配
};
extern struct sem sems[SEM_MAX_NUM]; //信号量数组
