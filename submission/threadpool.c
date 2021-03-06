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

/**
 * Holds the threadpool and the current worker to be passed in to function
 * worker_function() so that this function can do future execution and
 * future stealing logic.
 */
struct thread_pool_and_current_worker {
    pthread_mutex_t lock;
	struct thread_pool *pool;
	struct worker *worker;
};

// private functions for this class that must be declared here to be called below
static void * worker_function(void *pool_and_worker_arg);
static void worker_free(struct worker *worker);
static void exception_exit(char *msg);

/**
 * Each thread has this local variable. Even though it is declared like a 
 * global variable it is NOT. 
 * NOTE: remember that there is already 1 thread running the main code besides
 *       the worker threads you create in thread_pool_new().
 */
static __thread bool is_worker; 

typedef enum FutureStatus_ {
    NOT_STARTED,
    COMPLETED
} FutureStatus;

/**
 * Represents a task that needs to be done. Contains fields need to execute
 * this future and move it around within the global queue and each worker
 * threads local deque and store it's result.
 */
struct future {
    pthread_mutex_t f_lock;
    void *param_for_task_fp; 
    fork_join_task_t task_fp; // pointer to the function to be executed by worker

    void *result;
    sem_t result_sem; // for if result is finished computing

    FutureStatus status; // NOT_STARTED or COMPLETED

    struct thread_pool *p_pool; // must be passed as an parameter in future_get()
                                // to be able to execute future->task_fp 

    struct list_elem gs_queue_elem; // for adding to gs_queue
    struct list_elem deque_elem; // for adding to local deque of each worker
};

struct worker {
    pthread_t *thread_id;

    struct list/*<future>*/ local_deque;
    pthread_mutex_t local_deque_lock;

    struct list_elem elem;
};

struct thread_pool {
    pthread_mutex_t gs_queue_lock;
    struct list/*<future>*/ gs_queue; // global submission queue
   
    pthread_mutex_t shutdown_requested_lock;
    bool shutdown_requested; 
    sem_t number_of_futures_to_execute;

    unsigned int number_of_workers;
    struct list/*<future>*/ workers_list;
};

/**
 * @param nthreads = number of worker threads to create for this threadpool
 */
struct thread_pool * thread_pool_new(int nthreads) 
{
    fprintf(stdout, "> called %s(%d)\n", "thread_pool_new", nthreads);

	is_worker = false; // worker_function() sets it to true

    struct thread_pool* pool = (struct thread_pool*) malloc(sizeof(struct thread_pool));
    if (pool == NULL) { fprintf(stdout, "%s() malloc() error\n", "thread_pool_new"); } 

    pthread_mutex_init(&pool->gs_queue_lock, NULL);
    list_init(&pool->gs_queue);    
    
    pthread_mutex_init(&pool->shutdown_requested_lock, NULL);
    pool->shutdown_requested = false;
    sem_init(&pool->number_of_futures_to_execute, 0, 0);
    pool->number_of_workers = nthreads;

    // Initialize workers list
    list_init(&pool->workers_list);
    int i;
    for(i = 0; i < nthreads; i++) {
        struct worker *worker = (struct worker*) malloc(sizeof(struct worker));
        worker->thread_id = (pthread_t *) malloc(sizeof(pthread_t));
        if (worker == NULL || worker->thread_id == NULL) { fprintf(stdout, "%s()  malloc error\n", "thread_pool_new"); }
        list_init(&worker->local_deque); 
        pthread_mutex_init(&worker->local_deque_lock, NULL);
        list_push_back(&pool->workers_list, &worker->elem);
    }

    // to be passed as a parameter to worker_function()
    struct thread_pool_and_current_worker *worker_fn_args = (struct thread_pool_and_current_worker *) 
                malloc(sizeof(struct thread_pool_and_current_worker));
    pthread_mutex_init(&worker_fn_args->lock, NULL);
    pthread_mutex_lock(&worker_fn_args->lock);
    worker_fn_args->pool = pool; 

    struct list_elem *e;
    for (e = list_begin(&pool->workers_list); e != list_end(&pool->workers_list); e = list_next(e)) {
        struct worker *current_worker = list_entry(e, struct worker, elem);

    	worker_fn_args->worker = current_worker;  

        if (pthread_create(current_worker->thread_id, NULL, worker_function, worker_fn_args) != 0) {
            fprintf(stdout, "%s() PTHREAD_CREATE ERROR\n", "pthread_create");
        } 
    }
    pthread_mutex_unlock(&worker_fn_args->lock);
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
    fprintf(stdout, "> called %s(pool)\n", "thread_pool_shutdown_and_destroy");

	assert(pool != NULL);
    pthread_mutex_lock(&pool->shutdown_requested_lock);
    if (pool->shutdown_requested) { // already called
        return; 
    } else {
        pool->shutdown_requested = true; 
    }
    pthread_mutex_unlock(&pool->shutdown_requested_lock);

    int i;
    for (i = 0; i < pool->number_of_workers; i++) {
        sem_post(&pool->number_of_futures_to_execute);
    }

    // Join all worker threads
    struct list_elem *e;
    for (e = list_begin(&pool->workers_list); e != list_end(&pool->workers_list); e = list_next(e)) {
        
        fprintf(stdout, "> in %s() inside workers_list loop BEFORE JOIN\n", "thread_pool_shutdown_and_destroy");
        struct worker *current_worker = list_entry(e, struct worker, elem);
        pthread_join(*current_worker->thread_id, NULL);
        fprintf(stdout, ">> in %s() inside workers_list loop JOIN SUCCESS\n", "thread_pool_shutdown_and_destroy");
        worker_free(current_worker);
    }

    if (pthread_mutex_destroy(&pool->gs_queue_lock) != 0) { fprintf(stdout, "pthread_mutex_destroy() error prob. still locked!\n"); }
    free(pool);
    return;
}

struct future * thread_pool_submit(struct thread_pool *pool,
                                   fork_join_task_t task,
                                   void * data)
{
    fprintf(stdout, ">> called %s(pool, task, data) on thread_id = %d\n", "thread_pool_submit", (int) pthread_self());
    
    if (pool == NULL) { fprintf(stdout, "%s(pool, task, data) error pool = NULL\n", "thread_pool_submit"); }
    if (task == NULL) { fprintf(stdout, "%s(pool, task, data) error task = NULL\n", "thread_pool_submit");}
    // --------------------- Initialize Future struct --------------------------
    struct future *p_future = (struct future*) malloc(sizeof(struct future));
    pthread_mutex_init(&p_future->f_lock, NULL);
    //pthread_mutex_lock(&p_future->f_lock);
    p_future->param_for_task_fp = data;
    p_future->task_fp = task; 
    p_future->result = NULL;
    sem_init(&p_future->result_sem, 0, 0);
    p_future->status = NOT_STARTED;
    //pthread_mutex_unlock(&p_future->f_lock);
    // -------------------------------------------------------------------------

    fprintf(stdout, ">> in thread_pool_submit(): is_worker = %d\n", is_worker);

    // If this thread is not a worker, add future to global queue (external submission)
    if (!is_worker) {

    	// Acquire lock for the global submission queue 
    	pthread_mutex_lock(&pool->gs_queue_lock);
	    list_push_back(&pool->gs_queue, &p_future->gs_queue_elem);
        sem_post(&pool->number_of_futures_to_execute);
	    pthread_mutex_unlock(&pool->gs_queue_lock);
	} 
    else { // internal submission by worker thread       
        // add to the top of local_deque of the worker thread calling the thread_pool_submit()
        pthread_t this_thread_id = pthread_self();
        // loop through pool's worker_list to find the worker struct with this thread's tid

        struct list_elem *e;
        for (e = list_begin(&pool->workers_list); e != list_end(&pool->workers_list); e = list_next(e)) {
            struct worker *current_worker = list_entry(e, struct worker, elem);
            if (*current_worker->thread_id == this_thread_id) {
                pthread_mutex_lock(&current_worker->local_deque_lock);
                // internal submissions (futures) added to top of local deque                
                list_push_front(&current_worker->local_deque, &p_future->deque_elem);
                sem_post(&pool->number_of_futures_to_execute);
                pthread_mutex_unlock(&current_worker->local_deque_lock);                            
            }
        }
	}
	return p_future;
}

void * future_get(struct future *f) 
{
    fprintf(stdout, "> called %s(f)\n", "future_get");

    if (is_worker) { // internal worker threads
        pthread_mutex_lock(&f->f_lock);
        if (f->status == COMPLETED) {
            pthread_mutex_unlock(&f->f_lock);
            return f->result;
        }
        // Below if statement is for when the threadpool has 1 thread and 
        // multiple futures. You cannot just simply call sem_wait() here
        // because if 1 worker thread calls sem_post() on the first future
        // and the first future generated 2 other futures then the 1 thread
        // would execute 1 of the 2 generated futures and then deadlock.
        else if (f->status == NOT_STARTED) {
            pthread_mutex_unlock(&f->f_lock);
            // Execute task in worker thread      
            void *result = (*(f->task_fp))(f->p_pool, f->param_for_task_fp);

            pthread_mutex_lock(&f->f_lock);
            f->result = result;
            f->status = COMPLETED;
            pthread_mutex_unlock(&f->f_lock);
            sem_post(&f->result_sem); // increment_and_wake_a_waiting_thread_if_any()
            return f->result;
        } else {
            pthread_mutex_unlock(&f->f_lock);
            return f->result;
        }
    } 
    else { // external threads 
        // External threads always block here
        sem_wait(&f->result_sem);
        // when the value is incremented by worker_function the result will be 
        // computed and returned
        return f->result;
    }
}

void future_free(struct future *f) 
{
    fprintf(stdout, "> called %s(f)\n", "future_free");
    if (f == NULL) { exception_exit("future_free() called with NULL parameter"); }
    pthread_mutex_destroy(&f->f_lock); //Why not working?
    sem_destroy(&f->result_sem);
    free(f);
}

/**
 * This is the logic for how a worker thread decides to execute a 
 * task.
 */
static void * worker_function(void *pool_and_worker_arg) 
{
    fprintf(stdout, ">>> called %s()\n", "worker_function");

	is_worker = true; // = thread local variable
    struct thread_pool_and_current_worker *pool_and_worker = (struct thread_pool_and_current_worker *) pool_and_worker_arg;
    pthread_mutex_lock(&pool_and_worker->lock);
	struct thread_pool *pool = pool_and_worker->pool;
	struct worker *worker = pool_and_worker->worker;
    pthread_mutex_unlock(&pool_and_worker->lock);
            
    // The worker thread checks three potential locations for futures to execute 
	while (true) {
        // check if threadpool has been shutdown
        pthread_mutex_lock(&pool->shutdown_requested_lock);
        bool locked = true;
        if (pool->shutdown_requested) {
            pthread_mutex_unlock(&pool->shutdown_requested_lock);
            locked = false;    

            fprintf(stdout, ">>> about to call %s(NULL)\n", "pthread_exit");
            pthread_exit(NULL);
        }
        if (locked) {
            pthread_mutex_unlock(&pool->shutdown_requested_lock);
        }

        // 1) Checks its own local deque first 
        pthread_mutex_lock(&worker->local_deque_lock);
		if (!list_empty(&worker->local_deque)) {
            fprintf(stdout, ">>>> about to execute future in local deque\n");
			struct future *future = list_entry(list_pop_front(&worker->local_deque), struct future, deque_elem);
			pthread_mutex_unlock(&worker->local_deque_lock);

            void *result = (*(future->task_fp))(pool, future->param_for_task_fp);  /// execute future task  

            pthread_mutex_lock(&future->f_lock); // TODO: do I need to lock before executing task_fp?   
			future->result = result;
            future->status = COMPLETED;            
            pthread_mutex_unlock(&future->f_lock);  
            sem_post(&future->result_sem); // increment_and_wake_a_waiting_thread_if_any() 

            continue; // there might be another future in local deque to execute        
		} 
        // 'if' must be false to get to this point. When 'if' true, releases lock. 
        pthread_mutex_unlock(&worker->local_deque_lock);   // fails with EPERM if not owner

        // 2) Check for futures in global threadpool queue 
        pthread_mutex_lock(&pool->gs_queue_lock);
		if (!list_empty(&pool->gs_queue)) {
            fprintf(stdout, ">>>> about to steal future from global submission queue\n");
			struct future *future = list_entry(list_pop_front(&pool->gs_queue), struct future, gs_queue_elem);
			pthread_mutex_unlock(&pool->gs_queue_lock);

            pthread_mutex_lock(&worker->local_deque_lock);
            list_push_front(&worker->local_deque, &future->deque_elem);
            pthread_mutex_unlock(&worker->local_deque_lock);
            continue; // there might be another future in global submission queue to execute   
		} 
        pthread_mutex_unlock(&pool->gs_queue_lock);

        // 3) The worker attempts to steal a future to work on from the bottom of other threads' deques 
        struct list_elem *e;
        bool stole_a_future = false;
        for (e = list_begin(&pool->workers_list); e != list_end(&pool->workers_list); e = list_next(e)) {
            if (stole_a_future) {
                break;
            }
            struct worker *other_worker = list_entry(e, struct worker, elem);

            // steal future from bottom of their deque, if they have any futures
            pthread_mutex_lock(&other_worker->local_deque_lock);

            if (!list_empty(&other_worker->local_deque)) {
                fprintf(stdout, ">>>> about to steal future from another worker's deque\n");
                struct future *stolen_future = list_entry(list_pop_back(&other_worker->local_deque), struct future, deque_elem);
                pthread_mutex_unlock(&other_worker->local_deque_lock);
                stole_a_future = true;

                // now add this stolen future to the current worker's local deque
                pthread_mutex_lock(&worker->local_deque_lock);
                list_push_front(&worker->local_deque, &stolen_future->deque_elem);
                pthread_mutex_unlock(&worker->local_deque_lock);
            } else {
                pthread_mutex_unlock(&other_worker->local_deque_lock);
            }
        }

        // sem_wait(threadpool->semaphore)
        sem_wait(&pool->number_of_futures_to_execute);
	}
    return NULL;
}

static void worker_free(struct worker *worker)
{
    fprintf(stdout, "> called %s(f)\n", "worker_free");
    assert(worker != NULL);
    pthread_mutex_destroy(&worker->local_deque_lock); // Causing problem when run with helgrind
    free(worker);
}

static void exception_exit(char *msg)
{
    //fprintf(stderr, "%s\n", msg);
    exit(EXIT_FAILURE);
}