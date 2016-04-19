#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "defs.h"
#include "x86.h"
#include "elf.h"
#include "file.h"
#include "stat.h"

char* itoa(int num, char* str, int* nu);
void init_vaddr_queue(struct vaddr_queue* vaq);
void swap_map_add(struct va_swap_map* vsm, uint va);
void init_swap_map(struct va_swap_map* vsm);
struct inode* create(char *path, short type, short major, short minor);

int
exec(char *path, char **argv)
{
  char *s, *last;
  int i, off;
  uint argc, sz, sp, ustack[3+MAXARG+1];
  struct elfhdr elf;
  struct inode *ip;
  struct file *sf;
  struct inode *ip2;
  struct proghdr ph;
  pde_t *pgdir, *oldpgdir;

  begin_op();
  if((ip = namei(path)) == 0){
    end_op();
    return -1;
  }
  ilock(ip);
  pgdir = 0;

  // Check ELF header
  if(readi(ip, (char*)&elf, 0, sizeof(elf)) < sizeof(elf))
    goto bad;
  if(elf.magic != ELF_MAGIC)
    goto bad;

  if((pgdir = setupkvm()) == 0)
    goto bad;

  // Load program into memory.
  sz = 0;
  cprintf("it begins\n");
  for(i=0, off=elf.phoff; i<elf.phnum; i++, off+=sizeof(ph)){
    if(readi(ip, (char*)&ph, off, sizeof(ph)) != sizeof(ph))
      goto bad;
    if(ph.type != ELF_PROG_LOAD)
      continue;
    if(ph.memsz < ph.filesz)
      goto bad;
    //if((sz = allocuvm(pgdir, sz, ph.vaddr + ph.memsz)) == 0)
    //  goto bad;
    sz += ph.vaddr + ph.memsz;
    cprintf("vaddr: 0x%x, memsz: %d, total: %d, sz: %d, 0x%x, round up sz: %d, 0x%x\n", ph.vaddr, ph.memsz, ph.vaddr + ph.memsz, sz, sz, PGROUNDUP(sz), PGROUNDUP(sz));
    //if(loaduvm(pgdir, (char*)ph.vaddr, ip, ph.off, ph.filesz) < 0)
    //  goto bad;
    cprintf("addr: 0x%x, inode: %d, offset: 0x%x, %d, filesz: 0x%x, %d\n", ph.vaddr, ip, ph.off, ph.off, ph.filesz, ph.filesz);
  }
  proc->ipgswp2 = ip;
  int num;
  char swap_name[40];
  itoa(proc->pid, swap_name, &num);
  swap_name[num++] = '.';
  swap_name[num++] = 's';
  swap_name[num++] = 'w';
  swap_name[num++] = 'a';
  swap_name[num++] = 'p';
  swap_name[num]   = '\0';
  ip2 = create(swap_name, T_FILE, 0, 0);
  sf = filealloc();
  sf->type = FD_INODE;
  sf->ip = ip2;
  sf->off = 0;
  sf->readable = 1;
  sf->writable = 1;
  fileclose(sf);
  cprintf("File created!\n");

  init_vaddr_queue(&proc->vaq);
  init_swap_map(&proc->vsm);
  
  uint index;
  proc->vsm.size = 0;
  char *mem_buf = kalloc();
  for (index = 0; index < sz; index += PGSIZE) {
    //swap_map_add(&proc->vsm, index);
    readi(ip, mem_buf, ph.off + index, PGSIZE);
    writei(ip2, mem_buf, index, PGSIZE);
  }
  kfree(mem_buf);
  proc->ipgswp = ip2;
  iunlockput(ip);
  end_op();
  //ip = 0;

  // Allocate two pages at the next page boundary.
  // Make the first inaccessible.  Use the second as the user stack.
  sz = PGROUNDUP(sz);
  if((sz = allocuvm(pgdir, sz, sz + 2*PGSIZE)) == 0)
    goto bad;
  clearpteu(pgdir, (char*)(sz - 2*PGSIZE));
  sp = sz;

  // Push argument strings, prepare rest of stack in ustack.
  for(argc = 0; argv[argc]; argc++) {
    if(argc >= MAXARG)
      goto bad;
    sp = (sp - (strlen(argv[argc]) + 1)) & ~3;
    if(copyout(pgdir, sp, argv[argc], strlen(argv[argc]) + 1) < 0)
      goto bad;
    ustack[3+argc] = sp;
  }
  ustack[3+argc] = 0;

  ustack[0] = 0xffffffff;  // fake return P
  ustack[1] = argc;
  ustack[2] = sp - (argc+1)*4;  // argv pointer

  sp -= (3+argc+1) * 4;
  if(copyout(pgdir, sp, ustack, (3+argc+1)*4) < 0)
    goto bad;

  // Save program name for debugging.
  for(last=s=path; *s; s++)
    if(*s == '/')
      last = s+1;
  safestrcpy(proc->name, last, sizeof(proc->name));

  // Commit to the user image.
  oldpgdir = proc->pgdir;
  proc->pgdir = pgdir;
  proc->sz = sz;
  proc->tf->eip = elf.entry;  // main
  proc->tf->esp = sp;
  switchuvm(proc);
  freevm(oldpgdir);
  return 0;

 bad:
  if(pgdir)
    freevm(pgdir);
  if(ip){
    iunlockput(ip);
    end_op();
  }
  return -1;
}

 
// Implementation of itoa()
char* itoa(int num, char* str, int* nu)
{
    int i = 0;
    char temp;
 
    /* Handle 0 explicitely, otherwise empty string is printed for 0 */
    if (num == 0)
    {
        str[i++] = '0';
        str[i] = '\0';
        return str;
    }
 
    int base = 10;
 
    // Process individual digits
    while (num != 0)
    {
        int rem = num % base;
        str[i++] = (rem > 9)? (rem-10) + 'a' : rem + '0';
        num = num/base;
    }
 
    // If number is negative, append '-
 
    str[i] = '\0'; // Append string terminator
    *nu = i;
    // Reverse the string
    int start = 0;
    int end = i -1;
    while (start < end)
    {
        temp = *(str + start);
        *(str + start) = *(str + end);
        *(str + end) = temp;
        start++;
        end--;
    }
 
    return str;
}