#include "param.h"
#include "types.h"
#include "defs.h"
#include "x86.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "elf.h"

extern char data[];  // defined by kernel.ld
pde_t *kpgdir;  // for use in scheduler()

// Set up CPU's kernel segment descriptors.
// Run once on entry on each CPU.
void
seginit(void)
{
  struct cpu *c;

  // Map "logical" addresses to virtual addresses using identity map.
  // Cannot share a CODE descriptor for both kernel and user
  // because it would have to have DPL_USR, but the CPU forbids
  // an interrupt from CPL=0 to DPL=3.
  c = &cpus[cpunum()];
  c->gdt[SEG_KCODE] = SEG(STA_X|STA_R, 0, 0xffffffff, 0);
  c->gdt[SEG_KDATA] = SEG(STA_W, 0, 0xffffffff, 0);
  c->gdt[SEG_UCODE] = SEG(STA_X|STA_R, 0, 0xffffffff, DPL_USER);
  c->gdt[SEG_UDATA] = SEG(STA_W, 0, 0xffffffff, DPL_USER);

  // Map cpu and proc -- these are private per cpu.
  c->gdt[SEG_KCPU] = SEG(STA_W, &c->cpu, 8, 0);

  lgdt(c->gdt, sizeof(c->gdt));
  loadgs(SEG_KCPU << 3);

  // Initialize cpu-local storage.
  cpu = c;
  proc = 0;
}

// Return the address of the PTE in page table pgdir
// that corresponds to virtual address va.  If alloc!=0,
// create any required page table pages.
static pte_t *
walkpgdir(pde_t *pgdir, const void *va, int alloc)
{
  pde_t *pde;
  pte_t *pgtab;

  pde = &pgdir[PDX(va)];
  if(*pde & PTE_P){
    pgtab = (pte_t*)P2V(PTE_ADDR(*pde));
  } else {
    if(!alloc || (pgtab = (pte_t*)kalloc()) == 0)
      return 0;
    // Make sure all those PTE_P bits are zero.
    memset(pgtab, 0, PGSIZE);
    // The permissions here are overly generous, but they can
    // be further restricted by the permissions in the page table
    // entries, if necessary.
    *pde = V2P(pgtab) | PTE_P | PTE_W | PTE_U;
  }
  return &pgtab[PTX(va)];
}

// Create PTEs for virtual addresses starting at va that refer to
// physical addresses starting at pa. va and size might not
// be page-aligned.
static int
mappages(pde_t *pgdir, void *va, uint size, uint pa, int perm)
{
  char *a, *last;
  pte_t *pte;

  a = (char*)PGROUNDDOWN((uint)va);
  last = (char*)PGROUNDDOWN(((uint)va) + size - 1);
  for(;;){
    if((pte = walkpgdir(pgdir, a, 1)) == 0)
      return -1;
    //if(*pte & PTE_P)
      //panic("remap");
    *pte = pa | perm | PTE_P;
    if(a == last)
      break;
    a += PGSIZE;
    pa += PGSIZE;
  }
  return 0;
}

// There is one page table per process, plus one that's used when
// a CPU is not running any process (kpgdir). The kernel uses the
// current process's page table during system calls and interrupts;
// page protection bits prevent user code from using the kernel's
// mappings.
//
// setupkvm() and exec() set up every page table like this:
//
//   0..KERNBASE: user memory (text+data+stack+heap), mapped to
//                phys memory allocated by the kernel
//   KERNBASE..KERNBASE+EXTMEM: mapped to 0..EXTMEM (for I/O space)
//   KERNBASE+EXTMEM..data: mapped to EXTMEM..V2P(data)
//                for the kernel's instructions and r/o data
//   data..KERNBASE+PHYSTOP: mapped to V2P(data)..PHYSTOP,
//                                  rw data + free physical memory
//   0xfe000000..0: mapped direct (devices such as ioapic)
//
// The kernel allocates physical memory for its heap and for user memory
// between V2P(end) and the end of physical memory (PHYSTOP)
// (directly addressable from end..P2V(PHYSTOP)).

// This table defines the kernel's mappings, which are present in
// every process's page table.
static struct kmap {
  void *virt;
  uint phys_start;
  uint phys_end;
  int perm;
} kmap[] = {
 { (void*)KERNBASE, 0,             EXTMEM,    PTE_W}, // I/O space
 { (void*)KERNLINK, V2P(KERNLINK), V2P(data), 0},     // kern text+rodata
 { (void*)data,     V2P(data),     PHYSTOP,   PTE_W}, // kern data+memory
 { (void*)DEVSPACE, DEVSPACE,      0,         PTE_W}, // more devices
};

// Set up kernel part of a page table.
pde_t*
setupkvm(void)
{
  pde_t *pgdir;
  struct kmap *k;

  if((pgdir = (pde_t*)kalloc()) == 0)
    return 0;
  memset(pgdir, 0, PGSIZE);
  if (P2V(PHYSTOP) > (void*)DEVSPACE)
    panic("PHYSTOP too high");
  for(k = kmap; k < &kmap[NELEM(kmap)]; k++)
    if(mappages(pgdir, k->virt, k->phys_end - k->phys_start,
                (uint)k->phys_start, k->perm) < 0)
      return 0;
  return pgdir;
}

// Allocate one page table for the machine for the kernel address
// space for scheduler processes.
void
kvmalloc(void)
{
  kpgdir = setupkvm();
  switchkvm();
}

// Switch h/w page table register to the kernel-only page table,
// for when no process is running.
void
switchkvm(void)
{
  lcr3(V2P(kpgdir));   // switch to the kernel page table
}

// Switch TSS and h/w page table to correspond to process p.
void
switchuvm(struct proc *p)
{
  pushcli();
  cpu->gdt[SEG_TSS] = SEG16(STS_T32A, &cpu->ts, sizeof(cpu->ts)-1, 0);
  cpu->gdt[SEG_TSS].s = 0;
  cpu->ts.ss0 = SEG_KDATA << 3;
  cpu->ts.esp0 = (uint)proc->kstack + KSTACKSIZE;
  // setting IOPL=0 in eflags *and* iomb beyond the tss segment limit
  // forbids I/O instructions (e.g., inb and outb) from user space
  cpu->ts.iomb = (ushort) 0xFFFF;
  ltr(SEG_TSS << 3);
  if(p->pgdir == 0)
    panic("switchuvm: no pgdir");
  lcr3(V2P(p->pgdir));  // switch to process's address space
  popcli();
}

// Load the initcode into address 0 of pgdir.
// sz must be less than a page.
void
inituvm(pde_t *pgdir, char *init, uint sz)
{
  char *mem;

  if(sz >= PGSIZE)
    panic("inituvm: more than a page");
  mem = kalloc();
  memset(mem, 0, PGSIZE);
  mappages(pgdir, 0, PGSIZE, V2P(mem), PTE_W|PTE_U);
  memmove(mem, init, sz);
}

// Load a program segment into pgdir.  addr must be page-aligned
// and the pages from addr to addr+sz must already be mapped.
int
loaduvm(pde_t *pgdir, char *addr, struct inode *ip, uint offset, uint sz)
{
  uint i, pa, n;
  pte_t *pte;

  if((uint) addr % PGSIZE != 0)
    panic("loaduvm: addr must be page aligned");
  for(i = 0; i < sz; i += PGSIZE){
    if((pte = walkpgdir(pgdir, addr+i, 0)) == 0)
      panic("loaduvm: address should exist");
    pa = PTE_ADDR(*pte);
    if(sz - i < PGSIZE)
      n = sz - i;
    else
      n = PGSIZE;
    if(readi(ip, P2V(pa), offset+i, n) != n)
      return -1;
  }
  return 0;
}

// Allocate page tables and physical memory to grow process from oldsz to
// newsz, which need not be page aligned.  Returns new size or 0 on error.
int
allocuvm(pde_t *pgdir, uint oldsz, uint newsz)
{
  char *mem;
  uint a;

  if(newsz >= KERNBASE)
    return 0;
  if(newsz < oldsz)
    return oldsz;

  a = PGROUNDUP(oldsz);
  for(; a < newsz; a += PGSIZE){
    mem = kalloc();
    if(mem == 0){
      cprintf("allocuvm out of memory\n");
      //deallocuvm(pgdir, newsz, oldsz);
      //return 0;
      break;
    }
    memset(mem, 0, PGSIZE);
    if(mappages(pgdir, (char*)a, PGSIZE, V2P(mem), PTE_W|PTE_U) < 0){
      cprintf("allocuvm out of memory (2)\n");
      deallocuvm(pgdir, newsz, oldsz);
      kfree(mem);
      return 0;
    }
  }
  return newsz;
}

// Deallocate user pages to bring the process size from oldsz to
// newsz.  oldsz and newsz need not be page-aligned, nor does newsz
// need to be less than oldsz.  oldsz can be larger than the actual
// process size.  Returns the new process size.
int
deallocuvm(pde_t *pgdir, uint oldsz, uint newsz)
{
  pte_t *pte;
  uint a, pa;

  if(newsz >= oldsz)
    return oldsz;

  a = PGROUNDUP(newsz);
  for(; a  < oldsz; a += PGSIZE){
    pte = walkpgdir(pgdir, (char*)a, 0);
    if(!pte)
      a += (NPTENTRIES - 1) * PGSIZE;
    else if((*pte & PTE_P) != 0){
      pa = PTE_ADDR(*pte);
      if(pa == 0)
        panic("kfree");
      char *v = P2V(pa);
      if((flags & PTE_SWAPPED) == 0)
	kfree(v);
      //kfree(v);
      *pte = 0;
    }
  }
  return newsz;
}

// Free a page table and all the physical memory pages
// in the user part.
void
freevm(pde_t *pgdir)
{
  uint i;

  if(pgdir == 0)
    panic("freevm: no pgdir");
  deallocuvm(pgdir, KERNBASE, 0);
  for(i = 0; i < NPDENTRIES; i++){
    if(pgdir[i] & PTE_P){
      char * v = P2V(PTE_ADDR(pgdir[i]));
      kfree(v);
    }
  }
  kfree((char*)pgdir);
}

// Clear PTE_U on a page. Used to create an inaccessible
// page beneath the user stack.
void
clearpteu(pde_t *pgdir, char *uva)
{
  pte_t *pte;

  pte = walkpgdir(pgdir, uva, 0);
  if(pte == 0)
    panic("clearpteu");
  *pte &= ~PTE_U;
}

// Given a parent process's page table, create a copy
// of it for a child.
pde_t*
copyuvm(pde_t *pgdir, uint sz)
{
  pde_t *d;
  pte_t *pte;
  uint pa, i, flags;
  char *mem;

  if((d = setupkvm()) == 0)
    return 0;
  for(i = 0; i < sz; i += PGSIZE){
    if((pte = walkpgdir(pgdir, (void *) i, 0)) == 0)
      panic("copyuvm: pte should exist");
    if(!(*pte & PTE_P))
      panic("copyuvm: page not present");
    pa = PTE_ADDR(*pte);
    flags = PTE_FLAGS(*pte);
    if((mem = kalloc()) == 0)
      goto bad;
    memmove(mem, (char*)P2V(pa), PGSIZE);
    if(mappages(d, (void*)i, PGSIZE, V2P(mem), flags) < 0)
      goto bad;
  }
  return d;

bad:
  freevm(d);
  return 0;
}

pde_t*
copyuvm_onwrite(pde_t *pgdir, uint sz)
{
  pde_t *d;
  pte_t *pte;
  uint pa, i, flags;
  char *mem;

  if((d = setupkvm()) == 0)
    return 0;
  for(i = 0; i < sz; i += PGSIZE){
    if((pte = walkpgdir(pgdir, (void *) i, 0)) == 0)
      panic("copyuvm: pte should exist");
    if(!(*pte & PTE_P))
      panic("copyuvm: page not present");
    if(i>=3*PGSIZE){
      *pte = ((*pte) & (~PTE_W));
    }
    pa = PTE_ADDR(*pte);
    flags = PTE_FLAGS(*pte);
    if(i < 3*PGSIZE){//如果是前三页，直接复制
      if((mem = kalloc()) == 0)
      goto bad;
      memmove(mem, (char*)P2V(pa), PGSIZE);
      if(mappages(d, (void*)i, PGSIZE, V2P(mem), flags) < 0)
        goto bad;
    }
    else{//不是前三页，将子进程页表指向父进程页表
        mappages(d, (void*)i, PGSIZE, pa, flags);
        cprintf("lazy %p\n",P2V(pa));
        pageref_set(pa,1);//引用此时设置为1
    }
  }
  return d;

bad:
  freevm(d);
  return 0;
}

void copy_on_write(pde_t *pgdir, void *va)
{
    pte_t *pte = walkpgdir(pgdir, va, 0);
    uint pa = PTE_ADDR(*pte);
    uint ref = pageref_get(pa);
    if (ref > 1){// 引用次数大于1
        pageref_set(pa, -1); // ref计数减1
        char *mem = kalloc();
        memmove(mem, (char *)P2V(pa), PGSIZE);
        mappages(pgdir, va, PGSIZE, V2P(mem), PTE_W | PTE_U);
        cprintf("[copy on write]: %p -> %p\n", P2V(pa), mem);
    }
    else{// 引用次数等于1
        *pte = (*pte) | PTE_W; // 去掉写禁止
        cprintf("ref=1 remove write forbidden\n");
    }
}


//PAGEBREAK!
// Map user virtual address to kernel address.
char*
uva2ka(pde_t *pgdir, char *uva)
{
  pte_t *pte;

  pte = walkpgdir(pgdir, uva, 0);
  if((*pte & PTE_P) == 0)
    return 0;
  if((*pte & PTE_U) == 0)
    return 0;
  return (char*)P2V(PTE_ADDR(*pte));
}

// Copy len bytes from p to user address va in page table pgdir.
// Most useful when pgdir is not the current page table.
// uva2ka ensures this only works for PTE_U pages.
int
copyout(pde_t *pgdir, uint va, void *p, uint len)
{
  char *buf, *pa0;
  uint n, va0;

  buf = (char*)p;
  while(len > 0){
    va0 = (uint)PGROUNDDOWN(va);
    pa0 = uva2ka(pgdir, (char*)va0);
    if(pa0 == 0)
      return -1;
    n = PGSIZE - (va - va0);
    if(n > len)
      n = len;
    memmove(pa0 + (va - va0), buf, n);
    len -= n;
    buf += n;
    va = va0 + PGSIZE;
  }
  return 0;
}

//PAGEBREAK!
// Blank page.
//PAGEBREAK!
// Blank page.
//PAGEBREAK!
// Blank page.

//slab
typedef struct slab{
    int slab_size; //size of slab
    int  num;  //number of slab
    char used_mask[256]; //slab status
    void *phy_addr;  //physical address
  }slab;
  
  slab slabs[8];
  
void slab_init(){
    int size,i;
    for(size=16,i=0; size<=2048; size*=2,i++){
      slabs[i].slab_size = size;//slab size
      slabs[i].num = 4096/size;//number of slab
      memset(slabs[i].used_mask, 0, 256);//slab status
      slabs[i].phy_addr = kalloc();//physical address
      cprintf("slab size: %d, number of slab: %d, physical address: %x\n",slabs[i].slab_size,slabs[i].num,slabs[i].phy_addr);
    }
}
  
int slab_alloc(pde_t *pgdir, void *va, uint sz){
    if(sz>2048 || sz<=0)//如果sz大于2048或者小于等于0，返回0
      return 0;
    int size=16,i=0;
    while(size<sz)//找到第一个大于sz的slab
      size*=2,i++;
    int j;
    for(j=0; j<slabs[i].num; j++){
      if(slabs[i].used_mask[j]==0){//找到空闲的slab
        slabs[i].used_mask[j]=1;//标志为已分配
        break;
      }
      if(j==slabs[i].num-1)//如果没有空闲的slab
        return 0;
    }
    uint pa = (uint)slabs[i].phy_addr + j*slabs[i].slab_size;//计算物理地址
    cprintf("assign from physical address: %p\n",pa);
    mappages(pgdir, va, 4096, V2P(pa), PTE_W|PTE_U);//映射虚拟地址到物理地址
    return j*slabs[i].slab_size;//返回物理页的地址偏移量
}

int slab_free(pde_t *pgdir, void *va){
    uint page_addr = (uint)uva2ka(pgdir, va);//获取起始物理地址
    uint page_offset = (uint)va&4095;//获取物理页的地址偏移量
    uint pa = page_addr + page_offset;//计算物理地址
    cprintf("free from physical address: %p\n",pa);
    int i;
    for(i=0;i<8;i++){//遍历slabs
        uint start=(uint)slabs[i].phy_addr;//计算slab的起始物理地址
        uint end=start+(uint)slabs[i].slab_size*slabs[i].num;//计算slab的结束物理地址
        if(pa>=start && pa<end){//如果物理地址在slab内
            break;//退出循环
        }
    }
    if(i==8)//如果没有找到slab
        return 0;
    uint offset = pa - (uint)slabs[i].phy_addr;//计算物理地址在slab内的偏移量
    int j = offset / slabs[i].slab_size;//计算在对应大小slab中的索引
    slabs[i].used_mask[j] = 0;//标志为未分配
    pte_t *pte = walkpgdir(pgdir, va, 0);//获取页表项
    *pte = (uint)0;//清除页表项
    return 1;//返回1表示释放成功
}

uint 
swapout(pde_t *pgdir, uint swap_start, uint sz) { // 换出一个物理页帧
    pte_t *pte;
    uint a = swap_start;// 起始地址
    a = PGROUNDDOWN(a); // 向下取整
    
    for(; a < sz; a += PGSIZE) {
      pte = walkpgdir(pgdir, (char*)a, 0);
      if((*pte  & PTE_P) && ((*pte & PTE_SWAPPED) == 0 )) { // 找到一个映射页,并且没有被换出
        uint pa = PTE_ADDR(*pte)); 

        begin_op();
        uint blockno = balloc8(1); // 申请 8 个盘块
        end_op();

        write_page_to_disk(1, (char*)P2V(pa), blockno); // 写入磁盘

        *pte = (blockno << 12); // 记录盘块号
        *pte = (*pte) | PTE_SWAPPED | PTE_P;//将其swapped位置1
        cprintf("[swap out] va: %p --> block: %d, get free page pa: %p\n", a, blockno, P2V(pa));

        return pa;
      }
    }
    return 0;
 }

void
swapin(char* pa, uint blockno){
  read_page_from_disk(1, P2V(pa), blockno); 
  bfree8(1, blockno);
}

void
pagefault(pde_t *pgdir, void *va, uint swap_start, uint sz)
{
  va = (char*)PGROUNDDOWN((uint)va);
  pte_t* pte = walkpgdir(proc->pgdir, va, 0);
  uint flags = PTE_FLAGS(*pte);

  char* mem = kalloc(); // 分配一块物理页帧
  if(mem == 0) mem = (char*)swapout(pgdir, swap_start, sz);
  if(mem == 0) panic("can not find swap page!\n");

  if(flags & PTE_SWAPPED){
    uint blockno = (*pte) >> 12;
    swapin(mem, blockno);
    *pte = (*pte) & (~PTE_SWAPPED);
    cprintf("[swap in] block: %d --> va: %p, free block: %d\n", blockno, va, blockno);
  }
  // 最后建立映射
  mappages(proc->pgdir, va, PGSIZE, (uint)mem,  PTE_W | PTE_U);
  cprintf("[pg fault] map va: %p to pa: %p\n",va, V2P(mem));
}
