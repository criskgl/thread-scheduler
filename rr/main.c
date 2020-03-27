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

  return 0;
} /****** End main() ******/


