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
struct queue *rr_queue;
struct queue *sjf_queue;

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
    /* time_t end = time(NULL) + sec; */
    while(running->remaining_ticks)
    {

    }
    mythread_exit();
}

/* For HIGH_PRIORITY only: Forces thread to eject */
void function_thread_eject(int sec)
{
  /* Thread runs out of remaining_ticks */
  while(running->remaining_ticks + 100)
  {

  }
  mythread_exit();
}

/* Initialize the thread library */
void init_mythreadlib() 
{
  int i;
  /* Round Robin queue */
  rr_queue = queue_new();
  /* Shortest Job First queue */
  sjf_queue = queue_new();

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
  
  /* Finds first FREE thread available */
  for (i=0; i<N; i++)
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

  /* Every new thread is enqueued in its corresponding queue */
  if(priority == LOW_PRIORITY){
    disable_interrupt();
    enqueue(rr_queue, &t_state[i]);
    enable_interrupt();
  }else{
    disable_interrupt();
    sorted_enqueue(sjf_queue, &t_state[i], t_state[i].remaining_ticks);
    enable_interrupt();
  }

  printf("*** THREAD %i READY\n", i);

  /* Checks if new thread is higher priority that running thread */
  if(running->priority == LOW_PRIORITY && priority == HIGH_PRIORITY){
    printf("*** THREAD %i PREEMTED : SETCONTEXT OF %i\n", running->tid, i);
    disable_interrupt();
    enqueue(rr_queue, running);
    enable_interrupt();
    TCB* next = scheduler();
    activator(next);
  }
  
  /* Checks if new thread is shorter than running thread (for HIGH_PRIORITY threads only) */
  if(running->priority == HIGH_PRIORITY && priority == HIGH_PRIORITY){
    if(running->remaining_ticks > t_state[i].remaining_ticks){
      disable_interrupt();
      sorted_enqueue(sjf_queue, running, running->remaining_ticks);
      enable_interrupt();
    
      TCB* next = scheduler();
      activator(next);
    }
  }

  return i;
} 
/****** End my_thread_create() ******/

/* Read disk syscall (Not used) */
int read_disk()
{
   return 1;
}

/* Disk interrupt  (Not used) */
void disk_interrupt(int sig)
{

}

/* Free terminated thread and exits */
void mythread_exit() {
  int tid = mythread_gettid();	

  printf("*** THREAD %d FINISHED\n", tid);	
  t_state[tid].state = FREE;
  free(t_state[tid].run_env.uc_stack.ss_sp); 

  TCB* next = scheduler();
  activator(next);
}

/* HIGH_PRIORITY thread thread runs out of remaining_ticks (not used) */
void mythread_timeout(int tid) {
  printf("*** THREAD %d EJECTED\n", tid);
  t_state[tid].state = FREE;
  free(t_state[tid].run_env.uc_stack.ss_sp);

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

/* Get the current thread id. */
int mythread_gettid(){
  if (!init) { init_mythreadlib(); init=1;}
  return current;
}

/* Round Robin + SJF */
TCB* scheduler()
{
  TCB* next;
  if(!queue_empty(sjf_queue)){
    disable_interrupt();
    next = dequeue(sjf_queue);
    enable_interrupt();
  }else{
    disable_interrupt();
    next = dequeue(rr_queue);
    enable_interrupt();
  }
  if(next == NULL){
    printf("*** FINISH\n");	
    exit(1);
  }
  return next;
}

/* Timer interrupt */
void timer_interrupt(int sig)
{
  running->remaining_ticks--;
  running->ticks--;
  
  /* Checks if a HIGH_PRIORITY thread ran out of remaining_ticks */
  if(running->priority == HIGH_PRIORITY && running->remaining_ticks < 0){
    mythread_timeout(running->tid);
  }
  if(running->priority == LOW_PRIORITY && running->ticks == 0){
    /* Updates TCB with new state and quantum_ticks */
    running->ticks = QUANTUM_TICKS;
    running->state = INIT;
    /* Enqueues previous running thread to wait for next execution */
    disable_interrupt();
    enqueue(rr_queue, running);
    enable_interrupt();

    TCB* next = scheduler();
    activator(next); 
  }
} 

/* Activator */
void activator(TCB* next)
{
  /* Error check: Do not swap context for the same thread */
  if(running != next){ 
    TCB* prev = running;
    current = next->tid;
    running = next;
    if(prev->state == FREE){
      /* Activator was called because previous thread had finished executing */
      printf("*** THREAD %i TERMINATED: SETCONTEXT OF %i \n", prev->tid, next->tid);
      setcontext (&(next->run_env));    
      printf("mythread_free: After setcontext, should never get here!!...\n");
    }else{
      /* Activator was called because previous thread ran out of ticks */
      printf("*** SWAPCONTEXT FROM %i TO %i\n", prev->tid, next->tid);
      swapcontext(&(prev->run_env),&(next->run_env));
    }	
  }
}