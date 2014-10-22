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
#include "list.h"

#include <errno.h>  // perror()
#include <string.h> // strerror()
 
/* Print error message and exit */
static void error_exit(char *msg, int err_code);
static void exception_exit(char *msg);

/* Wrapper functions with error checking for pthreads and semaphores */

/* malloc */
static void * malloc_c(int size);

/* POSIX Threads (pthread.h) */
// Thread Routines
//static void pthread_create_c(pthread_t *thread, const pthread_attr_t *attr, 
//                     void *(*start_routine)(void *), void *arg);
static void pthread_join_c(pthread_t thread, void **value_ptr);
static pthread_t pthread_self_c(void);
// Mutex Routines
static void pthread_mutex_destroy_c(pthread_mutex_t *mutex);
static void pthread_mutex_init_c(pthread_mutex_t *mutex, const pthread_mutexattr_t *attr);
static void pthread_mutex_lock_c(pthread_mutex_t *mutex);
static void pthread_mutex_unlock_c(pthread_mutex_t *mutex);
// Condition Variable Routines
static void pthread_cond_init_c(pthread_cond_t *cond, const pthread_condattr_t *attr);
static void pthread_cond_destroy_c(pthread_cond_t *cond);
static void pthread_cond_broadcast_c(pthread_cond_t *cond);
static void pthread_cond_wait_c(pthread_cond_t *cond, pthread_mutex_t *mutex);

/* Semaphores (semaphore.h) */
static void sem_init_c(sem_t *sem, int pshared, unsigned int value);
static void sem_destroy_c(sem_t *sem);
static void sem_post_c(sem_t *sem);
static void sem_wait_c(sem_t *sem);


/****************/



/**
 * Holds the threadpool and the current worker to be passed in to function
 * worker_function() so that this function can do future execution and
 * future stealing logic.
 */
struct thread_pool_and_current_worker {
	struct thread_pool *pool;
	struct worker *worker;
};

// private functions for this class that must be declared here to be called below
static void * worker_function(void *pool_and_worker);
static struct worker * worker_init(struct worker * worker, unsigned int index_of_worker);
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
    IN_PROGRESS,
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

    FutureStatus status; // NOT_STARTED, IN_PROGRESS, or COMPLETED

    struct thread_pool *p_pool; // must be passed as an parameter in future_get()
                                // to be able to execute future->task_fp 

    struct list_elem gs_queue_elem; // for adding to gs_queue
    struct list_elem deque_elem; // for adding to local deque of each worker
};

struct worker {
    pthread_t thread_id;

    unsigned int index_of_worker;

    struct list/*<Future>*/ local_deque;
    pthread_mutex_t local_deque_lock;
};

struct thread_pool {
    struct list/*<Future>*/ gs_queue; // global submission queue
    pthread_mutex_t gs_queue_lock;      
    pthread_cond_t gs_queue_has_tasks;  

    bool shutdown_requested; 

    unsigned int number_of_workers;                     
    struct worker *workers; // actual array of workers
};

/**
 * @param nthreads = number of worker threads to create for this threadpool
 */
struct thread_pool * thread_pool_new(int nthreads) 
{
    fprintf(stdout, ">> called %s(%d)\n", "thread_pool_new", nthreads);
	if (nthreads < 1) { exception_exit("thread_pool_new(): must create at least one worker thread"); }

	is_worker = false; // worker_function() sets it to true

	struct thread_pool* pool = (struct thread_pool*) malloc_c(sizeof(struct thread_pool));

    list_init(&pool->gs_queue);    
    pthread_mutex_init_c(&pool->gs_queue_lock, NULL);

    // Initialize condition variable used to broadcast to worker threads that 
    // tasks are available in the global submission queue 
    pthread_cond_init_c(&pool->gs_queue_has_tasks, NULL);

    pool->shutdown_requested = false;

    pool->number_of_workers = nthreads;

    // Initialize workers array
    pool->workers = (struct worker *) malloc_c(nthreads * sizeof(struct worker)); 

    struct worker *p_worker = pool->workers;
	int i;
	for (i = 0; i < nthreads; i++) {
        p_worker[i] = *((struct worker *) malloc_c(sizeof(struct worker)));
        p_worker[i] = *(worker_init( (p_worker + i) , i));
	}

    // to be passed as a parameter to worker_function()
    struct thread_pool_and_current_worker *pool_and_worker = (struct thread_pool_and_current_worker*) 
                malloc_c(sizeof(struct thread_pool_and_current_worker));
    pool_and_worker->pool = pool;

    for (i = 0; i < pool->number_of_workers; i++) {
        struct worker *current_worker = pool->workers + i;

    	pool_and_worker->worker = current_worker;
        void *pool_and_worker2 = pool_and_worker;

        pthread_create(&current_worker->thread_id, NULL, (void *) worker_function, pool_and_worker2);
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
	if (pool == NULL) { exception_exit("thread_pool_shutdown_and_destroy: pool is NULL.\n"); }
    if (pool->shutdown_requested == true) { return; } // already called
    
    pool->shutdown_requested = true;

    // Wake up any sleeping threads prior to exit 
    pthread_cond_broadcast_c(&pool->gs_queue_has_tasks); // QUinn: why?

    int i;
    for (i = 0; i < pool->number_of_workers; i++) {
		//struct worker *worker = list_entry(e, struct worker, elem);
        struct worker *current_worker = pool->workers + i;
		pthread_join_c(current_worker->thread_id, NULL);   // NOTE: the value passed to pthread_exit() by the terminating thread is
                                                            // stored in the location referenced by value_ptr.
        worker_free(current_worker);
	}

    pthread_mutex_destroy_c(&pool->gs_queue_lock);
    pthread_cond_destroy_c(&pool->gs_queue_has_tasks);
    free(pool);
}

struct future * thread_pool_submit(struct thread_pool *pool,
                                   fork_join_task_t task,
                                   void * data)
{
    fprintf(stdout, "called %s(pool, task, data)\n", "thread_pool_submit");

    if (pool == NULL) { exception_exit("thread_pool_submit() pool arg cannot be NULL"); }
    if (task == NULL) { exception_exit("thread_pool_submit() task arg cannot be NULL"); }
    // --------------------- Initialize Future struct --------------------------
    struct future *p_future = (struct future*) malloc(sizeof(struct future));
    pthread_mutex_init_c(&p_future->f_lock, NULL);
    p_future->param_for_task_fp = data;
    p_future->task_fp = task;
    p_future->result = NULL;
    sem_init_c(&p_future->result_sem, 0, 0);
    p_future->status = NOT_STARTED;
    // -------------------------------------------------------------------------

    // If this thread is not a worker, add future to global queue (external submission)
    if (!is_worker) {
    	// Acquire lock for the global submission queue 
    	pthread_mutex_lock_c(&pool->gs_queue_lock);
	    list_push_back(&pool->gs_queue, &p_future->gs_queue_elem);
	    // Broadcast to sleeping threads that future is availabe in global submission queue
	    pthread_cond_broadcast_c(&pool->gs_queue_has_tasks);
	    pthread_mutex_unlock_c(&pool->gs_queue_lock);
	} 
    else { // internal submission by worker thread       
        // add to the top of local_deque of the worker thread calling the thread_pool_submit()
        pthread_t this_thread_id = pthread_self_c();
        // loop through pool's worker_list to find the worker struct with this thread's tid

        int i;
        for (i = 0; i < pool->number_of_workers; i++) {
            struct worker *current_worker = pool->workers + i;
            if (current_worker->thread_id == this_thread_id) {
                pthread_mutex_lock_c(&current_worker->local_deque_lock);
                // internal submissions (futures) added to top of local deque                
                list_push_front(&current_worker->local_deque, &p_future->deque_elem);
                pthread_mutex_unlock_c(&current_worker->local_deque_lock);                            
            }
        }
	}
	return p_future;
}

void * future_get(struct future *f) 
{
    if (f == NULL) {  exception_exit("future_get() called with NULL parameter"); }

    if (is_worker) {
        pthread_mutex_lock_c(&f->f_lock);
        FutureStatus future_status = f->status;
        pthread_mutex_unlock_c(&f->f_lock);

        if (future_status == COMPLETED) {
            return f->result;
        }
        // Below if statement is for when the threadpool has 1 thread and 
        // multiple futures. You cannot just simply call sem_wait() here
        // because if 1 worker thread calls sem_post() on the first future
        // and the first future generated 2 other futures then the 1 thread
        // would execute 1 of the 2 generated futures and then deadlock.
        else if (future_status == NOT_STARTED) {
            // Execute task in worker thread      
            pthread_mutex_lock_c(&f->f_lock);        
            void *result = (*(f->task_fp))(f->p_pool, f->param_for_task_fp);
            f->result = result;
            f->status = COMPLETED;
            sem_post_c(&f->result_sem); // increment_and_wake_a_waiting_thread_if_any()
            pthread_mutex_unlock_c(&f->f_lock);
            return f->result;
        }
        return f->result;
    } 
    else { 
        // thread executing this is not a worker thread so it is safe to 
        // sem_wait()
        sem_wait_c(&f->result_sem); // decrement_and_block_if_the_result_is_negative()
        // when the value is incremented by worker_function the result will be 
        // computed
        return f->result;
    }
}

void future_free(struct future *f) 
{
    if (f == NULL) { exception_exit("future_free() called with NULL parameter"); }
    pthread_mutex_destroy_c(&f->f_lock);
    sem_destroy_c(&f->result_sem);
    free(f);
}

/**
 * This is the logic for how a worker thread decides to execute a 
 * task.
 */
static void * worker_function(void *pool_and_worker2) 
{
    fprintf(stdout, ">>>> in %s(pool_and_worker)\n", "worker_function");

	is_worker = true; // = thread local variable
    struct thread_pool_and_current_worker *pool_and_worker = (struct thread_pool_and_current_worker*) pool_and_worker2;
	struct thread_pool *pool = pool_and_worker->pool;
	struct worker *worker = pool_and_worker->worker;

    fprintf(stdout, ">>>> worker->index_of_worker = %d\n", (int) worker->index_of_worker);
    fprintf(stdout, ">>>> worker->thread_id = %d\n", (int) worker->thread_id);

	while (true) {
        pthread_mutex_lock_c(&worker->local_deque_lock);
        bool worker_deque_locked = true;

		// if there are futures in local deque execute them first
		if (!list_empty(&worker->local_deque)) {
			struct future *future = list_entry(list_pop_front(&worker->local_deque), struct future, deque_elem);
			pthread_mutex_unlock_c(&worker->local_deque_lock);
            worker_deque_locked = false;

            pthread_mutex_lock_c(&future->f_lock); // TODO: do I need to lock before executing task_fp?     
            void *result = (*(future->task_fp))(pool, future->param_for_task_fp);    
			future->result = result;
            future->status = COMPLETED;            
			sem_post_c(&future->result_sem); // increment_and_wake_a_waiting_thread_if_any()
            pthread_mutex_unlock_c(&future->f_lock);   

            continue; // there might be another future in local deque to execute        
		} 

        if(worker_deque_locked) {
            pthread_mutex_unlock_c(&worker->local_deque_lock);  
        }

        pthread_mutex_lock_c(&pool->gs_queue_lock);
        bool gs_queue_locked = true;

        // else if there are futures in gs_queue execute them second 
		if (!list_empty(&pool->gs_queue)) {
            // "If a worker runs out of tasks, it checks a global submission queue for tasks. If a task
            //  can be found it it, it is executed" from 2.1 of spec
			struct future *future = list_entry(list_pop_front(&pool->gs_queue), struct future, gs_queue_elem);
			pthread_mutex_unlock_c(&pool->gs_queue_lock);
            gs_queue_locked = false;

            pthread_mutex_lock_c(&future->f_lock);
            void *result = (*(future->task_fp))(pool, future->param_for_task_fp);
            future->result = result;
            future->status = COMPLETED;            
			sem_post_c(&future->result_sem); // increment_and_wake_a_waiting_thread_if_any()
            pthread_mutex_unlock_c(&future->f_lock);

            continue; // // there might be another future in global submission queue to execute   
		} 

        if(gs_queue_locked) {
            pthread_mutex_unlock_c(&pool->gs_queue_lock);
        }

        // the local deque and global submission are empty
		if (true) { // TODO: consider removing later?
            // "Otherwise, the worker attempts to steal tasks to work on from the bottom of other
            //  threads'" deques.

            // iterate through other worker threads' deques
            int i;
            for (i = 0; i < pool->number_of_workers; i++) {
                struct worker *other_worker = pool->workers + i;

                // starting at the bottom through other_worker's local deque
                // and check if there is an unstarted future to steal and execute
                pthread_mutex_lock_c(&other_worker->local_deque_lock);
                bool other_worker_deque_locked = true;

                if (!list_empty(&other_worker->local_deque)) {
                    struct future *stolen_future = list_entry(list_pop_back(&other_worker->local_deque), struct future, deque_elem);
                    pthread_mutex_unlock_c(&other_worker->local_deque_lock);

                    // now execute this stolen future 
                    pthread_mutex_lock_c(&stolen_future->f_lock);
                    void *result = (*(stolen_future->task_fp))(pool, stolen_future->param_for_task_fp);
                    stolen_future->result = result;
                    stolen_future->status = COMPLETED;            
                    sem_post_c(&stolen_future->result_sem); // increment_and_wake_a_waiting_thread_if_any()
                    pthread_mutex_unlock_c(&stolen_future->f_lock);
                }

                if(other_worker_deque_locked) {
                    pthread_mutex_unlock_c(&other_worker->local_deque_lock);  
                }
            }

            // literally no futures to execute or steal so wait
            pthread_cond_wait_c(&pool->gs_queue_has_tasks, &pool->gs_queue_lock); // TODO: ?
        }
	}
    return NULL;
}

/**
 * Initialize the worker struct
 * @param worker = pointer to memory allocated for this struct
 * @param index_of_worker = the index of the worker in the thread_pool's array of worker structs
 * @return pointer to the initialized worker struct
 */
static struct worker * worker_init(struct worker *worker, unsigned int index_of_worker) 
{
    //worker->thread_id = will be set when pthread_create() is called
    worker->index_of_worker = index_of_worker;
    list_init(&worker->local_deque); 
    pthread_mutex_init_c(&worker->local_deque_lock, NULL);
    return worker;
} 

/**
 * Free all memory allocated to the worker struct.
 * @param worker = pointer to the worker to free
 */
static void worker_free(struct worker *worker)
{
    if (worker == NULL) { exception_exit("worker_free() you cannot pass NULL\n"); }
    pthread_mutex_destroy_c(&worker->local_deque_lock);
    free(worker);
}


/***************************************************************************
 * Wrapper functions for malloc, pthread.h, and semaphore.h
 *   - These make code more readable by moving the original function and 
 *     checking of the return value for errors.
 *   - All wrapped functions have the same parameters as the original and
 *     same name with _c appended to the end (for 'checked')
 *   - Not all pthread or semaphore functions included
 ***************************************************************************/


static void error_exit(char *msg, int err_code)
{
    fprintf(stderr, "%s: returned error code: %s\n", msg, strerror(err_code));
    exit(EXIT_FAILURE);
}

static void exception_exit(char *msg)
{
    fprintf(stderr, "%s\n", msg);
    exit(EXIT_FAILURE);
}


/* malloc */
static void * malloc_c(int size)
{
    void *p = malloc(size);
    if (p == NULL) {
        perror("malloc() failed to allocate memory\n");
        exit(EXIT_FAILURE);
    }
    return p;
}

/**
// POSIX Threads (pthread.h) 
// Thread Routines
static void pthread_create_c(pthread_t *thread, const pthread_attr_t *attr, 
                      void *(*start_routine)(void *), void *arg)
{
    int rc; // return code
    rc = pthread_create(thread, attr, start_routine, (void *)arg);
    if (rc != 0) {
        error_exit("pthread_create", rc);
    }
}
*/
static void pthread_join_c(pthread_t thread, void **value_ptr)
{
    int rc;
    rc = pthread_join(thread, value_ptr);
    if (rc != 0) {
        error_exit("pthread_join", rc);
    }
}

//int pthread_cancel(pthread_t thread)

static pthread_t pthread_self_c(void)
{
    // does not return any errors, included for convenience
    return pthread_self();
}

// Mutex Routines
static void pthread_mutex_destroy_c(pthread_mutex_t *mutex)
{
    int rc;
    rc = pthread_mutex_destroy(mutex);
    if (rc != 0) {
        error_exit("pthread_mutex_destroy", rc);
    }
}

static void pthread_mutex_init_c(pthread_mutex_t *mutex, const pthread_mutexattr_t *attr)
{
    int rc;
    rc = pthread_mutex_init(mutex, attr);
    if (rc != 0) {
        error_exit("pthread_mutex_init", rc);
    }
}

static void pthread_mutex_lock_c(pthread_mutex_t *mutex)
{
    int rc;
    rc = pthread_mutex_lock(mutex);
    if (rc != 0) {
        error_exit("pthread_mutex_lock", rc);
    }
}

//void pthread_mutex_trylock(pthread_mutex_t *mutex)
static void pthread_mutex_unlock_c(pthread_mutex_t *mutex)
{
    int rc;
    rc = pthread_mutex_unlock(mutex);
    if (rc != 0) {
        error_exit("pthread_mutex_unlock", rc);
    }
}

// Condition Variable Routines
static void pthread_cond_init_c(pthread_cond_t *cond, const pthread_condattr_t *attr)
{
    int rc;
    rc = pthread_cond_init(cond, attr);
    if (rc != 0) {
        error_exit("pthread_cond_init", rc);
    }
}

static void pthread_cond_destroy_c(pthread_cond_t *cond)
{
    int rc;
    rc = pthread_cond_destroy(cond);
    if (rc != 0) {
        error_exit("pthread_cond_destroy", rc);
    }
}

static void pthread_cond_broadcast_c(pthread_cond_t *cond)
{
    int rc;
    rc = pthread_cond_broadcast(cond);
    if (rc != 0) {
        error_exit("pthread_cond_broadcast", rc);
    }
}

static void pthread_cond_wait_c(pthread_cond_t *cond, pthread_mutex_t *mutex)
{
    int rc;
    rc = pthread_cond_wait(cond, mutex);
    if (rc != 0) {
        error_exit("pthread_cond_wait", rc);
    }
}

/* Semaphores (semaphore.h) */
static void sem_init_c(sem_t *sem, int pshared, unsigned int value)
{
    int rc;
    rc = sem_init(sem, pshared, value);
    if (rc < 0) {
        error_exit("sem_init", rc);
    }
}

static void sem_destroy_c(sem_t *sem)
{
    int rc;
    rc = sem_destroy(sem);
    if (rc < 0) {
        error_exit("sem_destroy", rc);
    }
}

static void sem_post_c(sem_t *sem)
{
    int rc;
    rc = sem_post(sem);
    if (rc < 0) {
        error_exit("sem_post", rc);
    }
}

static void sem_wait_c(sem_t *sem)
{
    int rc;
    rc = sem_wait(sem);
    if (rc < 0) {
        error_exit("sem_wait", rc);
    }
}