#include "Csemaphore.h"
#include "kernel/types.h"
#include "user/user.h"


int csem_alloc(struct counting_semaphore *sem, int initial_value) {

  if(sem == 0)
    return -1;

  if((sem->bid1 = bsem_alloc()) == 0)
    return -1;

  if((sem->bid2 = bsem_alloc()) == 0)
    return -1;

  sem->val = initial_value;
  return 0;
}

void csem_free(struct counting_semaphore *sem)  {
  if(sem == 0)  {
    printf("csem_free: sem not allocated.\n");
    return;
  }

  bsem_free(sem->bid1);
  bsem_free(sem->bid2);

  sem->bid1 = 0;
  sem->bid2 = 0;
}

void csem_down(struct counting_semaphore *sem)  {
  if(sem == 0)  {
    printf("csem_down: sem not allocated.\n");
    return;
  }

  bsem_down(sem->bid2);
  bsem_down(sem->bid1);
  sem->val--;
  if(0 < sem->val)
    bsem_up(sem->bid2);
  bsem_up(sem->bid1);

}

void csem_up(struct counting_semaphore *sem)  {
  if(sem == 0)  {
    printf("csem_down: sem not allocated.\n");
    return;
  }

  bsem_down(sem->bid1);
  sem->val++;
  if(sem->val == 1)
    bsem_up(sem->bid2);
  bsem_up(sem->bid1);

}