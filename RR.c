#include <stdio.h>
#include <sys/time.h>
#include <signal.h>
#include <stdlib.h>
#include <ucontext.h>
#include <unistd.h>
#include "my_io.h"

//#include "mythread.h"
#include "interrupt.h"

#include "queue.h"

TCB* scheduler();
void activator();
void timer_interrupt(int sig);
void disk_interrupt(int sig);
struct queue *init_queue;


/* Array of state thread control blocks: the process allows a maximum of N threads */
static TCB t_state[N]; 

/* Current running thread */
static TCB* running;
static int current = 0;

/* Variable indicating if the library is initialized (init == 1) or not (init == 0) */
static int init=0;

/* Thread control block for the idle thread */
static TCB idle;

static void idle_function()
{
  while(1);
}

void function_thread(int sec)
{
    //time_t end = time(NULL) + sec;
    while(running->remaining_ticks)
    {
      //do something
    }
    mythread_exit();
}


/* Initialize the thread library */
void init_mythreadlib() 
{
  int i;
  init_queue = queue_new();

  /* Create context for the idle thread */
  if(getcontext(&idle.run_env) == -1)
  {
    perror("*** ERROR: getcontext in init_thread_lib");
    exit(-1);
  }

  idle.state = IDLE;
  idle.priority = SYSTEM;
  idle.function = idle_function;
  idle.run_env.uc_stack.ss_sp = (void *)(malloc(STACKSIZE));
  idle.tid = -1;

  if(idle.run_env.uc_stack.ss_sp == NULL)
  {
    printf("*** ERROR: thread failed to get stack space\n");
    exit(-1);
  }

  idle.run_env.uc_stack.ss_size = STACKSIZE;
  idle.run_env.uc_stack.ss_flags = 0;
  idle.ticks = QUANTUM_TICKS;
  makecontext(&idle.run_env, idle_function, 1); 

  t_state[0].state = INIT;
  t_state[0].priority = LOW_PRIORITY;
  t_state[0].ticks = QUANTUM_TICKS;

  if(getcontext(&t_state[0].run_env) == -1)
  {
    perror("*** ERROR: getcontext in init_thread_lib");
    exit(5);
  }	

  for(i=1; i<N; i++)
  {
    t_state[i].state = FREE;
  }

  t_state[0].tid = 0;
  running = &t_state[0];
  /* Initialize disk and clock interrupts */
  init_disk_interrupt();
  init_interrupt();
}


/* Create and intialize a new thread with body fun_addr and one integer argument */ 
int mythread_create (void (*fun_addr)(),int priority,int seconds)
{
  int i;
  
  if (!init) { init_mythreadlib(); init=1;}

  for (i=0; i<N; i++)//search for next free thread
    if (t_state[i].state == FREE) break;

  if (i == N) return(-1);

  if(getcontext(&t_state[i].run_env) == -1)
  {
    perror("*** ERROR: getcontext in my_thread_create");
    exit(-1);
  }

  t_state[i].state = INIT;
  t_state[i].priority = priority;
  t_state[i].function = fun_addr;
  t_state[i].ticks = QUANTUM_TICKS;
  t_state[i].execution_total_ticks = seconds_to_ticks(seconds);
  t_state[i].remaining_ticks = t_state[i].execution_total_ticks;
  t_state[i].run_env.uc_stack.ss_sp = (void *)(malloc(STACKSIZE));
  
  if(t_state[i].run_env.uc_stack.ss_sp == NULL)
  {
    printf("*** ERROR: thread failed to get stack space\n");
    exit(-1);
  }

  t_state[i].tid = i;
  t_state[i].run_env.uc_stack.ss_size = STACKSIZE;
  t_state[i].run_env.uc_stack.ss_flags = 0;
  makecontext(&t_state[i].run_env, fun_addr,2,seconds);

  //Enqueue thread in Init queue
  disable_interrupt();
  enqueue(init_queue, &t_state[i]);
  enable_interrupt();
  
  

  printf("thread %i has been inserted to queue\n", i);

  return i;
} 
/****** End my_thread_create() ******/


/* Read disk syscall */
int read_disk()
{
   return 1;
}

/* Disk interrupt  */
void disk_interrupt(int sig)
{

}


/* Free terminated thread and exits */
void mythread_exit() {

  printf("*** THREAD %d FINISHED\n", mythread_gettid());	
  if(running->state != IDLE){
    running->state = FREE;
  }
  free(running->run_env.uc_stack.ss_sp); 
  TCB* next = scheduler();
  activator(next);
}


void mythread_timeout() {
    printf("*** THREAD %d EJECTED\n", mythread_gettid());

    // Update TCB with new state and ticks
    running->ticks = QUANTUM_TICKS;
    running->state = INIT;
    // Enqueue old running thread to wait for next execution
    disable_interrupt();
    enqueue(init_queue, running);
    enable_interrupt();
    free(running->run_env.uc_stack.ss_sp);
    
    TCB* next = scheduler();
    activator(next);
}


/* Sets the priority of the calling thread */
void mythread_setpriority(int priority) 
{
  int tid = mythread_gettid();	
  t_state[tid].priority = priority;
  if(priority ==  HIGH_PRIORITY){
    t_state[tid].remaining_ticks = 195;
  }
}

/* Returns the priority of the calling thread */
int mythread_getpriority(int priority) 
{
  int tid = mythread_gettid();	
  return t_state[tid].priority;
}


/* Get the current thread id.  */
int mythread_gettid(){
  if (!init) { init_mythreadlib(); init=1;}
  return current;
}


/* Round Robin */

TCB* scheduler()
{
  disable_interrupt();
  TCB* next = dequeue(init_queue);
  enable_interrupt();
  if(next == NULL){
    printf("mythread_free: No thread in the system\nExiting...\n");	
    exit(1);
  }
  return next;
}


/* Timer interrupt */
void timer_interrupt(int sig)
{
  running->ticks--;
  //printf("remaining ticks: %i\n", running->ticks);
  //printf("running state: %i\nrunning tid: %i\n", running->state, running->tid);
  if(running->ticks == 0){
    running->state = INIT;
    running->ticks = QUANTUM_TICKS;
    disable_interrupt();
    enqueue(init_queue, running);
    enable_interrupt();
    TCB* old_running = running;
    TCB* next = scheduler();
    activator(next);
  }
} 

/* Activator */
void activator(TCB* next)
{
  TCB* prev = running;
  current = next->tid;
  running = next;
  if(prev->state == FREE){//activator was called because prev thread had finished executing
    printf("*** THREAD %i TERMINATED: SETCONTEXT OF %i \n", prev->tid, next->tid);
    setcontext (&(next->run_env));    
  }else{//activator was called because prev thread runned out of time
    printf("*** SWAPCONTEXT FROM %i TO %i\n", prev->tid, next->tid);
    swapcontext(&(prev->run_env),&(next->run_env));
  }
  printf("mythread_free: After setcontext, should never get here!!...\n");	
}