#include <stdio.h>
#include <sys/time.h>
#include <signal.h>
#include <stdlib.h>
#include <ucontext.h>
#include <unistd.h>

#include "mythread.h"


//Each thread executes this function
extern void function_thread(int sec);
extern void function_thread_eject(int sec);


int main(int argc, char *argv[])
{
  int j,k,l,a,b;

  mythread_setpriority(LOW_PRIORITY);
  if((j = mythread_create(function_thread,HIGH_PRIORITY, 1)) == -1){
      printf("thread failed to initialize\n");
      exit(-1);
  }

  if((k = mythread_create(function_thread,HIGH_PRIORITY, 0.5f)) == -1){
    printf("thread failed to initialize\n");
    exit(-1);
  }

  if((l = mythread_create(function_thread,HIGH_PRIORITY, 0.5f)) == -1){
    printf("thread failed to initialize\n");
    exit(-1);
  }

  for (a=0; a<10; ++a) {
    for (b=0; b<30000000; ++b);
  }
  
  mythread_exit();	
  
  printf("This program should never come here\n");
  
  return 0;
} /****** End main() ******/


