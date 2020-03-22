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
struct queue *waiting_queue;

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

    }
    mythread_exit();
}

// High priority threads that take longer than remaining_ticks
void function_thread_eject(int sec)
{
  while(running->remaining_ticks + 100)
  {

  }
  mythread_exit();
}

// Threads that need to read from disk
void function_thread_disk(int sec)
{
  read_disk();
  read_disk();
  read_disk();
  read_disk();
  read_disk();
  while(running->remaining_ticks)
  {

  }
  mythread_exit();
}

/* Initialize the thread library */
void init_mythreadlib() 
{
  int i;
  rr_queue = queue_new();//RR queue for LOW_PRIORITY
  sjf_queue = queue_new();//SJF queue for HIGH_PRIORITY
  waiting_queue = queue_new();//Queue for WAITING threads

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

  //Enqueue thread in the corresponding queue depending on priority
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

  if(running->priority == LOW_PRIORITY && priority == HIGH_PRIORITY){
    printf("*** THREAD %i PREEMTED : SETCONTEXT OF %i\n", running->tid, i);
    TCB* next = scheduler();
    activator(next);
  }
  
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

/* Read disk syscall */
int read_disk()
{
  printf("*** THREAD %d READ FROM DISK\n", running->tid);
  // Data is not in cache
  if(!data_in_page_cache()){
    running->state = WAITING;

    disable_interrupt();
    enqueue(waiting_queue, running);
    enable_interrupt();

    TCB* next = scheduler();
    activator(next);
  }
  return 1;
}

/* Disk interrupt  */
void disk_interrupt(int sig)
{
  //Check if there are waiting threads
  if(!queue_empty(waiting_queue)){
    disable_interrupt();
    TCB* waiting_to_ready = dequeue(waiting_queue);
    enable_interrupt();

    waiting_to_ready->state = INIT;

    if(waiting_to_ready->priority == HIGH_PRIORITY){
      disable_interrupt();
      sorted_enqueue(sjf_queue, waiting_to_ready, waiting_to_ready->remaining_ticks);
      enable_interrupt();
    }else{
      disable_interrupt();
      enqueue(rr_queue, waiting_to_ready);
      enable_interrupt();
    }
  }
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

/* Get the current thread id.  */
int mythread_gettid(){
  if (!init) { init_mythreadlib(); init=1;}
  return current;
}

/* Round Robin - SJF */
TCB* scheduler()
{
  TCB* next;
  if(!queue_empty(sjf_queue)){
    disable_interrupt();
    next = dequeue(sjf_queue);
    enable_interrupt();
  }else if(!queue_empty(rr_queue)){
    disable_interrupt();
    next = dequeue(rr_queue);
    enable_interrupt();
  }else if(!queue_empty(waiting_queue)){
    return &idle;
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
  //Beg  For IDLE THREAD only
  if(running->tid == -1){//if idle
    if(!queue_empty(rr_queue) || !queue_empty(sjf_queue)){//if there is any process ready...
      activator(scheduler());
    }
  }
  //End For IDLE THREAD only

  running->remaining_ticks--;
  running->ticks--;
  if(running->priority == HIGH_PRIORITY && running->remaining_ticks < 0){
    //EJECT THREAD
    mythread_timeout(running->tid);
  }
  if(running->priority == LOW_PRIORITY && running->ticks == 0){
    // Update TCB with new state and ticks
    running->ticks = QUANTUM_TICKS;
    running->state = INIT;
    // Enqueue old running thread to wait for next execution
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
  //Do not swap same context 
  if(running != next){ 
    TCB* prev = running;
    current = next->tid;
    running = next;
    if(prev->state == FREE){
      //activator was called because prev thread had finished executing
      printf("*** THREAD %i TERMINATED: SETCONTEXT OF %i \n", prev->tid, next->tid);
      setcontext (&(next->run_env));    
      printf("mythread_free: After setcontext, should never get here!!...\n");
    }else{
      //activator was called because prev thread ran out of time OR is waiting for disk
      printf("*** SWAPCONTEXT FROM %i TO %i\n", prev->tid, next->tid);
      swapcontext(&(prev->run_env),&(next->run_env));
    }	
  }
}