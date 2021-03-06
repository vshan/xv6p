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
void swap_map_add(struct va_swap_map* vsm, uint va);
int swap_map_check(struct va_swap_map* vsm, uint va);
void init_vaddr_queue(struct vaddr_queue* vaq);
void vaddr_queue_enq(struct vaddr_queue* vaq, uint va);
uint vaddr_queue_deq(struct vaddr_queue* vaq);
pte_t *walkpgdir(pde_t *pgdir, const void *va, int alloc);

// Interrupt descriptor table (shared by all CPUs).
struct gatedesc idt[256];
extern uint vectors[];  // in vectors.S: array of 256 entry pointers
struct spinlock tickslock;
uint ticks;
void page_fault_handler(void);
void page_out(void);

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
  uint val, val2;
  if (((uint)(val = rcr2())) >= ((uint)(val2 = proc->tf->esp))) {
    //int i;
    //cprintf("heap is faulting at %d %d\n", val, val2);
    //cprintf("contents of swap map: ");
    //for (i = 0; i < proc->vsm.size; i++) {
    //  cprintf("0x%x", proc->vsm.vaddrs[i]);
    //}
    //cprintf("contents of vaddr queue: ");
    //for (i = 0; i < proc->vaq.size; i++) {
      //cprintf("0x%x", proc->vaq.vaddrs[i]);
    //}
    char *new_mem;
    uint old_adr;
    old_adr = PGROUNDDOWN(rcr2());
    while((new_mem = kalloc()) == 0)
    {
      cprintf("system out of memory\n");
      page_out();
    }
    int ind;
    vaddr_queue_enq(&proc->vaq, rcr2());
    memset(new_mem, 0, PGSIZE);
    mappages(proc->pgdir, (char*)old_adr, PGSIZE, v2p(new_mem), PTE_W|PTE_U);

    if ((ind = swap_map_check(&proc->vsm, old_adr)) == -1) { // heap is new
      swap_map_add(&proc->vsm, rcr2());
    }
    else { // heap page exists
      loaduvm(proc->pgdir, (char*)old_adr, proc->ipgswp, ind * PGSIZE, PGSIZE);
    }
  }
  // page fault is from code data segment
  // read from disk, page it in
  else {
    //int i;
    uint old_adr;
    char *new_mem;
    //cprintf("text data segment is faulting!\n");
    //cprintf("contents of swap map: ");
    //for (i = 0; i < proc->vsm.size; i++) {
    //  cprintf("0x%x", proc->vsm.vaddrs[i]);
    //}
    //cprintf("contents of vaddr queue: ");
    //for (i = 0; i < proc->vaq.size; i++) {
    //  cprintf("0x%x", proc->vaq.vaddrs[i]);
    //}
    old_adr = PGROUNDDOWN(rcr2());
    //cprintf("at 0x%x\n", rcr2());
    
    //allocuvm(pgdir, sz, ph.vaddr + ph.memsz);
    int ind = swap_map_check(&proc->vsm, old_adr);
    while((new_mem = kalloc()) == 0)
    {
      cprintf("system out of memory\n");
      // IMPLEMENT PAGING OUT HERE
      // PAGE REPLACEMENT ALGO
      //deallocuvm(pgdir, newsz, oldsz); TODO: Implement this
      page_out();
    }
    vaddr_queue_enq(&proc->vaq, rcr2());
    memset(new_mem, 0, PGSIZE);
    mappages(proc->pgdir, (char*)old_adr, PGSIZE, v2p(new_mem), PTE_W|PTE_U);
    if ((ind = swap_map_check(&proc->vsm, old_adr)) == -1) { // code data is new
      swap_map_add(&proc->vsm, rcr2());
      struct elfhdr elf;
      struct proghdr ph;
      readi(proc->ipgswp2, (char*)&elf, 0, sizeof(elf));
      readi(proc->ipgswp2, (char*)&ph, elf.phoff, sizeof(ph));
      loaduvm(proc->pgdir, (char*)old_adr, proc->ipgswp2, ph.off + old_adr, PGSIZE);
    }
    else { // code page exists
      loaduvm(proc->pgdir, (char*)old_adr, proc->ipgswp, ind * PGSIZE, PGSIZE);
    }
    //loaduvm(proc->pgdir, (char*)old_adr, proc->ipgswp, ind * PGSIZE, PGSIZE);
  }
}

void 
page_out(void)
{
  pte_t *pte;
  uint pa;
  uint repl_va = PGROUNDDOWN(vaddr_queue_deq(&proc->vaq));
  int ind = swap_map_check(&proc->vsm, repl_va);
  writei(proc->ipgswp, (char*)repl_va, ind * PGSIZE, PGSIZE);
  pte = walkpgdir(proc->pgdir, (char*)repl_va, 0);
  pa = PTE_ADDR(*pte);
  if(pa == 0)
    panic("kfree");
  char *v = p2v(pa);
  kfree(v);
  *pte = 0;
}