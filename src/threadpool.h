/**************************************************
 * threadpool.h
 *
 * A work-stealing, fork-join thread pool.
 **************************************************/

#ifndef THREADPOOL_H // if unique value THREADPOOL_H is NOT defined
#define THREADPOOL_H //   define(add) THREADPOOL_H code into file

#include <stdio.h> // printf()
#include <stdlib.h> // malloc()
#include <pthread.h> // pthread_create()
#include "list.h"
#include "threadpool_lib.h"  /* print_error() */

/* 
 * Opaque forward declarations. The actual definitions of these 
 * types will be local to your threadpool.c implementation.
 */
/* A fork-join, work-stealing thread pool. Manages a variable number of threads
   which execute tasks in parallel */
struct thread_pool;
/* The result of a computation */
struct future;

/* Create a new thread pool with no more than n threads. */
struct thread_pool * thread_pool_new(int nthreads);

/* 
 * Shutdown this thread pool in an orderly fashion.  
 * Tasks that have been submitted but not executed may or
 * may not be executed.
 *
 * Deallocate the thread pool object before returning. 
 */
void thread_pool_shutdown_and_destroy(struct thread_pool *);

/* A function pointer representing a 'fork/join' task.
 * Tasks are represented as a function pointer to a
 * function.
 * 'pool' - the thread pool instance in which this task
 *          executes
 * 'data' - a pointer to the data provided in thread_pool_submit
 *
 * Returns the result of its computation.
 */
typedef void * (* fork_join_task_t) (struct thread_pool *pool, void *data);

/* 
 * Submit a fork join task to the thread pool and return a
 * future.  The returned future can be used in future_get()
 * to obtain the result.
 * 'pool' - the pool to which to submit
 * 'task' - the task to be submitted.
 * 'data' - data to be passed to the task's function
 *
 * Returns a future representing this computation.
 */
struct future * thread_pool_submit(struct thread_pool *pool, 
                                   fork_join_task_t task, 
                                   void * data);

/* Make sure that the thread pool has completed the execution
 * of the fork join task this future represents.
 *
 * Returns the value returned by this task.
 */
void * future_get(struct future *f);

/* Deallocate this future.  Must be called after future_get() */
void future_free(struct future *f);


/* Enums, Constants, etc. */

// Replace with bool is_done ? remove if so. Ever in NOT_STARTED?
typedef enum future_status_ {
  NOT_STARTED,
  IN_PROGRESS,
  COMPLETED
} future_status;

#endif