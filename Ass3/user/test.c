
// includes:
  #include "kernel/param.h"
  #include "kernel/types.h"
  #include "user/user.h"
  #include "kernel/fcntl.h"
  #include "kernel/syscall.h"
  #include "kernel/stat.h"

// signatures:
  void singlePageFault(void);
  void test_restore_from_disk(void);
  void test_priority(void);
  void test_max_page_allocation(void);
  void test_alloc_dealloc(void);
  void fork_test(void);
  
// test numbers:
  #define TEST_SINGLE_PAGE_FAULT 0
  #define TEST_RESTORE_FROM_DISK 1
  #define TEST_PRIORITY 2
  #define TEST_MAX_PAGE_ALLOCATION 3
  #define TEST_ALLOC_DEALLOC 4
  #define TEST_FORK_TEST 5
  #define CURRENT_TEST TEST_FORK_TEST

// more helpful defines:
  #define PGSIZE 4096
  #define PGROUNDDOWN(a) (((a)) & ~(PGSIZE-1))
  #define N_PAGES 24
  #define PAGES_COUNT 14
  char* data[PAGES_COUNT];

// Tests: 
  
   void singlePageFault() {
    void * mem;
    char *a;

    mem = sbrk(0);
    printf("my old size is: %p\n", mem);
    mem = sbrk(17*PGSIZE);
    mem = sbrk(0);
    printf("my new size is: %p\n", mem);

    a = (char*)(5*PGSIZE);
    if(*a == 0)
      printf("");
    printf("no pagefault should have been thus far.\n");

    a = (char*)(3*PGSIZE);
    if(*a == 0)
      printf("");
    printf("pagefault should have already occured by that point.\n");

    exit(0);
  }

  void test_restore_from_disk(void) {
    void * mem;
    int *add;
    mem = sbrk(5*PGSIZE);
    printf("before stroing in ram:\n");
    // print_pages_detailes();
    add = (int*)(6*PGSIZE); // check address in memory
    *add = 666;
    mem = sbrk(12*PGSIZE); // addres 3*PGSIZE should be swapped out
    mem = sbrk(0); 
    printf("my new size is: %p\n", mem);
    printf("after sending to disk:\n");
    printf("this should be 666: %d\n", (int)(*add));// print_pages_detailes();

    exit(0);
  }

  //tests the priority of the algorithms - must print also from kernel (debug in param.h should be 1)
  void test_priority()  {
    int i = 0;
    int n = PAGES_COUNT;
    for (i = 0; i < n ;)
    {

      data[i] = sbrk(4096);
      data[i][0] = 00 + i;
      data[i][1] = 10 + i;
      data[i][2] = 20 + i;
      data[i][3] = 30 + i;
      data[i][4] = 40 + i;
      data[i][5] = 50 + i;
      data[i][6] = 60 + i;
      data[i][7] = 70 + i;
      fprintf(1, "allocated new page #%d at address: %x\n", i, data[i]);
      i++;
      
    }

    fprintf(1,"\nIterate through pages seq:\n");

    int j;
    int k;
        for(k = 0; k <3; k++){
          for(j = 1; j < n + 1; j++){   
            fprintf(1,"j:  %d\n",j);
              for(i = 0; i < j; i++) {
                data[i][10] = 2; // access to the i-th page
                fprintf(1,"%d, ",i);
              }
            fprintf(1,"\n");
        }
    }
    
    data[17] = sbrk(PGSIZE);
      for(k = 0; k <3; k++){
        for(j = 1; j < n + 1; j++){   
            fprintf(1,"j:  %d\n",j);
            for(i = 0; i < j; i++) {
              data[i][10] = 2; // access to the i-th page
              fprintf(1,"%d, ",i);
            }
            fprintf(1,"\n");
       }
    }
    exit(0);
  }

  void test_max_page_allocation(){
     void * mem;
     int i;
    mem = sbrk(0);
    printf("my current size is: %p\n", mem);
    for(i = 5; i < 33; i++){
      sbrk(PGSIZE);
      mem = sbrk(0);
      printf("allocating page %d my current size is: %p\n", i, mem);
    }
    exit(0);
  }

   void test_alloc_dealloc(){
     void * mem;
     int i;
    mem = sbrk(0);
    printf("my current size is: %p\n\n", mem);
    printf("---allocating pages---\n\n");
    for(i = 5; i <= 17; i++){
      sbrk(PGSIZE);
      mem = sbrk(0);
      printf("allocating page %d my current size is: %p\n", i, mem);
    }

    printf("\n\n---deallocating pages--- \n\n");

     for(i = 17; i >= 5; i--){
      sbrk(-PGSIZE);
      mem = sbrk(0);
      printf("de-allocating page %d my current size is: %p\n", i, mem);
    }
    exit(0);
  }

//SELECTION = SCFIFO
void fork_test(){

    void * mem;
    char *a;
    printf("success if order is: 1->2->3->4->5->6->7\n\n");
    mem = sbrk(0);
    printf("1. my old size is: %p\n", mem);
    mem = sbrk(17*PGSIZE);
    mem = sbrk(0);
    printf("2. my new size is: %p\n", mem);
    if(fork()){
      wait(0);
      printf("6. my child done handling page fault\n");
      printf("7. exit...\n");
    }

    else{
      printf("3. i am the child\n");
        a = (char*)(5*PGSIZE);
    if(*a == 0)
      printf("");
    printf("4. no pagefault should have been thus far.\n");

    a = (char*)(3*PGSIZE);
    if(*a == 0)
    printf("");
    printf("5. child done handling pagefault\n");
    }

    exit(0);

}

static void (*tests[])(void) = {
  [TEST_SINGLE_PAGE_FAULT]  singlePageFault,
  [TEST_RESTORE_FROM_DISK] test_restore_from_disk,
  [TEST_PRIORITY] test_priority,
  [TEST_MAX_PAGE_ALLOCATION] test_max_page_allocation,
  [TEST_ALLOC_DEALLOC] test_alloc_dealloc, 
  [TEST_FORK_TEST] fork_test
};

int main(int argc, char **argv) {
  tests[CURRENT_TEST]();
  return 0;
}