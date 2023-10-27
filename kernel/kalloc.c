// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct kmem{
  struct spinlock lock;
  struct run *freelist;
};
struct kmem kmems[NCPU];

void
kinit()
{
  for(int i = 0; i < NCPU; i++) {
    //使锁名字以kmem开头
    // char name[10];
    // snprintf(name, sizeof(name), "kmem%d", i);
    //初始化锁
    initlock(&kmems[i].lock, "kmem*");
  }
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  //关闭中断
  push_off();
  //调用cpuid, 为当前运行的freerange的CPU分配空闲内存
  int currentCpuId = cpuid();
  acquire(&kmems[currentCpuId].lock);
  r->next = kmems[currentCpuId].freelist;
  kmems[currentCpuId].freelist = r;
  release(&kmems[currentCpuId].lock);
  //打开中断
  pop_off();
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;
  //打开中断
  push_off();
  int currentCpuId = cpuid();
  acquire(&kmems[currentCpuId].lock);
  r = kmems[currentCpuId].freelist;
  if(r)
    kmems[currentCpuId].freelist = r->next;
  release(&kmems[currentCpuId].lock);
  
  //若当前CPU的链表已满，则窃取其他CPU的链表
  if(!r) {
    for(int i = 0; i < NCPU; i++) {
      if(i == currentCpuId)
        continue;
      acquire(&kmems[i].lock);
      r = kmems[i].freelist;
      if(r) {
        kmems[i].freelist = r->next;
        release(&kmems[i].lock);
        break;
      }
      release(&kmems[i].lock);
    }
  }

  //打开中断
  pop_off();

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}
