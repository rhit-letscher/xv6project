// Sleeping locks

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"
#include "sleeplock.h"

extern struct sthread* mythread();


void
initsleeplock(struct sleeplock *lk, char *name)
{
  initlock(&lk->lk, "sleep lock");
  lk->name = name;
  lk->locked = 0;
  lk->pid = 0;
}

void
acquiresleep(struct sleeplock *lk)
{
  printf("ACQUIRESLEEP()\n");
  acquire(&lk->lk);
  while (lk->locked) {
    printf("ACQUIRESLEEP is calling SLEEP() on chan %d from acquiresleep\n",lk);
    sleep(lk, &lk->lk);
  }
  lk->locked = 1;
  lk->pid = mythread()->tid;
  release(&lk->lk);
  printf("ACQUIRESLEEP() DONE\n");

}

void
releasesleep(struct sleeplock *lk)
{
  acquire(&lk->lk);
  lk->locked = 0;
  lk->pid = 0;
  printf("lock %d",lk);
  printf("calling wakeup from sleeplock\n");
  wakeup(lk);
  release(&lk->lk);
}

int
holdingsleep(struct sleeplock *lk)
{
  int r;
  
  acquire(&lk->lk);
  r = lk->locked && (lk->pid == mythread()->tid);
  release(&lk->lk);
  return r;
}



