#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "x86.h"
#include "traps.h"
#include "spinlock.h"
#include "elf.h"

int mappages(pde_t *pgdir, void *va, uint size, uint pa, int perm);

// Interrupt descriptor table (shared by all CPUs).
struct gatedesc idt[256];
extern uint vectors[];  // in vectors.S: array of 256 entry pointers
struct spinlock tickslock;
uint ticks;
void page_fault_handler(void);

void
tvinit(void)
{
  int i;

  for(i = 0; i < 256; i++)
    SETGATE(idt[i], 0, SEG_KCODE<<3, vectors[i], 0);
  SETGATE(idt[T_SYSCALL], 1, SEG_KCODE<<3, vectors[T_SYSCALL], DPL_USER);
  
  initlock(&tickslock, "time");
}

void
idtinit(void)
{
  lidt(idt, sizeof(idt));
}

//PAGEBREAK: 41
void
trap(struct trapframe *tf)
{
  
  if(tf->trapno == T_SYSCALL){
    if(proc->killed)
      exit();
    proc->tf = tf;
    syscall();
    if(proc->killed)
      exit();
    return;
  }

  switch(tf->trapno){
  case T_IRQ0 + IRQ_TIMER:
    if(cpu->id == 0){
      acquire(&tickslock);
      ticks++;
      wakeup(&ticks);
      release(&tickslock);
    }
    lapiceoi();
    break;
  case T_IRQ0 + IRQ_IDE:
    ideintr();
    lapiceoi();
    break;
  case T_IRQ0 + IRQ_IDE+1:
    // Bochs generates spurious IDE1 interrupts.
    break;
  case T_IRQ0 + IRQ_KBD:
    kbdintr();
    lapiceoi();
    break;
  case T_IRQ0 + IRQ_COM1:
    uartintr();
    lapiceoi();
    break;
  case T_IRQ0 + 7:
  case T_IRQ0 + IRQ_SPURIOUS:
    cprintf("cpu%d: spurious interrupt at %x:%x\n",
            cpu->id, tf->cs, tf->eip);
    lapiceoi();
    break;
  
  // for lazy page allocation
  case T_PGFLT:
    page_fault_handler();
    break;

    /*
     Currently this Page Fault handler only deals with lazy page allocation of heap
     memory. How do we bring in the code-data we need ? think of readi. And how do we
     distiguish the two? 


    */



  //PAGEBREAK: 13
  default:
    if(proc == 0 || (tf->cs&3) == 0){
      // In kernel, it must be our mistake.
      cprintf("unexpected trap %d from cpu %d eip %x (cr2=0x%x)\n",
              tf->trapno, cpu->id, tf->eip, rcr2());
      panic("trap");
    }
    // In user space, assume process misbehaved.
    cprintf("pid %d %s: trap %d err %d on cpu %d "
            "eip 0x%x addr 0x%x--kill proc\n",
            proc->pid, proc->name, tf->trapno, tf->err, cpu->id, tf->eip, 
            rcr2());
    proc->killed = 1;
  }

  // Force process exit if it has been killed and is in user space.
  // (If it is still executing in the kernel, let it keep running 
  // until it gets to the regular system call return.)
  if(proc && proc->killed && (tf->cs&3) == DPL_USER)
    exit();

  // Force process to give up CPU on clock tick.
  // If interrupts were on while locks held, would need to check nlock.
  if(proc && proc->state == RUNNING && tf->trapno == T_IRQ0+IRQ_TIMER)
    yield();

  // Check if the process has been killed since we yielded
  if(proc && proc->killed && (tf->cs&3) == DPL_USER)
    exit();
}

void 
page_fault_handler(void)
{
  // For lazy page allocation
  // if is heap or is stack
  // stack if equal, heap otherwise
  if (rcr2() >= proc->tf->esp) { 
    cprintf("heap is faulting\n");
    char *new_mem;
    uint old_adr;
    old_adr = PGROUNDDOWN(rcr2());
    new_mem = kalloc();
    if(new_mem == 0){
      cprintf("system out of memory\n");
      //deallocuvm(pgdir, newsz, oldsz); TODO: Implement this
      return;
    }
    memset(new_mem, 0, PGSIZE);
    mappages(proc->pgdir, (char*)old_adr, PGSIZE, v2p(new_mem), PTE_W|PTE_U);
  }
  // page fault is from code data segment
  // read from disk, page it in
  else {
    struct elfhdr elf;
    struct proghdr ph;
    uint old_adr;
    char *new_mem;
    cprintf("text data segment is faulting!\n");
    old_adr = PGROUNDDOWN(rcr2());
    cprintf("at 0x%x\n", rcr2());
    readi(proc->ipgswp, (char*)&elf, 0, sizeof(elf));
    readi(proc->ipgswp, (char*)&ph, elf.phoff, sizeof(ph));
    //allocuvm(pgdir, sz, ph.vaddr + ph.memsz);
    new_mem = kalloc();
    if(new_mem == 0){
      cprintf("system out of memory\n");
      // IMPLEMENT PAGING OUT HERE
      // PAGE REPLACEMENT ALGO
      //deallocuvm(pgdir, newsz, oldsz); TODO: Implement this
      return;
    }
    memset(new_mem, 0, PGSIZE);
    mappages(proc->pgdir, (char*)old_adr, PGSIZE, v2p(new_mem), PTE_W|PTE_U);
    loaduvm(proc->pgdir, (char*)old_adr, proc->ipgswp, ph.off + old_adr, PGSIZE);

  }
}