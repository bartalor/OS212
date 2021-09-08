#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

struct cpu cpus[NCPU];

struct proc proc[NPROC];

struct proc *initproc;


int nextpid = 1;
int nextbid = 1;


struct spinlock pid_lock;
struct spinlock bid_lock;

extern void forkret(void);
static void freeproc(struct proc *p);
static void freethread(struct thread *t);
void do_sig_stop(void);
void do_sig_cont(void);
void do_sig_kill(void);
void checkSignals(void);
void handleSignal(int signum);
struct bsem *find_bsem(int bid);

void customHandler(int signum);

extern void *start_sigret_call(void);
extern void *end_sigret_call(void);

extern char trampoline[]; // trampoline.S

char *procstateStr[] = { "UNUSED", "USED", "ZOMBIE" };
char *threadstateStr[] = {"T_UNUSED", "T_USED", "SLEEPING", "RUNNABLE", "RUNNING", "T_ZOMBIE" };

struct bsem bsem[NBSEM];

// helps ensure that wakeups of wait()ing
// parents are not lost. helps obey the
// memory model when using p->parent.
// must be acquired before any p->lock.
struct spinlock wait_lock;
struct spinlock bsem_wait_lock;

// Allocate a page for each process's kernel stack.
// Map it high in memory, followed by an invalid
// guard page.
void
proc_mapstacks(pagetable_t kpgtbl) {
  struct proc *p;
  struct thread *t;
  int ti, pi;
  for(p = proc, pi=0; pi<NPROC; p++,pi++) {
    for(t = p->threads, ti = 0; ti < NTHREAD; t++,ti++) {
      char *pa = kalloc();
      if(pa == 0)
        panic("kalloc");
      uint64 va = KSTACK((int) (pi*NTHREAD+ti));
      kvmmap(kpgtbl, va, (uint64)pa, PGSIZE, PTE_R | PTE_W);
    }
  }
}

void
procinit(void)
{
  struct proc *p;
  struct thread *t;
  int pi,ti;
  initlock(&pid_lock, "nextpid");
  initlock(&wait_lock, "wait_lock");
  for(p = proc, pi = 0; pi<NPROC; p++, pi++) {
    for(t = p->threads, ti = 0; ti < NTHREAD; t++, ti++) {
      initlock(&t->lock, "proc");
      t->kstack = KSTACK((int) (pi*NTHREAD+ti));
      t->proc = p;
    }
  }
}

// Must be called with interrupts disabled,
// to prevent race with process being moved
// to a different CPU.
int
cpuid()
{
  int id = r_tp();
  return id;
}

// Return this CPU's cpu struct.
// Interrupts must be disabled.
struct cpu*
mycpu(void) {
  int id = cpuid();
  struct cpu *c = &cpus[id];
  return c;
}

// Return the current struct proc *, or zero if none.
struct proc*
myproc(void) {
  push_off();
  struct cpu *c = mycpu();
  struct proc *p = c->proc;
  pop_off();
  return p;
}

struct thread*
mythread(void) {
  push_off();
  struct cpu *c = mycpu();
  struct thread *t = c->thread;
  pop_off();
  return t;
}

int alloctid(struct proc *p)  {
  int tid;
  acquire(&p->tid_lock);
  tid = p->nextTid++;
  release(&p->tid_lock);
  return tid;
}

int
allocpid() {
  int pid;
  acquire(&pid_lock);
  pid = nextpid;
  nextpid = nextpid + 1;
  release(&pid_lock);

  return pid;
}

static struct thread *allocthread(struct proc *p)  {
  struct thread *t;
  for(t = p->threads; t < &p->threads[NTHREAD]; t++) {
    acquire(&t->lock);
    if(t->state == T_UNUSED) {
      goto found;
    } else {
      release(&t->lock);
    }
  }
  return 0;
  found:
  t->tid = alloctid(p);
  t->state = T_USED;

  memset(&t->context, 0, sizeof(t->context));
  t->context.ra = (uint64)forkret;
  t->context.sp = t->kstack + PGSIZE;

  return t;
  
}
// Look in the process table for an UNUSED proc.
// If found, initialize state required to run in the kernel,
// and return with p->lock held.
// If there are no free procs, or a memory allocation fails, return 0.
static struct proc*
allocproc(void)
{
  int i;
  struct proc *p;
  struct thread *t;
  for(p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock);
    if(p->state == UNUSED) {
      goto found;
    } else {
      release(&p->lock);
    }
  }
  return 0;

found:

  p->pid = allocpid();
  p->state = USED;
  p->nextTid = 1;
  p->xstateAssigned = 0;


  /*
  in creation all handlers are SIG_DFL
  */
  for(i = 0; i<NUM_OF_SIGNALS; i++){
    p->signalHandlers[i] = SIG_DFL;
  }     
  
  // Allocate a trapframe page.
  if((p->trapframes = kalloc()) == 0){
    freeproc(p);
    release(&p->lock);
    //release(&p->lock);
    return 0;
  }

  // An empty user page table.
  p->pagetable = proc_pagetable(p);
  if(p->pagetable == 0) {
    release(&p->lock);
    freeproc(p);
    return 0;
  }

  i = 0;
  for(t = p->threads, i = 0; t < &p->threads[NTHREAD]; t++) {
     t->trapframe = (struct trapframe*) (uint64) p->trapframes + i;
     i++;
  }

  if(allocthread(p) == 0) {
    release(&p->lock);
    freeproc(p);
    return 0;
  }

  return p;
}


// free a proc structure and the data hanging from it,
// including user pages.
// p->lock must be held.


static void
freeproc(struct proc *p)
{
  struct thread *t;
  for(t = p->threads; t < &p->threads[NTHREAD]; t++)  {
    freethread(t);
  }
  if(p->trapframes)
      kfree(p->trapframes);
  for(t = p->threads; t < &p->threads[NTHREAD]; t++)  {
    t->trapframe = 0;
  }
  if(p->pagetable)
  proc_freepagetable(p->pagetable, p->sz);
  p->pagetable = 0;
  p->sz = 0;
  p->pid = 0;
  p->parent = 0;
  p->name[0] = 0;
  p->state = UNUSED;
}

static void
freethread(struct thread *t)
{
  t->chan = 0;
  t->killed = 0;
  t->xstate = 0;
  t->state = T_UNUSED;
}

// Create a user page table for a given process,
// with no user memory, but with trampoline pages.
pagetable_t
proc_pagetable(struct proc *p)
{
  pagetable_t pagetable;
  // An empty page table.
  pagetable = uvmcreate();
  if(pagetable == 0)
    return 0;

  // map the trampoline code (for system call return)
  // at the highest user virtual address.
  // only the supervisor uses it, on the way
  // to/from user space, so not PTE_U.
  if(mappages(pagetable, TRAMPOLINE, PGSIZE,
              (uint64)trampoline, PTE_R | PTE_X) < 0){
    uvmfree(pagetable, 0);
    return 0;
  }

  // map the trapframe just below TRAMPOLINE, for trampoline.S.
  if(mappages(pagetable, TRAPFRAME, PGSIZE,
              (uint64)(p->trapframes), PTE_R | PTE_W) < 0){
    uvmunmap(pagetable, TRAMPOLINE, 1, 0);
    uvmfree(pagetable, 0);
    return 0;
  }

  return pagetable;
}

// Free a process's page table, and free the
// physical memory it refers to.
void
proc_freepagetable(pagetable_t pagetable, uint64 sz)
{
  uvmunmap(pagetable, TRAMPOLINE, 1, 0);
  uvmunmap(pagetable, TRAPFRAME, 1, 0);
  uvmfree(pagetable, sz);
}

// a user program that calls exec("/init")
// od -t xC initcode
uchar initcode[] = {
  0x17, 0x05, 0x00, 0x00, 0x13, 0x05, 0x45, 0x02,
  0x97, 0x05, 0x00, 0x00, 0x93, 0x85, 0x35, 0x02,
  0x93, 0x08, 0x70, 0x00, 0x73, 0x00, 0x00, 0x00,
  0x93, 0x08, 0x20, 0x00, 0x73, 0x00, 0x00, 0x00,
  0xef, 0xf0, 0x9f, 0xff, 0x2f, 0x69, 0x6e, 0x69,
  0x74, 0x00, 0x00, 0x24, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00
};

// Set up first user process.
void
userinit(void)
{
  struct proc *p;
  p = allocproc();
  initproc = p;
  
  // allocate one user page and copy init's instructions
  // and data into it.
  uvminit(p->pagetable, initcode, sizeof(initcode));
  p->sz = PGSIZE;

  // prepare for the very first "return" from kernel to user.
  p->threads[0].trapframe->epc = 0;      // user program counter
  p->threads[0].trapframe->sp = PGSIZE;  // user stack pointer

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  p->threads[0].state = RUNNABLE;
  release(&p->threads[0].lock);
  release(&p->lock);
}

// Grow or shrink user memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint sz;
  struct proc *p = myproc();
  sz = p->sz;
  if(n > 0){
    if((sz = uvmalloc(p->pagetable, sz, sz + n)) == 0) {
      return -1;
    }
  } else if(n < 0){
    sz = uvmdealloc(p->pagetable, sz, sz + n);
  }
  p->sz = sz;
  return 0;
}

// Create a new process, copying the parent.
// Sets up child kernel stack to return as if from fork() system call.
int
fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *p = myproc();
  struct thread *t = mythread();
  
  
  if(p->killed == 1){
    return -1;
  }

   if(t->killed == 1){
    return -1;
  }

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  // Copy user memory from parent to child.
  if(uvmcopy(p->pagetable, np->pagetable, p->sz) < 0){
    freeproc(np);
    release(&np->threads[0].lock);
    release(&np->lock);
    return -1;
  }
  np->sz = p->sz;
  
  //*copy signum mask and signum handlers
  np->signalMask = p->signalMask;
  for( i =0; i<NUM_OF_SIGNALS; i++){
    np->signalHandlers[i] = p->signalHandlers[i];
  }

  // copy saved user registers.
  *(np->threads[0].trapframe) = *(t->trapframe);

  // Cause fork to return 0 in the child.
  np->threads[0].trapframe->a0 = 0;

  // increment reference counts on open file descriptors.
  for(i = 0; i < NOFILE; i++)
    if(p->ofile[i])
      np->ofile[i] = filedup(p->ofile[i]);
  np->cwd = idup(p->cwd);

  safestrcpy(np->name, p->name, sizeof(p->name));

  pid = np->pid;
  release(&np->threads[0].lock);
  release(&np->lock);

  acquire(&wait_lock);
  np->parent = p;
  release(&wait_lock);

  acquire(&np->threads[0].lock);
  np->threads[0].state = RUNNABLE;
  release(&np->threads[0].lock);

  return pid;
}

// Pass p's abandoned children to init.
// Caller must hold wait_lock.
void
reparent(struct proc *p)
{
  struct proc *pp;
  for(pp = proc; pp < &proc[NPROC]; pp++){
    if(pp->parent == p){
      pp->parent = initproc;
      wakeup(initproc);
    }
  }
}

int count_threads(void) {
  int count = 0;
  struct proc *p = myproc();
  struct thread *t;
  for(t = p->threads; t < &p->threads[NTHREAD]; t++) {
    count += (t->state != T_UNUSED) && (t->state != T_ZOMBIE);
  }
  return count;
}

void exit(int status) {
  struct thread *t;
  struct proc *p = myproc();
  acquire(&p->lock);
  if(p->xstateAssigned == 0)  {
    p->xstateAssigned = 1;
  }
  int count = count_threads();
  if(count == 0) panic("all threads already dead, not good.");
  else if(1 < count) { // killing bros
    for(t = p->threads; t < &p->threads[NTHREAD]; t++)  {
      t->killed = 1;
    }
    release(&p->lock);
  }
  else  { // count == 1
    release(&p->lock);
    exit_last_thread_and_process(status);
  }
}


void kthread_exit(int status) {
  struct thread *t = mythread();
  struct proc *p = myproc();
  acquire(&p->lock);
  if(p->xstateAssigned == 0)  {
    p->xstate = status;
    p->xstateAssigned = 1;
  }
  int count = count_threads();
  if(count == 0) panic("all threads already dead, not good.");
  else if(1 < count) {
    acquire(&t->lock);
    t->state = T_ZOMBIE;
    wakeup(t); // wakeup threads waiting to join me.
    release(&p->lock);
    sched();
  }
  else  { // count == 1
    release(&p->lock);
    exit_last_thread_and_process(status);
  }
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait().
void
exit_last_thread_and_process(int status)
{
  struct proc *p = myproc();
  struct thread *t = mythread();
  
  if(count_threads() != 1) // sanity check
    panic("exit_last_thread_and_process: more than ove thread left!");

  if(p == initproc)
    panic("init exiting");
  // Close all open files.
  for(int fd = 0; fd < NOFILE; fd++){
    if(p->ofile[fd]){
      struct file *f = p->ofile[fd];
      fileclose(f);
      p->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(p->cwd);
  end_op();
  p->cwd = 0;

  acquire(&wait_lock);

  // Give any children to init.
  reparent(p);

  // Parent might be sleeping in wait().
  wakeup(p->parent);

  acquire(&p->lock);
  p->state = ZOMBIE;
  release(&p->lock);

  acquire(&t->lock);
  t->xstate = status;
  t->state = T_ZOMBIE;

  release(&wait_lock);

  // Jump into the scheduler, never to return.
  sched();
  panic("zombie exit");
}





// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(uint64 addr)
{
  struct proc *np;
  int havekids, pid;
  struct proc *p = myproc();
  acquire(&wait_lock);
  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(np = proc; np < &proc[NPROC]; np++){
      if(np->parent == p){
        // make sure the child isn't still in exit() or swtch().
        acquire(&np->lock);

        havekids = 1;
        if(np->state == ZOMBIE){
          // Found one.
          pid = np->pid;
          if(addr != 0 && copyout(p->pagetable, addr, (char *)&np->xstate,
                                  sizeof(np->xstate)) < 0) {
            release(&np->lock);
            release(&wait_lock);
            return -1;
          }
          freeproc(np);
          release(&np->lock);
          release(&wait_lock);
          return pid;
        }
        release(&np->lock);
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || p->killed){
      release(&wait_lock);
      return -1;
    }
    
    // Wait for a child to exit.
    sleep(p, &wait_lock);  //DOC: wait-sleep
  }
}

//returns true whether Cont or Kill are pending false otherwise
int checkContOrKill(struct proc *p){
  int sig;
  void* handler;

  for (sig = MIN_SIG; sig <= MAX_SIG; sig++) {
    if(p->pendingSignals & (1 << sig)) {
      handler = ((struct sigaction*)&p->signalHandlers[sig])->sa_handler;

      if(handler == (void (*)())SIGCONT || handler == (void (*)())SIGKILL)
        return 1;

      else if((sig == SIGCONT && (handler == SIG_DFL))   || sig == SIGKILL)
        return 1;
    }
  }
  return 0;
}

// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run.
//  - swtch to start running that process.
//  - eventually that process transfers control
//    via swtch back to the scheduler.
void
scheduler(void)
{
  struct proc *p;
  struct thread *t;
  int pi, ti;
  struct cpu *c = mycpu();
  c->thread = 0;
  c->proc = 0;
  for(;;){
    // Avoid deadlock by ensuring that devices can interrupt.
    intr_on();
    for(p = proc, pi = 0; pi < NPROC; p++, pi++) {
        acquire(&p->lock);
         if(p->stopped){
            if(checkContOrKill(p)){
            }
            else{
              release(&p->lock);
              continue;
            }
          }
          release(&p->lock);
      for(t = p->threads, ti = 0; ti < NTHREAD; t++, ti++) {
        acquire(&t->lock);
        if(t->state == RUNNABLE) {
          // Switch to chosen process.  It is the process's job
          // to release its lock and then reacquire it
          // before jumping back to us.
       
          
          t->state = RUNNING;
          c->thread = t;
          c->proc = p;
       
          swtch(&c->context, &t->context);

          // Process is done running for now.
          // It should have changed its p->state before coming back.
          c->thread = 0;
          c->proc = 0;
        }
        release(&t->lock);
      }
    }
  }
}

// Switch to scheduler.  Must hold only p->lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->noff, but that would
// break in the few places where a lock is held but
// there's no process.
void
sched(void)
{
  int intena;
  struct thread *t = mythread();
  if(!holding(&t->lock))
    panic("sched t->lock");
  // if(mycpu()->noff != 1)
  //   panic("sched locks");
  if(t->state == RUNNING)
    panic("sched running");
  if(intr_get())
    panic("sched interruptible");

  intena = mycpu()->intena;

  swtch(&t->context, &mycpu()->context);

  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  struct thread *t = mythread();

  acquire(&t->lock);
  t->state = RUNNABLE;
  sched();
  release(&t->lock);

}

// A fork child's very first scheduling by scheduler()
// will swtch to forkret.
void
forkret(void) {
  static int first = 1;
  // Still holding p->lock from scheduler.
  release(&mythread()->lock);

  if (first) {
    // File system initialization must be run in the context of a
    // regular process (e.g., because it calls sleep), and thus cannot
    // be run from main().
    first = 0;
    fsinit(ROOTDEV);
  }

  usertrapret();
}

int kthread_create ( void ( *start_func ) ( ) , void *stack ) {
  struct proc *p = myproc();  
  struct thread *t = mythread();
  struct thread *nt;
  int id;

  if(p->killed == 1){
    return -1;
  }

   if(t->killed == 1){
    return -1;
  }

  acquire(&p->lock);
  if((nt = allocthread(p)) == 0)  {
    release(&nt->lock);
    release(&p->lock);
    return -1;
  }
  *(nt->trapframe) = *(t->trapframe);
  nt->trapframe->epc = (uint64)start_func;
  nt->trapframe->sp = (uint64)(stack + MAX_STACK_SIZE);
  nt->state = RUNNABLE;
  id = nt->tid;
  release(&nt->lock);
  release(&p->lock);
  return id;
}


int kthread_join(int thread_id, int *status)  {
  // how do we get status???
  struct proc *p = myproc();
  struct thread *it = mythread();

   if(p->killed == 1){
    return -1;
  }

   if(it->killed == 1){
    return -1;
  }

  acquire(&p->lock);
  struct thread* t = get_thread(thread_id);
  if(t == 0) {
    release(&p->lock);
    return -1;
  }
  if(t->state == T_UNUSED)  {
    release(&t->lock);
    release(&p->lock);
    return -1;
  }
  if(t->state == T_ZOMBIE)  {
    freethread(t);
    if(status)
      either_copyout(1,(uint64)status,&t->xstate,sizeof(int));
    release(&t->lock);
    release(&p->lock);
    return 0;
  }
  release(&t->lock);
  sleep(t,&p->lock);
  release(&p->lock);

  if(t->state == T_ZOMBIE && t->tid == thread_id)  {
    if(status)
      either_copyout(1,(uint64)status,&t->xstate,sizeof(int));
    return 0;
  }
  else  { // missed it
    return -1;
  }

  return 0;
}


struct thread *get_thread(int tid) {
  struct proc *p = myproc();
  struct thread *t;
  if(tid <= 0) return 0;
  for(p = proc; p<&proc[NPROC]; p++) {
    for(t = p->threads; t<&p->threads[NTHREAD]; t++)  {
      if(t->state == T_UNUSED) continue;
      if(t->tid == tid) {
        acquire(&t->lock);
        return t;
      }
    }
  }
  return 0;
}

void
sleep(void *chan, struct spinlock *lk) // how to synchronize
{
  struct thread *t = mythread();
  // Must acquire p->lock in order to
  // change p->state and then call sched.
  // Once we hold p->lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup locks p->lock),
  // so it's okay to release lk.

  acquire(&t->lock);  //DOC: sleeplock1
  release(lk);

  // Go to sleep.
  t->chan = chan;
  t->state = SLEEPING;

  sched();

  // Tidy up.
  t->chan = 0;

  // Reacquire original lock.
  release(&t->lock);
  acquire(lk);
}

// Wake up all processes sleeping on chan.
// Must be called without any p->lock.
void
wakeup(void *chan)
{
  struct proc *p;
  struct thread *t;
  for(p = proc; p < &proc[NPROC]; p++) {
    for(t = p->threads; t < &p->threads[NTHREAD]; t++)  {
      if(t != mythread()) {
        acquire(&t->lock);
        if(t->state == SLEEPING && t->chan == chan) {
          t->state = RUNNABLE;
        }
        release(&t->lock);
      }
    }
  }
}

// Kill the process with the given pid.
// The victim won't exit until it tries to return
// to user space (see usertrap() in trap.c).

int
kill(int pid , int signum)
{
  struct proc *p;
  if(signum > MAX_SIG || signum < MIN_SIG)
    return -1;
  for(p = proc; p < &proc[NPROC]; p++)  {
    acquire(&p->lock);
    if(p->pid == pid){
      p->pendingSignals = p->pendingSignals | (1 << signum);
      release(&p->lock);
      return 0;
    }
    release(&p->lock);
  }
  return -1;
}


//* syscall sigprocmask
uint
sigprocmask(uint sigmask){
   struct proc *p = myproc();
   int oldMask;
   acquire(&p->lock);
   oldMask = p->signalMask;
   p->signalMask = sigmask;
   release(&p->lock);
   return oldMask;
}

int 
sigaction(int signum, uint64 act, uint64 oldact){
  struct proc *p = myproc();
  struct sigaction to_old;
  struct sigaction new_act;
  memset(&to_old, 0, sizeof(to_old));
  memset(&new_act, 0, sizeof(new_act));

  if(signum == SIGKILL || signum == SIGSTOP || signum > MAX_SIG || signum < MIN_SIG)
    return -1;

  if(p->signalHandlers[signum] != 0){
    //*coppying data from kernel to user
    if(either_copyout(1, oldact, (void *)((struct sigaction*)&p->signalHandlers[signum]) , sizeof(to_old)) < 0)
        return -1;
  }

  if(act!=0){

    if((p->signalHandlers[signum] = (struct sigaction *)kalloc()) == 0){
      return -1;
    }
      
    if(either_copyin(&p->signalHandlers[signum], 1, act, sizeof(struct sigaction)) < 0)
    return -1;

  }

   return 0;
} 

void 
sigret(void){
  struct proc *p = myproc(); 
  struct thread *t = mythread();
  either_copyin(t->trapframe, 1, (uint64)t->tp_backup, sizeof(struct trapframe));
  t->trapframe->sp = (uint64)t->tp_backup;
  t->trapframe->sp += sizeof(struct trapframe);
  p->signalMask = p->backUpsignalMask;
}

void 
do_sig_stop(void){
  struct proc *p = myproc();
  p->stopped = 1;
//  p->pendingSignals = (p->pendingSignals & (~(1 << SIGSTOP))); //where should off the signals
}

void 
do_sig_cont(void){
  struct proc *p = myproc();
   p->stopped = 0;
  // p->pendingSignals = (p->pendingSignals & (~(1 << SIGCONT))); //where should off the signals
}

void 
do_sig_kill(void){
  struct proc *p = myproc();
   p->killed = 1;
  wake_process(p);
  // p->pendingSignals = (p->pendingSignals & (~(1 << SIGCONT))); //where should off the signals
}

void wake_process(struct proc *p) {
  struct thread *t;
  for(t = p->threads; t < &p->threads[NTHREAD]; t++) {
       if(t->state == SLEEPING){
        t->state = RUNNABLE;
      }
  }
}

void
checkSignals(void){
     struct proc *p = myproc();
     int signum;
     acquire(&p->lock);
     for(signum = MIN_SIG; signum < NUM_OF_SIGNALS; signum++){
         if((p->pendingSignals & (1 << signum))!= 0){

           if(signum == SIGKILL || signum == SIGSTOP){
             handleSignal(signum); 
             break;
           }

           if(((1 << signum) & (1 << p->signalMask)) != 0)
              continue;
            

            else{
              p->backUpsignalMask = p->signalMask;
              p->signalMask = ((struct sigaction*)&p->signalHandlers[signum])->sigmask;
              //p->signalMask =  p->sigactions[signum].sigmask;
              handleSignal(signum);
              break;
            }
         }
     }
     release(&p->lock);
     return;
}


void
handleSignal(int sig){
  struct proc *p = myproc();
  p->pendingSignals ^= (1 << sig); // turn off signals before handling it 
  
  if(p->signalHandlers[sig] == (void*)SIG_IGN)
    return;

   else if(p->signalHandlers[sig] == (void *)SIG_DFL){
     switch(sig){
       case SIGSTOP:
        do_sig_stop();
        break;

         case SIGCONT:
        do_sig_cont();
        break;

         case SIGKILL:
        do_sig_kill();
        break;

        default:  {
          do_sig_kill();
        }
     }
   } 

  else  {
    customHandler(sig);
  }
} 

void customHandler(int signum) {
  struct proc *p = myproc();
  struct thread *t = mythread();
  t->trapframe->sp -= sizeof(struct trapframe);
  either_copyout(1,t->trapframe->sp,t->trapframe,sizeof(struct trapframe));
  t->tp_backup = (struct trapframe*) t->trapframe->sp;

  uint64 sigret_call_size = end_sigret_call - start_sigret_call;
  t->trapframe->sp -= sigret_call_size;
  either_copyout(1,t->trapframe->sp,start_sigret_call,sigret_call_size);

  t->trapframe->a0 = signum;
  t->trapframe->ra = t->trapframe->sp;
  
  t->trapframe->epc = (uint64)((struct sigaction*)&p->signalHandlers[signum])->sa_handler;

  return;
}

// Copy to either a user address, or kernel address,
// depending on usr_dst.
// Returns 0 on success, -1 on error.
int
either_copyout(int user_dst, uint64 dst, void *src, uint64 len)
{
  struct proc *p = myproc();
  if(user_dst){
    return copyout(p->pagetable, dst, src, len);
  } else {
    memmove((char *)dst, src, len);
    return 0;
  }
}

// Copy from either a user address, or kernel address,
// depending on usr_src.
// Returns 0 on success, -1 on error.
int
either_copyin(void *dst, int user_src, uint64 src, uint64 len)
{
  struct proc *p = myproc();
  if(user_src){
    return copyin(p->pagetable, dst, src, len);
  } else {
    memmove(dst, (char*)src, len);
    return 0;
  }
}

// Semaphore:
  void seminit(void)  {
    struct bsem *b;
    initlock(&bsem_wait_lock, "bsem_wait_lock");
    for(b = bsem; b < &bsem[NBSEM]; b++)  {
      initlock(&b->lock, "bsem");
    }
  }
  // BSEM:
    int allocbid()  {
      int bid;
      acquire(&bid_lock);
      bid = nextbid++;
      release(&bid_lock);
      return bid;
    }

  /*
    int bsem_alloc()  {
      struct bsem *b;
      if((b = find_bsem(0)) == 0) return -1;
      b->bid = allocbid();
      b->val = 0;
      release(&b->lock);
      return b->bid;
    }

    struct bsem *find_bsem(int bid) {
      struct bsem *b = 0;
      for(b = bsem; b < &bsem[NBSEM]; b++) {
        acquire(&b->lock);
        if(b->bid == bid) return b;
        else release(&b->lock);
      }
      return 0;
    }

    void bsem_free(int bid)  {
      struct bsem *b;
      if((b = find_bsem(bid)) == 0) return;
      b->bid = 0;
      release(&b->lock);  // to delete??
    }

    void bsem_down(int bid) {
      struct bsem *b;
      while((b = find_bsem(bid))) {
        release(&b->lock);
        acquire(&bsem_wait_lock);
        sleep(b,&bsem_wait_lock);
        acquire(&b->lock);
        if(b->val == 0) {
          b->val = 1;
          release(&b->lock);
          release(&bsem_wait_lock);
          return;
        }
        release(&b->lock);
      }
    }

    void bsem_up(int bid) {
      struct bsem *b;
      if((b = find_bsem(bid)) == 0) return;
      b->val = 0;
      release(&b->lock);
      wakeup(b);
    } */



int bsem_alloc(void){

struct bsem *b;
for(b = bsem; b < &bsem[NBSEM]; b++ ) {
   acquire(&b->lock);
       if(b->bid == 0) {
      goto found;
    } 
    else {
      release(&b->lock);
    }
}
return -1;

found: 
b->bid = allocbid();
b->locked = FREE;
release(&b->lock);
return b->bid;
}


void bsem_free(int desc){

  struct bsem *b;

for(b = bsem; b < &bsem[NBSEM]; b++ ) {
   acquire(&b->lock);
       if(b->bid == desc) {
      goto found;
    } 
    else {
      release(&b->lock);
    }
}
return;

found: 
b->locked = FREE;
//b->lock = 0;
b->bid = 0;
release(&b->lock); 
}


void bsem_down(int desc){
struct bsem *b;

for(b = bsem; b < &bsem[NBSEM]; b++ ) {
   acquire(&b->lock);
       if(b->bid == desc) {
      goto found;
    } 
    else {
      release(&b->lock);
    }
}
return;

found: 

  while(1){
    if(b->locked == FREE){
      b->locked = LOCKED;
      release(&b->lock);
      break;
    }
    sleep(b,&b->lock);
  }
}


void bsem_up(int desc){
struct bsem *b;

for(b = bsem; b < &bsem[NBSEM]; b++ ) {
   acquire(&b->lock);
       if(b->bid == desc) {
      goto found;
    } 
    else {
      release(&b->lock);
    }
}
return;

found: 
  if(b->locked == LOCKED){
     b->locked = FREE;
     release(&b->lock);
     wakeup(b);
  }
}

// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [ZOMBIE]    "zombie"
  };
  struct proc *p;
  char *state;

  printf("\n");
  for(p = proc; p < &proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    printf("%d %s %s", p->pid, state, p->name);
    printf("\n");
  }
}
