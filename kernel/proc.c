#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#define NTHREAD MAX_THREADS_PER_PROCESS*NPROC

struct cpu cpus[NCPU];

struct proc proc[NPROC];

//pointers of all the threads used to assign kstack
struct sthread thread[NTHREAD];

struct proc *initproc;

int nextpid = 1;
int nexttid = 0;
struct spinlock pid_lock;

extern pagetable_t thread_pagetable(struct sthread *s);
extern void forkret(void);
static void freeproc(struct proc *p);
extern char trampoline[]; // trampoline.S
extern int threadkilled(struct sthread *t);
extern void threadsetkilled(struct sthread *t);
extern struct sthread* mythread();
// helps ensure that wakeups of wait()ing
// parents are not lost. helps obey the
// memory model when using p->parent.
// must be acquired before any p->lock.
struct spinlock wait_lock;

// Allocate a page for each process's kernel stack.
// Map it high in memory, followed by an invalid
// guard page.
void
proc_mapstacks(pagetable_t kpgtbl)
{
  struct proc *p;
  
  for(p = proc; p < &proc[NPROC]; p++) {
    char *pa = kalloc();
    if(pa == 0)
      panic("kalloc");
    uint64 va = KSTACK((int) (p - proc));
    kvmmap(kpgtbl, va, (uint64)pa, PGSIZE, PTE_R | PTE_W);
  }
}

// initialize the proc table.
void
procinit(void)
{
  struct proc *p;
  
  initlock(&pid_lock, "nextpid");
  initlock(&wait_lock, "wait_lock");
  for(p = proc; p < &proc[NPROC]; p++) {
      initlock(&p->lock, "proc");
      p->state = UNUSED;
      //this is handled in allocthread now
      //p->threads[0]->kstack = KSTACK((int) (p - proc));
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
mycpu(void)
{
  int id = cpuid();
  struct cpu *c = &cpus[id];
  return c;
}

// Return the current struct proc *, or zero if none.
struct proc*
myproc(void)
{
  push_off();
  struct cpu *c = mycpu();
  struct proc *p = c->proc;
  pop_off();
  return p;
}

// returns the currently running thread, or zero if none
struct sthread* mythread()
{
  push_off();
  struct cpu *c = mycpu();
  struct sthread *t = c->thread;
  pop_off();
  return t;
}

int
allocpid()
{
  int pid;
  
  acquire(&pid_lock);
  pid = nextpid;
  nextpid = nextpid + 1;
  release(&pid_lock);

  return pid;
}

// Create a user page table for a given process, with no user memory,
// but with trampoline and trapframe pages.
pagetable_t
thread_pagetable(struct sthread *s)
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

  // map the trapframe page just below the trampoline page, for
  // trampoline.S.
  if(mappages(pagetable, TRAPFRAME, PGSIZE,
              (uint64)(s->trapframe), PTE_R | PTE_W) < 0){
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
// assembled from ../user/initcode.S
// od -t xC ../user/initcode
uchar initcode[] = {
  0x17, 0x05, 0x00, 0x00, 0x13, 0x05, 0x45, 0x02,
  0x97, 0x05, 0x00, 0x00, 0x93, 0x85, 0x35, 0x02,
  0x93, 0x08, 0x70, 0x00, 0x73, 0x00, 0x00, 0x00,
  0x93, 0x08, 0x20, 0x00, 0x73, 0x00, 0x00, 0x00,
  0xef, 0xf0, 0x9f, 0xff, 0x2f, 0x69, 0x6e, 0x69,
  0x74, 0x00, 0x00, 0x24, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00
};

//allocates a new thread
//revert thread_alloc function to as close to the original version
//of procalloc as possible
struct sthread *thread_alloc(struct proc *parent, struct sthread *nt){
 // nt-> func = func;
 // nt-> func_args = func_args;
  nt->state = RUNNABLE;
  printf("nt->state set to RUNNABLE at line 187\n");

  nt-> tid = nexttid;
  nexttid++;

  //init trapframe
  if ((nt->trapframe = (struct trapframe *)kalloc() ) == 0){
    printf("trapframe fucked :(");
  }

  //init pagetable
  nt->pagetable = thread_pagetable(nt);
  if(nt->pagetable == 0){
    printf("pagetable fucked :(");
  }
  
  //init kstack uses kstack macro that determines distance from start of all threads to new one
  nt->kstack = KSTACK((int) (&parent->threads[0] - nt));
  //nt->kstack = KSTACK((int) (initproc - parent));


  // Set up new context to start executing at forkret,
  // which returns to user space.
  printf("here 1\n");
  memset(&nt->context, 0, sizeof(nt->context));
  nt->context.ra = (uint64)forkret;
  nt->context.sp = nt->kstack + PGSIZE;
  printf("here 2\n");

  return nt;

}

// Look in the process table for an UNUSED proc.
// If found, initialize state required to run in the kernel,
// and return with p->lock held.
// If there are no free procs, or a memory allocation fails, return 0.
static struct proc*
allocproc(void)
{
  struct proc *p;

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

  //we are moving this to threads
  // Allocate a trapframe page.
  // if((p->trapframe = (struct trapframe *)kalloc()) == 0){
  //   freeproc(p);
  //   release(&p->lock);
  //   return 0;
  // }

  // An empty user page table.
  //moved
  // p->pagetable = proc_pagetable(p);
  // if(p->pagetable == 0){
  //   freeproc(p);
  //   release(&p->lock);
  //   return 0;
  // }

  //Create starting thread.
  //how are we gonna get argc and argc
  //todo finish
  thread_alloc(p, &p->threads[0]);
  p->num_threads = 1;

//moved to thread
  // // Set up new context to start executing at forkret,
  // // which returns to user space.
  // memset(&p->context, 0, sizeof(p->context));
  // p->context.ra = (uint64)forkret;
  // p->context.sp = p->kstack + PGSIZE;

  return p;
}

static void freethread(struct sthread *t){
  printf("calling freethread\n");
  if(t->trapframe)
    kfree((void*)t->trapframe);
  t->trapframe = 0;
  if(t->pagetable)
    proc_freepagetable(t->pagetable, t->sz);
  t->sz = 0;
  t->tid = 0;
  t->parent = 0;
  //p->name[0] = 0;
  t->chan = 0;
  t->killed = 0;
  t->xstate = 0;
  t->state = UNUSED;
}

// free a proc structure and the data hanging from it,
// including user pages.
// p->lock must be held.
static void
freeproc(struct proc *p)
{
  printf("CALLING freeproc\n");
  printf("calling freeproc\n");
  //frees all threads
  for(int i = 0;i<p->num_threads;i++){
    freethread(&p->threads[i]);
  }
  // if(p->trapframe)
  //   kfree((void*)p->trapframe);
  // p->trapframe = 0;
  // if(p->pagetable)
  //   proc_freepagetable(p->pagetable, p->sz);
 // p->pagetable = 0;
  p->num_threads = 0;
  p->sz = 0;
  p->pid = 0;
  p->parent = 0;
  //p->name[0] = 0;
  //p->chan = 0;
  p->killed = 0;
  p->xstate = 0;
  p->state = UNUSED;
}


// Create a user page table for a given process, with no user memory,
// but with trampoline and trapframe pages.
// pagetable_t
// proc_pagetable(struct proc *p)
// {
//   pagetable_t pagetable;

//   // An empty page table.
//   pagetable = uvmcreate();
//   if(pagetable == 0)
//     return 0;

//   // map the trampoline code (for system call return)
//   // at the highest user virtual address.
//   // only the supervisor uses it, on the way
//   // to/from user space, so not PTE_U.
//   if(mappages(pagetable, TRAMPOLINE, PGSIZE,
//               (uint64)trampoline, PTE_R | PTE_X) < 0){
//     uvmfree(pagetable, 0);
//     return 0;
//   }

//   // map the trapframe page just below the trampoline page, for
//   // trampoline.S.
//   if(mappages(pagetable, TRAPFRAME, PGSIZE,
//               (uint64)(p->trapframe), PTE_R | PTE_W) < 0){
//     uvmunmap(pagetable, TRAMPOLINE, 1, 0);
//     uvmfree(pagetable, 0);
//     return 0;
//   }

//   return pagetable;
// }



// Set up first user process.
//Modified since many of these fields are now in thread
void
userinit(void)
{
  printf("calling userinit\n");
  struct proc *p;

  p = allocproc();
  initproc = p;
  
  // allocate one user page and copy initcode's instructions
  // and data into it.
  uvmfirst(p->threads[0].pagetable, initcode, sizeof(initcode));
  p->sz = PGSIZE;

  //TODO: THIS MIGHT BE WRONG (PGSIZE/THREADS_PER_PROCESS)
  p->threads[0].sz = PGSIZE;


  // prepare for the very first "return" from kernel to user.
  p->threads[0].trapframe->epc = 0;      // user program counter
  p->threads[0].trapframe->sp = PGSIZE;  // user stack pointer

  //todo: possibly modify this
  safestrcpy(p->name, "initcode", sizeof(p->name));
  safestrcpy(p->threads[0].name, "initcode", sizeof(p->name));
  p->threads[0].cwd = namei("/");

  //p->state = USED;
  p->threads[0].state = RUNNABLE;
  printf("p->state set to RUNNABLE at line 373\n");

  //shouldnt need to do this and can't
  //mythread()->state = RUNNABLE;
  //printf("mythread()->state set to RUNNABLE at line 375\n");
  printf("done with userinit\n");
  release(&p->lock);
}

// Grow or shrink user memory by n bytes.
// Return 0 on success, -1 on failure.
// Changed to threads
int
growproc(int n)
{
  uint64 sz;
  //struct proc *p = myproc();
  struct sthread *t = mythread();

  sz = t->sz;
  if(n > 0){
    if((sz = uvmalloc(t->pagetable, sz, sz + n, PTE_W)) == 0) {
      return -1;
    }
  } else if(n < 0){
    sz = uvmdealloc(t->pagetable, sz, sz + n);
  }
  t->sz = sz;
  return 0;
}


//add a new thread to an existing process
struct sthread *create_thread(void* func, void* func_args){
  struct proc* parent = myproc();
  struct sthread* nt = thread_alloc(parent, &parent->threads[parent->num_threads]);
  

  parent->num_threads = parent->num_threads + 1;
  //parent->threads[parent->num_threads] = *nt;

  //overwrite context: instead of executing at forkret, we want to execute at whatever function in parameter
  nt->context.ra = (uint64)func;
  //todo push args onto stack

  nt->func = func;
  nt->arg = func_args;
  return nt;
}

// Create a new process, copying the parent, with a single initial thread (handled in allocproc).
// Sets up child kernel stack to return as if from fork() system call.
int
fork(void)
{
  int i;
  struct proc *p = myproc();
  struct sthread *t = mythread();
  printf("calling fork\n");
  //t = parent
  //p->threads[next] = child

  // Allocate thread.
  int next = p->num_threads;
  if(thread_alloc(p,&p->threads[next]) == 0){
    return -1;
  }
  p->num_threads++;

  // Copy user memory from parent to child.
  if(uvmcopy(t->pagetable, p->threads[next].pagetable, t->sz) < 0){
    //freeproc(np);
    //release(&np->lock);
    return -1;
  }
  p->threads[next].sz = t->sz;

  // copy saved user registers.
  *(p->threads[next].trapframe) = *(t->trapframe);

  // Cause fork to return 0 in the child.
  p->threads[next].trapframe->a0 = 0;

  // increment reference counts on open file descriptors.
  for(i = 0; i < NOFILE; i++)
    if(p->threads[next].ofile[i])
      p->threads[next].ofile[i] = filedup(t->ofile[i]);
  p->threads[next].cwd = idup(t->cwd);

  safestrcpy(p->threads[next].name, t->name, sizeof(t->name));

  int tid = p->threads[next].tid;


  acquire(&wait_lock);
  p->threads[next].parent = t;
  release(&wait_lock);

  printf("calling fork setting state runnable\n");
  //acquire(&np->lock);
  p->threads[next].state = RUNNABLE;
  printf("np->state set to RUNNABLE at line 373\n");
  //release(&np->lock);

  return tid;
}

// Pass p's abandoned children to init.
// Caller must hold wait_lock.
void
reparent(struct proc *p)
{
  printf("calling reparent\n");
  struct proc *pp;

  for(pp = proc; pp < &proc[NPROC]; pp++){
    if(pp->parent == p){
      pp->parent = initproc;
        printf("calling wakeup from reparent\n");
      wakeup(initproc);
    }
  }
}

// Exit the current THREAD.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait().
void
exit(int status)
{
  struct sthread *t = mythread();
  struct proc *p = myproc();
  printf("calling exit on proc %d",t->tid);

  if(p == initproc)
    printf("init exiting");

  // Close all open files.
  for(int fd = 0; fd < NOFILE; fd++){
    if(t->ofile[fd]){
      struct file *f = t->ofile[fd];
      fileclose(f);
      t->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(t->cwd);
  end_op();
  t->cwd = 0;

  acquire(&wait_lock);

  // Give any children to init.
  reparent(p);

  // Parent might be sleeping in wait().
  printf("calling wakeup from exit\n");
  wakeup(t->parent);
  
  acquire(&p->lock);

  t->xstate = status;
  t->state = ZOMBIE;

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
  struct proc *pp;
  int havekids, pid;
  struct proc *p = myproc();
  struct sthread *t = mythread();

  acquire(&wait_lock);

  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(pp = proc; pp < &proc[NPROC]; pp++){
      if(pp->parent == p){
        // make sure the child isn't still in exit() or swtch().
        acquire(&pp->lock);

        havekids = 1;
        if(pp->state == ZOMBIE_PROC){
          // Found one.
          pid = pp->pid;
          if(addr != 0 && copyout(t->pagetable, addr, (char *)&pp->xstate,
                                  sizeof(pp->xstate)) < 0) {
            release(&pp->lock);
            release(&wait_lock);
            return -1;
          }
          freeproc(pp);
          release(&pp->lock);
          release(&wait_lock);
          return pid;
        }
        release(&pp->lock);
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || killed(p)){
      release(&wait_lock);
      return -1;
    }
    
    // Wait for a child to exit.
    sleep(p, &wait_lock);  //DOC: wait-sleep
  }
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
  struct cpu *c = mycpu();
  
  c->proc = 0;
  c->thread = 0;
  for(;;){
    // Avoid deadlock by ensuring that devices can interrupt.
    intr_on();
    
    for(p = proc; p < &proc[NPROC]; p++) {
      //printf("scheduler starting search..\n");
      acquire(&p->lock);
      
      //we need to check if both the process is runnable and it has a runnable thread
      if(p->state == USED) {
        // Switch to chosen process.  It is the process's job
        // to release its lock and then reacquire it
        // before jumping back to us.
        //Examine threads within process
        //printf("found runnable process %d\n",p->pid);
        int noRunnableThread = 1;
        
        for(int i = 0; i<p->num_threads;i++){
          printf("scheduler checking threads\n");
          struct sthread *t = &p->threads[i];
          if((t->state) == RUNNABLE){
          c->proc = p;
          noRunnableThread = 0;
          printf("found runnable thread %d state = %d\n",t->tid,t->state);
          t->state = RUNNING;
          c->thread = t;
          //p->current_thread = i;
          printf("switching context\n");
          //after switch, running process will lock
          swtch(&c->context, &t->context);
          //after it stops running, it will lock
          
          //we shouldn't need to do this
          //t->state = RUNNABLE;
          }
          else{
            printf("thread %d is not runnable, has state %d\n",t->tid,t->state);
          }
        }

        // Process is done running for now.
        // It should have changed its p->state before coming back.
        if(noRunnableThread == 0){
          c->thread = 0;
          printf(" thread done\n");
          printf("process done\n");
          c->proc = 0;
        }
        else{
          //printf("process has no runnable threads, moving on \n");
        }
      }
      else{
        //printf("proc %d pid %d is not runnable, has state %d\n",p,p->pid,p->state);
      }
      //unlock the process that was locked during the yield
      release(&p->lock);
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
//on-demand reschedule, used when we wait() (yield)
void
sched(void)
{
  int intena;
  struct proc *p = myproc();
  struct sthread *t = mythread();
  printf("calling sched \n");

  if(!holding(&p->lock))
    panic("sched p->lock");
  if(mycpu()->noff != 1)
    panic("sched locks");
  // if(p->state == RUNNING)
  //   panic("sched running");
  if(t->state == RUNNING)
    panic("thread running in sched");
  if(intr_get())
    panic("sched interruptible");

  intena = mycpu()->intena;
  swtch(&t->context, &mycpu()->context);
  mycpu()->intena = intena;
  printf("finished calling sched \n");
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  printf("calling yield \n");
  struct proc *p = myproc();
  struct sthread *t = mythread();
  acquire(&p->lock);
  printf("resetting thread state to runnable \n");
  t->state = RUNNABLE;
  printf("t->state set to RUNNABLE at line 699\n");
  sched();
  release(&p->lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch to forkret.
void
forkret(void)
{
  printf("here at forkret\n");
  printf("here at forkret\n");
  printf("here at forkret\n");
  printf("here at forkret\n");
  printf("here at forkret\n");
  printf("here at forkret\n");
    printf("here at forkret\n");
  printf("here at forkret\n");
  printf("here at forkret\n");
  static int first = 1;

  // Still holding p->lock from scheduler.
  release(&myproc()->lock);

  if (first) {
    printf("first process calling fsinit\n");
    // File system initialization must be run in the context of a
    // regular process (e.g., because it calls sleep), and thus cannot
    // be run from main().
    first = 0;
    fsinit(ROOTDEV);
  }

  printf("calling  usertrapret from forkret\n");
  usertrapret();
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
// Threads sleep when they are waiting for some console input
// and are woken up via "wakeup" when this input comes into chan
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();
  struct sthread *t = mythread();

  // Must acquire p->lock in order to
  // change p->state and then call sched.
  // Once we hold p->lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup locks p->lock),
  // so it's okay to release lk.

  acquire(&p->lock);  //DOC: sleeplock1
  release(lk);

  // Go to sleep.
  t->chan = chan;
  printf("t/p set to SLEEPING in SLEEP() with channel %d\n",chan);
  t->state = SLEEPING;
  
  
  printf("calling sched from sleep. t-> state is %d\n",t->state);
  sched();

  // Tidy up.
  t->chan = 0;

  // Reacquire original lock.
  release(&p->lock);
  acquire(lk);
}

// Wake up all processes sleeping on chan.
// Must be called without any p->lock.
void
wakeup(void *chan)
{
  printf("calling WAKEUP() on channel %d\n",chan);
  struct proc *p;

  //the only runnable proc is my proc?

  for(p = proc; p < &proc[NPROC]; p++) {
      if(p->state == USED){
        for(int i = 0;i<p->num_threads;i++){
          //if im sleeping and on chan (meaning my process is ready)
          //acquire(&p->lock);
          printf("proc has %d threads, threads channel is %d\n",p->num_threads, p->threads[i].chan);
          if(mythread() != &p->threads[i] && p->threads[i].state == SLEEPING && p->threads[i].chan == chan){
            printf("in wakeup, switching proc %d thread %d state\n",p->pid, p->threads[i].tid);
            p->threads[i].state = RUNNABLE;
            printf("t->state set to RUNNABLE at line 771. State is %d\n",p->threads[i].state);
            //release(&p->lock);

        }
      }
    }
  //printf("looping on proc %d\n",p);
}
printf("finished wakeup\n");
}
// Kill the process with the given pid.
// The victim won't exit until it tries to return
// to user space (see usertrap() in trap.c).
int
kill(int pid)
{
  struct proc *p;

  printf("calling kill\n");
  for(p = proc; p < &proc[NPROC]; p++){
    acquire(&p->lock);
    if(p->pid == pid){
      p->killed = 1;
      for(int i = 0;i<p->num_threads;i++){
      if(p->threads[i].state == SLEEPING){
        // Wake process from sleep().
        p->threads[i].state = RUNNABLE;
        printf("t->state set to RUNNABLE at line 771\n");
      }
      
    }
    release(&p->lock);
    return 0;
    }
  
  release(&p->lock);
}
return -1;
}

void
threadsetkilled(struct sthread *t)
{
  acquire(&t->lock);
  t->killed = 1;
  release(&t->lock);
}


int
threadkilled(struct sthread *t)
{
  int k;
  
  acquire(&t->lock);
  k = t->killed;
  release(&t->lock);
  return k;
}

void
setkilled(struct proc *p)
{
  acquire(&p->lock);
  p->killed = 1;
  release(&p->lock);
}

int
killed(struct proc *p)
{
  int k;
  
  acquire(&p->lock);
  k = p->killed;
  release(&p->lock);
  return k;
}

// Copy to either a user address, or kernel address,
// depending on usr_dst.
// Returns 0 on success, -1 on error.
int
either_copyout(int user_dst, uint64 dst, void *src, uint64 len)
{
  //struct proc *p = myproc();
  struct sthread *t = mythread();
  if(user_dst){
    return copyout(t->pagetable, dst, src, len);
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
    return copyin(p->threads[p->current_thread].pagetable, dst, src, len);
  } else {
    memmove(dst, (char*)src, len);
    return 0;
  }
}

// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  printf("calling procdump\n");
  static char *states[] = {
  [UNUSED]    "unused",
  [USED]      "used",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
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
