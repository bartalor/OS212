// includes:
    #include "kernel/param.h"
    #include "kernel/types.h"
    #include "user/user.h"
    #include "kernel/fcntl.h"
    #include "kernel/syscall.h"
    #include "kernel/stat.h"

// signatures:
    void test_sigprocmask();
    void firstUserHandler(int signum);
    void TestUserHandler();
    

// test numbers:
  #define TEST_SigProcMask 0
  #define TEST_UserHandler 1
  #define TEST_StopContinue 2
  #define TEST_DoNothing 3
  #define TEST_StopKill 4

  #define Signal_3 3

// current test:
#define CURRENT_TEST TEST_DoNothing
// user handlers:


int wait_sig = 0;
void test_handlerr(int signum){
    printf("moomoo\n");
}

void test_handler(int signum){
    wait_sig = 1;
    printf("Received sigtest\n");
}

void test_thread(){
    printf("Thread is now running\n");
    kthread_exit(0);
}

void signal_test(){
    int pid;
    int testsig;
    testsig=15;
    struct sigaction acti = {test_handlerr, (uint)(1 << 29)};
    struct sigaction act = {test_handler, (uint)(1 << 29)};
    struct sigaction old;
    sigaction(25,&acti,0);

    sigprocmask(0);
    sigaction(testsig, &act, &old);
    if((pid = fork()) == 0){
        while(!wait_sig)
            sleep(1);
        exit(0);
    }
    kill(pid, testsig);
    wait(&pid);
    printf("Finished testing signals\n");
}

// Tests:
  void test_sigprocmask() {
      uint first;
      uint second;
      uint third;

      first = sigprocmask(7);
      second = sigprocmask(14);
      third = sigprocmask(21);

      if (first == 0 && second == 7 && third == 14)
          fprintf(2, "Ok\n");
      else
          fprintf(2, "Bad\n");

      sigprocmask(0);
  }

  void firstUserHandler(int signum) {
    printf("first user handler\n");
    return;
  }

  void secondUserHandler(int signum)  {
    printf("second user handler\n");
    return;
  }

  void thirdUserHandler(int signum)  {
    printf("third user handler\n");
    return;
  }

  void kernelSignalsTest (){
 
    //test_sigprocmask();
    int pid;
    int i = 0;
    pid = fork();
    if(pid == 0){
        while(1){
            for(i = 0; i< 20; i++){
                fprintf(2,"Hi num %d\n", i);
                if(i == 10){
                  kill(getpid(), SIGSTOP);
                  sleep(1);
                }
            }
           
            fprintf(2,"Continue!\n");
            exit(0);
        }
    }

    else{
        int status;
        sleep(6);
        fprintf(2,"I am father and my pid is: %d!\n", getpid());
        fprintf(2,"Going to continue my child with pid: %d!\n", pid);
        kill(pid, SIGCONT);
        wait(&status);
        fprintf(2,"Status of my child %d\n", status);
        exit(0);
    }
}

  void kernelSignalsTestKillVers (){
 
    int pid;
    int i = 0;
    pid = fork();
    if(pid == 0){
        while(1){
            for(i = 0; i< 20; i++){
                fprintf(2,"Hi num %d\n", i);
                if(i == 10){
                  kill(getpid(), SIGSTOP);
                  sleep(1);
                }
            }
           
            fprintf(2,"Continue!\n");
            exit(0);
        }
    }

    else{
        int status;
        sleep(4);
        fprintf(2,"I am father and my pid is: %d!\n", getpid());
        fprintf(2,"Going to Kill my child with pid: %d!\n", pid);
        kill(pid, SIGKILL);
        wait(&status);
        fprintf(2,"Status of my child %d\n", status);
        exit(0);
    }
}

  void TestUserHandler() {
    printf("Test User Handler \n");
    struct sigaction act0 = {
      .sa_handler = &firstUserHandler,
      .sigmask = 3
    };

    struct sigaction act1 = {
      .sa_handler = &secondUserHandler,
      .sigmask = 3
    };

  
     struct sigaction act2 = {
      .sa_handler = &thirdUserHandler,
      .sigmask = 3
    }; 

    struct sigaction actData; 

    sigaction(Signal_3,&act0,0);
    sigaction(Signal_3,&act1,0);
  
    // do some work:
    for(int i=0; i<15; i++) {
      if(i == 5) {
        printf("sending first signal!\n");
        kill(getpid(),Signal_3);
      }
      
      if(i == 10) {
        printf("sending second signal!\n",i);
        kill(getpid(),Signal_3);
      }
      sleep(1);
      printf("finished round %d!\n", i);
    }

    sigaction(Signal_3,&act2,&actData);

   
      // do some work:
    for(int i=0; i<15; i++) {
      if(i == 5) {
        printf("sending third signal!\n");
        kill(getpid(),Signal_3);
      }
      
      if(i == 10) {
        printf("sending fourth signal!\n",i);
        kill(getpid(),Signal_3);
      }
      sleep(1);
      printf("finished round %d!\n", i);
    }

    sigaction(Signal_3,&actData,0);

    for(int i=0; i<15; i++) {
      if(i == 5) {
        printf("sending fifth signal!\n");
        kill(getpid(),Signal_3);
      }
      
      if(i == 10) {
        printf("sending sixth signal!\n",i);
        kill(getpid(),Signal_3);
      }
      sleep(1);
      printf("finished round %d!\n", i);
    }
  }

    void DoNothing() {
    printf("Test Do Nothing \n");
    struct sigaction act0 = {
      .sa_handler = &firstUserHandler,
      .sigmask = 3
    };

    struct sigaction act1 = {
      .sa_handler = &secondUserHandler,
      .sigmask = 3
    };

    printf("Should print signal handler here: \n");
    sigaction(Signal_3,&act0,0);
    sigaction(Signal_3,&act1,0);
    kill(getpid(),Signal_3);
    printf("\n");
    // do some work:

    sigprocmask(3);
    printf("SHOULD NOT print signal handler here: \n");
    sigaction(Signal_3,&act1,0);
    sleep(1);
    

    printf("Test Really did nothing\n\n");
    
  }

// tests array:

static void (*tests[])(void) = {
  [TEST_SigProcMask]  test_sigprocmask,
  [TEST_UserHandler]  TestUserHandler,
  [TEST_StopContinue]  kernelSignalsTest, 
  [TEST_DoNothing]  DoNothing,
  [TEST_StopKill]  kernelSignalsTestKillVers
  
};    

int main(int argc, char **argv) {
  
 tests[CURRENT_TEST]();
  printf("Tests DONE \n"); 
  exit(0);
}