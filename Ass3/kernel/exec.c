#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "elf.h"

static int loadseg(pde_t *pgdir, uint64 addr, struct inode *ip, uint offset, uint sz);

//Variables to store old data:
 struct pageData ramArr_backup[MAX_PSYC_PAGES];
 struct pageData swapArr_backup[MAX_PSYC_PAGES];
 int clock_backup;
 struct file *swapFile_backup;

 void backup(){
   struct proc *p = myproc();
   int i;
   for(i = 0; i<MAX_PSYC_PAGES; i++){
     
     //backing up ram arr
     ramArr_backup[i].occupied = p->ramArr[i].occupied;
     ramArr_backup[i].offset = p->ramArr[i].offset;
     ramArr_backup[i].pagetable = p->ramArr[i].pagetable;
     ramArr_backup[i].va_addr = p->ramArr[i].va_addr;
     ramArr_backup[i].counter = p->ramArr[i].counter;

     //backing up swap arr
     swapArr_backup[i].occupied = p->swapArr[i].occupied;
     swapArr_backup[i].offset = p->swapArr[i].offset;
     swapArr_backup[i].pagetable = p->swapArr[i].pagetable;
     swapArr_backup[i].va_addr = p->swapArr[i].va_addr;

   }

  clock_backup = p->clock;
  swapFile_backup = p->swapFile;
 }

 void restore(){
     struct proc *p = myproc();
   int i;
   for(i = 0; i<MAX_PSYC_PAGES; i++){
     
     //restoring ram arr
      p->ramArr[i].occupied = ramArr_backup[i].occupied;
      p->ramArr[i].offset = ramArr_backup[i].offset;
      p->ramArr[i].pagetable = ramArr_backup[i].pagetable;
      p->ramArr[i].va_addr = ramArr_backup[i].va_addr;
      p->ramArr[i].counter = ramArr_backup[i].counter;

     //restoring swap arr
      p->swapArr[i].occupied = swapArr_backup[i].occupied;
      p->swapArr[i].offset = swapArr_backup[i].offset;
      p->swapArr[i].pagetable = swapArr_backup[i].pagetable;
      p->swapArr[i].va_addr = swapArr_backup[i].va_addr;
  
   }

  p->clock = clock_backup;
  p->swapFile = swapFile_backup;
 }


int
exec(char *path, char **argv)
{
  
  char *s, *last;
  int i, off ;
  #if (defined (SCFIFO) || defined(NFUA) || defined(LAPA)) 
  int k;
  #endif
  uint64 argc, sz = 0, sp, ustack[MAXARG+1], stackbase;
  struct elfhdr elf;
  struct inode *ip;
  struct proghdr ph;
  pagetable_t pagetable = 0, oldpagetable;
  struct proc *p = myproc();

  if(DEBUG) printf("started exec function.\n");

  #if (defined (SCFIFO) || defined(NFUA) || defined(LAPA)) 
    if(p->pid > 2){
      
      backup();
      for(k = 0; k <MAX_PSYC_PAGES; k++){
        p->ramArr[k].occupied = 0;
        p->ramArr[k].offset = 0;
        p->ramArr[k].pagetable = 0;
        p->ramArr[k].va_addr = 0;
        #if defined(NFUA)
          p->ramArr[k].counter = 0;
        #elif defined(LAPA)
          p->ramArr[k].counter = 0xFFFFFFFF;
        #endif

        p->swapArr[k].occupied = 0;
        p->swapArr[k].offset = 0;
        p->swapArr[k].pagetable = 0;
        p->swapArr[k].va_addr = 0;
      }
    }
  #endif
  
  begin_op();

  if((ip = namei(path)) == 0){
    end_op();
    return -1;
  }
  ilock(ip);

  // Check ELF header
  if(readi(ip, 0, (uint64)&elf, 0, sizeof(elf)) != sizeof(elf))
    goto bad;
  if(elf.magic != ELF_MAGIC)
    goto bad;

  if((pagetable = proc_pagetable(p)) == 0)
    goto bad;

  // Load program into memory.
  for(i=0, off=elf.phoff; i<elf.phnum; i++, off+=sizeof(ph)){
    if(readi(ip, 0, (uint64)&ph, off, sizeof(ph)) != sizeof(ph))
      goto bad;
    if(ph.type != ELF_PROG_LOAD)
      continue;
    if(ph.memsz < ph.filesz)
      goto bad;
    if(ph.vaddr + ph.memsz < ph.vaddr)
      goto bad;
    uint64 sz1;
    if((sz1 = uvmalloc(pagetable, sz, ph.vaddr + ph.memsz)) == 0)
      goto bad;
    sz = sz1;
    if(ph.vaddr % PGSIZE != 0)
      goto bad;
    if(loadseg(pagetable, ph.vaddr, ip, ph.off, ph.filesz) < 0)
      goto bad;
  }
  iunlockput(ip);
  end_op();
  ip = 0;

  p = myproc();
  uint64 oldsz = p->sz;

  // Allocate two pages at the next page boundary.
  // Use the second as the user stack.
  sz = PGROUNDUP(sz);
  uint64 sz1;
  if((sz1 = uvmalloc(pagetable, sz, sz + 2*PGSIZE)) == 0)
    goto bad;
  sz = sz1;
  uvmclear(pagetable, sz-2*PGSIZE);
  sp = sz;
  stackbase = sp - PGSIZE;

  // Push argument strings, prepare rest of stack in ustack.
  for(argc = 0; argv[argc]; argc++) {
    if(argc >= MAXARG)
      goto bad;
    sp -= strlen(argv[argc]) + 1;
    sp -= sp % 16; // riscv sp must be 16-byte aligned
    if(sp < stackbase)
      goto bad;
    if(copyout(pagetable, sp, argv[argc], strlen(argv[argc]) + 1) < 0)
      goto bad;
    ustack[argc] = sp;
  }
  ustack[argc] = 0;

  // push the array of argv[] pointers.
  sp -= (argc+1) * sizeof(uint64);
  sp -= sp % 16;
  if(sp < stackbase)
    goto bad;
  if(copyout(pagetable, sp, (char *)ustack, (argc+1)*sizeof(uint64)) < 0)
    goto bad;

  // arguments to user main(argc, argv)
  // argc is returned via the system call return
  // value, which goes in a0.
  p->trapframe->a1 = sp;

  // Save program name for debugging.
  for(last=s=path; *s; s++)
    if(*s == '/')
      last = s+1;
  safestrcpy(p->name, last, sizeof(p->name));
    
  // Commit to the user image.
#if (defined (SCFIFO) || defined(NFUA) || defined(LAPA)) 
  if(p->pid > 2){
    for(k = 0; k <MAX_PSYC_PAGES; k++){

    if(p->ramArr[k].occupied){
      p->ramArr[k].pagetable = pagetable;
    }

    if(p->swapArr[k].occupied){
      p->swapArr[k].pagetable = pagetable;
    }
  }  
}
#endif

  oldpagetable = p->pagetable;
  p->pagetable = pagetable;
  p->sz = sz;
  p->trapframe->epc = elf.entry;  // initial program counter = main
  p->trapframe->sp = sp; // initial stack pointer
  #if (defined (SCFIFO) || defined(NFUA) || defined(LAPA)) 
      if(p->pid >2){
      removeSwapFile(p);
      createSwapFile(p);
      }

  #endif
  proc_freepagetable(oldpagetable, oldsz);

  return argc; // this ends up in a0, the first argument to main(argc, argv)

 bad:
  if(pagetable)
    proc_freepagetable(pagetable, sz);
  if(ip){
    iunlockput(ip);
    end_op();
  }

  #if (defined (SCFIFO) || defined(NFUA) || defined(LAPA)) 
      if(p->pid >2)
        restore();
  #endif

  return -1;
}

// Load a program segment into pagetable at virtual address va.
// va must be page-aligned
// and the pages from va to va+sz must already be mapped.
// Returns 0 on success, -1 on failure.
static int
loadseg(pagetable_t pagetable, uint64 va, struct inode *ip, uint offset, uint sz)
{
  uint i, n;
  uint64 pa;

  if((va % PGSIZE) != 0)
    panic("loadseg: va must be page aligned");

  for(i = 0; i < sz; i += PGSIZE){
    pa = walkaddr(pagetable, va + i);
    if(pa == 0)
      panic("loadseg: address should exist");
    if(sz - i < PGSIZE)
      n = sz - i;
    else
      n = PGSIZE;
    if(readi(ip, 0, (uint64)pa, offset+i, n) != n)
      return -1;
  }
  
  return 0;
}
