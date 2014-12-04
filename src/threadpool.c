/**************************************************
 * threadpool.c
 *
 * A work-stealing, fork-join thread pool.
 **************************************************/

#include "threadpool.h"
#include <stdio.h> // printf()
#include <stdlib.h> // malloc()
#include <pthread.h> // pthread_create()
#include <semaphore.h> // sem_wait() sem_post()
#include <assert.h>

#include "list.h"

// private functions for this class that must be declared here to be called below
static void * worker_function(void *pool_and_worker_arg);

/**
 * Represents a task that needs to be done. Contains fields need to execute
 * this future and move it around within the global queue and each worker
 * threads local deque and store it's result.
 */
struct future {
    void *param_for_task_fp; 
    fork_join_task_t task_fp; // pointer to the function to be executed by worker

    void *result;
    sem_t result_sem; // for if result is finished computing

    struct list_elem gs_queue_elem; // for adding to gs_queue
};

struct worker {
    pthread_t *thread_id;
    struct list_elem elem;
};

struct thread_pool {
    pthread_mutex_t gs_queue_lock;
    struct list/*<future>*/ gs_queue; // global submission queue
   
    pthread_mutex_t shutdown_requested_lock;
    bool shutdown_requested; 

    unsigned int number_of_workers;
    struct list/*<worker>*/ workers_list;

    pthread_cond_t condition; // TODO:?
};

/**
 * @param nthreads = number of worker threads to create for this threadpool
 */
struct thread_pool * thread_pool_new(int nthreads) 
{
	//is_worker = false; // worker_function() sets it to true

    struct thread_pool* pool = (struct thread_pool*) malloc(sizeof(struct thread_pool));

    pthread_mutex_init(&pool->gs_queue_lock, NULL);
    list_init(&pool->gs_queue);    
    
    pthread_mutex_init(&pool->shutdown_requested_lock, NULL);
    pool->shutdown_requested = false;

    pool->number_of_workers = nthreads;

    // Initialize workers list
    list_init(&pool->workers_list);
    int i;
    for(i = 0; i < nthreads; i++) {
        struct worker *worker = (struct worker*) malloc(sizeof(struct worker));
        worker->thread_id = (pthread_t *) malloc(sizeof(pthread_t));
        
        list_push_back(&pool->workers_list, &worker->elem);
    }

    pthread_cond_init(&pool->condition, NULL);

    struct list_elem *e;
    for (e = list_begin(&pool->workers_list); e != list_end(&pool->workers_list); e = list_next(e)) {
        struct worker *current_worker = list_entry(e, struct worker, elem);

        pthread_create(current_worker->thread_id, NULL, worker_function, pool);
    }
	return pool;
}

/**
 * Shut down the threadpool. Already executing functions complete and
 * queued futures(in the global submission queue or a worker threads local
 * deque) do not complete.
 *
 * The calling thread must join all worker threads before returning.
 */
void thread_pool_shutdown_and_destroy(struct thread_pool *pool) 
{
    pthread_mutex_lock(&pool->shutdown_requested_lock);
    if (pool->shutdown_requested) { // already called
        return; 
    } else {
        pool->shutdown_requested = true; 
    }
    pthread_mutex_unlock(&pool->shutdown_requested_lock);

    if (pthread_cond_broadcast(&pool->condition) != 0) {
        return;
    }

    // Join all worker threads
    struct list_elem *e;
    for (e = list_begin(&pool->workers_list); e != list_end(&pool->workers_list); e = list_next(e)) {
        
        struct worker *current_worker = list_entry(e, struct worker, elem);
        pthread_join(*current_worker->thread_id, NULL);
    }

    pthread_mutex_destroy(&pool->gs_queue_lock); 
    pthread_cond_destroy(&pool->condition);
    free(pool);
    return;
}

struct future * thread_pool_submit(struct thread_pool *pool,
                                   fork_join_task_t task,
                                   void * data)
{    
    pthread_mutex_lock(&pool->shutdown_requested_lock);
    if (pool->shutdown_requested) {
        return NULL;
    }
    pthread_mutex_unlock(&pool->shutdown_requested_lock);

    // Initialize Future struct 
    struct future *future = (struct future*) malloc(sizeof(struct future));
    future->param_for_task_fp = data;
    future->task_fp = task; 
    future->result = NULL;
    sem_init(&future->result_sem, 0, 0);

    pthread_mutex_lock(&pool->gs_queue_lock);
    list_push_back(&pool->gs_queue, &future->gs_queue_elem);
    pthread_mutex_unlock(&pool->gs_queue_lock);

    return future;
}

void * future_get(struct future *f) 
{
    // External threads always block here
    sem_wait(&f->result_sem);
    // when the value is incremented by worker_function the result will be 
    // computed and returned
    return f->result;
}

void future_free(struct future *f) 
{
    free(f);
}

/**
 * This is the logic for how a worker thread decides to execute a 
 * task.
 */
static void * worker_function(void *threadpool) 
{
    struct thread_pool *pool = (struct thread_pool *) threadpool;

	while (true) {
        pthread_mutex_lock(&pool->gs_queue_lock);

        while (list_empty(&pool->gs_queue)) {
            pthread_cond_wait(&pool->condition, &pool->gs_queue_lock);

            // check if threadpool has been shutdown
            pthread_mutex_lock(&pool->shutdown_requested_lock);
            if (pool->shutdown_requested) {
                pthread_mutex_unlock(&pool->gs_queue_lock);
                pthread_exit(NULL);
            }
        }

        struct future *future = list_entry(list_pop_front(&pool->gs_queue), struct future, gs_queue_elem);
        pthread_mutex_unlock(&pool->gs_queue_lock);

        future->result = future->task_fp(pool, future->param_for_task_fp);  
        sem_post(&future->result_sem);
	}
    return NULL;
}
