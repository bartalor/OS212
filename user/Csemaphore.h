struct counting_semaphore {
  int bid1;
  int bid2;
  int val;
};

int csem_alloc(struct counting_semaphore *sem, int initial_value); 
void csem_free(struct counting_semaphore *sem);  
void csem_down(struct counting_semaphore *sem);  
void csem_up(struct counting_semaphore *sem);  