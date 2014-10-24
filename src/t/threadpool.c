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
#include <string.h>

#include "list.h"
//#define DEBUG // comment out to turn off debug print statements


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
   
    pthread_mutex_t shutting_down_lock;
    bool is_shutting_down; 
    sem_t number_of_futures_to_execute;

    unsigned int num_workers;       // NOTE: doesn't need to be synchronized how its currently used
    struct list/*<future>*/ workers_list;

    // pthread_cond_t tasks_available;
    // int num_tasks;
    // pthread_mutex_t num_tasks_lock;    
};


/* Helper functions */
static void set_shutting_down_flag(struct thread_pool* pool, bool shutdown_was_requested);
static bool is_shutting_down(struct thread_pool* pool);
static void set_shutting_down_flag(struct thread_pool* pool, bool shutting_down_value);
// private functions for this class that must be declared here to be called below
static void * worker_function(void *pool_and_worker_arg);
static void worker_free(struct worker *worker);

static bool is_shutting_down(struct thread_pool* pool);

static void error_exit(char *msg, int err_code);


/* Wrapper functions with error checking for pthreads and semaphores */
/* POSIX Threads (pthread.h) */
// Thread Routines
static void pthread_create_c(pthread_t *thread, const pthread_attr_t *attr, 
                      void *(*start_routine)(void *), void *arg);
static void pthread_join_c(pthread_t thread, void **value_ptr);
static pthread_t pthread_self_c(void);
// Mutex Routines
static void pthread_mutex_destroy_c(pthread_mutex_t *mutex);
static void pthread_mutex_init_c(pthread_mutex_t *mutex, const pthread_mutexattr_t *attr);
static void pthread_mutex_lock_c(pthread_mutex_t *mutex);
static void pthread_mutex_unlock_c(pthread_mutex_t *mutex);
// Condition Variable Routines 

// static void pthread_cond_init_c(pthread_cond_t *cond, const pthread_condattr_t *attr);
// static void pthread_cond_destroy_c(pthread_cond_t *cond);
// static void pthread_cond_broadcast_c(pthread_cond_t *cond);
// static void pthread_cond_wait_c(pthread_cond_t *cond, pthread_mutex_t *mutex);

/* Semaphores (semaphore.h) */
static void sem_init_c(sem_t *sem, int pshared, unsigned int value);
static void sem_destroy_c(sem_t *sem);
static void sem_post_c(sem_t *sem);
static void sem_wait_c(sem_t *sem);


/****************/




/**
 * @param nthreads = number of worker threads to create for this threadpool
 */
struct thread_pool * thread_pool_new(int nthreads) 
{
    #ifdef DEBUG 
        fprintf(stdout, "[Thread ID: %lu] in %s():  ENTER thread_pool_new", (unsigned long)pthread_self(), "thread_pool_new");
    #endif
    assert(nthreads > 0);

	is_worker = false; // worker_function() sets it to true

    struct thread_pool* pool = (struct thread_pool*) malloc(sizeof(struct thread_pool));

    if (pool == NULL) {
        #ifdef DEBUG 
        fprintf(stdout, "[Thread ID: %lu] in %s: malloc error\n", (unsigned long)pthread_self() % 1000000, "thread_pool_new");
        #endif
        #ifdef FAIL_ON_ERROR
        exit(EXIT_FAILURE);
        #endif
    }
    pthread_mutex_init(&pool->gs_queue_lock, NULL);
    list_init(&pool->gs_queue);    
    
    // pthread_mutex_init_c(&pool->num_tasks_lock, NULL);    
    // pthread_cond_init_c(&pool->tasks_available, NULL);
    // set_num_tasks(pool, 0);

    pthread_mutex_init(&pool->shutting_down_lock, NULL);
    set_shutting_down_flag(pool, false); // synchronized

    sem_init(&pool->number_of_futures_to_execute, 0, 0);
    pool->num_workers = nthreads;

    // Initialize workers list
    list_init(&pool->workers_list);
    int i;
    for(i = 0; i < nthreads; i++) {
        struct worker *worker = (struct worker*) malloc(sizeof(struct worker));
        worker->thread_id = (pthread_t *) malloc(sizeof(pthread_t));

        if (worker == NULL || worker->thread_id == NULL) { 
            #ifdef DEBUG 
            fprintf(stdout, "[Thread ID: %lu] in %s:", (unsigned long)pthread_self() % 1000000, "thread_pool_new");
            #endif
        }
        list_init(&worker->local_deque); 
        pthread_mutex_init_c(&worker->local_deque_lock, NULL);
        list_push_back(&pool->workers_list, &worker->elem);
    }


    // to be passed as a parameter to worker_function()
    struct thread_pool_and_current_worker *worker_fn_args = (struct thread_pool_and_current_worker *) 
                malloc(sizeof(struct thread_pool_and_current_worker));
    pthread_mutex_init_c(&worker_fn_args->lock, NULL);
    pthread_mutex_lock_c(&worker_fn_args->lock);
    worker_fn_args->pool = pool; 

    struct list_elem *e;
    for (e = list_begin(&pool->workers_list); e != list_end(&pool->workers_list); e = list_next(e)) {
        struct worker *current_worker = list_entry(e, struct worker, elem);

    	worker_fn_args->worker = current_worker;  

        pthread_create_c(current_worker->thread_id, NULL, worker_function, worker_fn_args);
    }
    pthread_mutex_unlock_c(&worker_fn_args->lock);
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

    #ifdef DEBUG 
    fprintf(stdout, "[Thread ID: %lu] \n\tENTERING FUNCTION %s():\n",
         (unsigned long)pthread_self() % 1000000, "thread_pool_shutdown_and_destroy");
    #endif
    /*

    assert(pool != NULL);
    
    */

    if (pool == NULL) {
        #ifdef DEBUG 
        fprintf(stdout, "[Thread ID: %lu] in %s: pool == NULL ?",
                (unsigned long)pthread_self() % 1000000, "thread_pool_shutdown_and_destroy");
        #endif
        #ifdef FAIL_ON_ERROR
        exit(EXIT_FAILURE);
        #endif
    }
    assert(!is_shutting_down(pool));    /* should not be called twice. If it is, it is an error either
                                          in our logic or in how the client is calling it. */

    

    set_shutting_down_flag(pool, true);
    
    int i;
    for (i = 0; i < pool->num_workers; i++) {
        sem_post_c(&pool->number_of_futures_to_execute);
    }

    // Join all worker threads
    struct list_elem *e;
    for (e = list_begin(&pool->workers_list); e != list_end(&pool->workers_list); e = list_next(e)) {
        #ifdef DEBUG 
        fprintf(stdout, ">> in %s, in join all workers loop\n", "thread_pool_shutdown_and_destroy");
        #endif
        struct worker *current_worker = list_entry(e, struct worker, elem);
        // unfortunately it causes gdb to also deadlock...

        assert(current_worker != NULL);

        pthread_join_c(*current_worker->thread_id, NULL);
        worker_free(current_worker);

    }

    pthread_mutex_destroy_c(&pool->gs_queue_lock);
    pthread_mutex_destroy_c(&pool->shutting_down_lock);
    // pthread_cond_destroy_c(&pool->tasks_available); 

    free(pool);
    return;
}

struct future * thread_pool_submit(struct thread_pool *pool, fork_join_task_t task, void *data)
{

    #ifdef DEBUG 
    fprintf(stdout, "[Thread ID: %lu] \n\tENTERING FUNCTION %s():\n", 
        (unsigned long)pthread_self() % 1000000, "thread_pool_submit");
    #endif

    if (pool == NULL) {
        #ifdef DEBUG 
        fprintf(stdout, "[Thread ID: %lu] in %s: ERROR pool null in thread_pool_submit!!!!!!!!\n", 
            (unsigned long)pthread_self() % 1000000, "thread_pool_submit");
        #endif
        #ifdef FAIL_ON_ERROR
        exit(EXIT_FAILURE);
        #endif
    }
    // --------------------- Initialize Future struct --------------------------
    struct future *p_future = (struct future*) malloc(sizeof(struct future));
    pthread_mutex_init(&p_future->f_lock, NULL);
    //pthread_mutex_lock(&p_future->f_lock);
    p_future->param_for_task_fp = data;
    p_future->task_fp = task; 
    p_future->result = NULL;
    sem_init_c(&p_future->result_sem, 0, 0);
    p_future->status = NOT_STARTED;

    //pthread_mutex_unlock(&p_future->f_lock);
    // -------------------------------------------------------------------------

    // If this thread is not a worker, add future to global queue (external submission)
    if (!is_worker) {

    	// Acquire lock for the global submission queue 
    	pthread_mutex_lock_c(&pool->gs_queue_lock);
	    list_push_back(&pool->gs_queue, &p_future->gs_queue_elem);
        // Broadcast to sleeping threads that future is availabe in global submission queue

        // increment_num_tasks(pool);
         // pthread_cond_broadcast_c(&pool->tasks_available);

        sem_post_c(&pool->number_of_futures_to_execute);
	    pthread_mutex_unlock(&pool->gs_queue_lock);
	} 
    else { // internal submission by worker thread       
        // add to the top of local_deque of the worker thread calling the thread_pool_submit()
        pthread_t this_thread_id = pthread_self_c();
        // loop through pool's worker_list to find the worker struct with this thread's tid

        struct list_elem *e;
        for (e = list_begin(&pool->workers_list); e != list_end(&pool->workers_list); e = list_next(e)) {
            struct worker *current_worker = list_entry(e, struct worker, elem);
            if (*current_worker->thread_id == this_thread_id) {
                pthread_mutex_lock_c(&current_worker->local_deque_lock);
                // internal submissions (futures) added to top of local deque                
                list_push_front(&current_worker->local_deque, &p_future->deque_elem);

                sem_post_c(&pool->number_of_futures_to_execute);
                pthread_mutex_unlock(&current_worker->local_deque_lock);                            
            }
        }
	}
	return p_future;
}

void * future_get(struct future *f) 
{
    #ifdef DEBUG 
    fprintf(stdout, "[Thread ID: %lu] \n\tENTERING++ FUNCTION %s():\n", 
        (unsigned long)pthread_self() % 1000000, "future_get");
    #endif
    assert(f != NULL);

    if (is_worker) { /* internal worker threads */
        #ifdef DEBUG
            fprintf(stdout, "[Thread ID: %lu] in %s(): LOCK: &future->f_lock (1) \n", 
                (unsigned long)pthread_self() % 1000000, "future_get");
        #endif
        pthread_mutex_lock_c(&f->f_lock);

        if (f->status == COMPLETED) {
            #ifdef DEBUG
            fprintf(stdout, "[Thread ID: %lu] in %s(): f->status == COMPLETED [if] (1) \n", 
                (unsigned long)pthread_self() % 1000000, "future_get");
            #endif            
            pthread_mutex_unlock_c(&f->f_lock);

        }
        // Below if statement is for when the threadpool has 1 thread and 
        // multiple futures. You cannot just simply call sem_wait() here
        // because if 1 worker thread calls sem_post() on the first future
        // and the first future generated 2 other futures then the 1 thread
        // would execute 1 of the 2 generated futures and then deadlock.
        else if (f->status == NOT_STARTED) {
            #ifdef DEBUG
            fprintf(stdout, "[Thread ID: %lu] in %s(): f->status == NOT_STARTED [else if] (1) \n",
             (unsigned long)pthread_self() % 1000000, "future_get");
            #endif       
                   
            pthread_mutex_unlock(&f->f_lock);     
            // Execute task in worker thread (do not keep locked while executing)
            void *result = (*(f->task_fp))(f->p_pool, f->param_for_task_fp);
            pthread_mutex_lock(&f->f_lock);

            f->result = result;
            f->status = COMPLETED;
//            decrement_num_tasks(f->p_pool);

            pthread_mutex_unlock(&f->f_lock);
            sem_post_c(&f->result_sem); // increment_and_wake_a_waiting_thread_if_any()
        } else {
            pthread_mutex_unlock_c(&f->f_lock);
        }
    } 
    else { // external threads 
        // External threads always block here
        sem_wait_c(&f->result_sem);
        // when the value is incremented by worker_function the result will be 
        // computed and returned
    }
    #ifdef DEBUG 
    fprintf(stdout, "[Thread ID: %lu] \n\tEXITING++ FUNCTION %s():\n", 
        (unsigned long)pthread_self() % 1000000, "future_get");
    #endif        
    return f->result;
}

void future_free(struct future *f) 
{

    #ifdef DEBUG 
    fprintf(stdout, "[Thread ID: %lu] in %s():  \n\tENTERING FUNCTION future_free \n",
     pthread_self() % 1000000, "future_free");  
    #endif
    assert(f != NULL);
    pthread_mutex_destroy_c(&f->f_lock);
    sem_destroy_c(&f->result_sem);
    free(f);
}

/**
 * This is the logic for how a worker thread decides to execute a 
 * task.
 */
static void * worker_function(void *pool_and_worker_arg) 
{

    #ifdef DEBUG
    fprintf(stdout, "[Thread ID: %lu] in %s():  \n\tENTERING FUNCTION worker_function \n", 
        (unsigned long)pthread_self() % 1000000, "worker_function");
    #endif
	is_worker = true; // = thread local variable
    struct thread_pool_and_current_worker *pool_and_worker = (struct thread_pool_and_current_worker *) pool_and_worker_arg;
    #ifdef DEBUG
     fprintf(stdout, "[Thread ID: %lu] in %s(): LOCK: pool_and_worker->lock \n",
             (unsigned long)pthread_self() % 1000000, "worker_function");
    #endif
    pthread_mutex_lock_c(&pool_and_worker->lock);
	struct thread_pool *pool = pool_and_worker->pool;
	struct worker *worker = pool_and_worker->worker;
    pthread_mutex_unlock_c(&pool_and_worker->lock);
            
    // The worker thread checks three potential locations for futures to execute 
    #ifdef DEBUG
     fprintf(stdout, "[Thread ID: %lu] in %s(): if is_shutting_down while(true) \n", (unsigned long)pthread_self(), "worker_function");
    #endif
    while (true) {
        // check if threadpool has been shutdown
        if (is_shutting_down(pool)) {  
            #ifdef DEBUG 
            fprintf(stdout, "[Thread ID: %lu] in %s(): in while(true) ; About to call pthread_exit\n",
                            (unsigned long)pthread_self() % 1000000, "worker_function");
            #endif            
            fprintf(stdout, ">>> about to call %s(NULL)\n", "pthread_exit");
            pthread_exit(NULL);
        }


        // 1) Checks its own local deque first 
        pthread_mutex_lock(&worker->local_deque_lock);
		if (!list_empty(&worker->local_deque)) {
            #ifdef DEBUG 
            fprintf(stdout, "[Thread ID: %lu] in %s(): (1) Check its own local deque ---> NOT EMPTY \n", 
                            (unsigned long)pthread_self() % 1000000, "worker_function");
            #endif
            struct future *future = list_entry(list_pop_front(&worker->local_deque), struct future, deque_elem);
			pthread_mutex_unlock(&worker->local_deque_lock);
            void *result = (*(future->task_fp))(pool, future->param_for_task_fp);  /// execute future task  
            pthread_mutex_lock(&future->f_lock); // TODO: do I need to lock before executing task_fp?   
			future->result = result;
            future->status = COMPLETED;            
            pthread_mutex_unlock(&future->f_lock);  
            sem_post_c(&future->result_sem); // increment_and_wake_a_waiting_thread_if_any() 

            continue; // there might be another future in local deque to execute        
		} 
        #ifdef DEBUG
        fprintf(stdout, "[Thread ID: %lu] in %s(): ---LOCAL DEQUE EMPTY---- (1) \n",
                             (unsigned long)pthread_self() % 1000000, "worker_function");
        #endif  
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

        /* 3) The worker attempts steals a task to work on from the bottom of other threads' deques */
        #ifdef DEBUG 
        fprintf(stdout, "[Thread ID: %lu] in %s(): (3) STEAL starting \n", (unsigned long)pthread_self(), "worker_function");
        #endif
        // iterate through other worker threads' deques
        struct list_elem *e;
        bool stole_a_future = false;

        #ifdef DEBUG
         fprintf(stdout, "[Thread ID: %lu] in %s(): (3) Steal: FOR (loop through workers) \n", (unsigned long)pthread_self(), "worker_function");
        #endif
        for (e = list_begin(&pool->workers_list); e != list_end(&pool->workers_list); e = list_next(e)) {
            if (stole_a_future) {
                break;
            }
            struct worker *other_worker = list_entry(e, struct worker, elem);
            // steal future from bottom of their deque, if they have any futures
            pthread_mutex_lock_c(&other_worker->local_deque_lock);
            if (!list_empty(&other_worker->local_deque)) {

                fprintf(stdout, ">>>> about to steal future from another worker's deque\n");
                struct future *stolen_future = list_entry(list_pop_back(&other_worker->local_deque), struct future, deque_elem);
                pthread_mutex_unlock_c(&other_worker->local_deque_lock);
                stole_a_future = true;
                // now add this stolen future to the current worker's local deque
                pthread_mutex_lock(&worker->local_deque_lock);
                list_push_front(&worker->local_deque, &stolen_future->deque_elem);

                pthread_mutex_unlock(&worker->local_deque_lock);
            } else {
                pthread_mutex_unlock(&other_worker->local_deque_lock);
            }
            #ifdef DEBUG
             fprintf(stdout, "[Thread ID: %lu] in %s(): (3) Steal: COULD NOT STEAL \n", (unsigned long)pthread_self(), "worker_function"); 
            #endif
    }
    #ifdef DEBUG 
        fprintf(stdout, "[Thread ID: %lu] in %s(): (4) No Tasks [found by current algorithm] - nothing executed afterwards \n", 
                                    (unsigned long)pthread_self() % 1000000, "worker_function");
    #endif
    // must be inside while(true) at top
        /*********
         * wrap in while (num_task. > 0) {
        sem_wait(&pool->number_of_futures_to_execute); <-- synchronize # futures
	   *******/

    sem_wait(&pool->number_of_futures_to_execute);

    }
    return NULL;
}



/* Sets the shutting_down flag on or off in a synchronized way 
 * @param shutdown_was_requested - true or false 
 */
static void set_shutting_down_flag(struct thread_pool* pool, bool shutting_down_value) 
{
    assert(pool != NULL);
    pthread_mutex_lock_c(&pool->shutting_down_lock);
    pool->is_shutting_down = shutting_down_value; 
    pthread_mutex_unlock_c(&pool->shutting_down_lock);
}


/* Checks whether the shutting_down flag is set to on or off in a synchronized way 
 * @param pool A pointer to the pool
 * @return true is shutting_down is set to true
 */
static bool is_shutting_down(struct thread_pool* pool)
{
    assert(pool != NULL);
    pthread_mutex_lock_c(&pool->shutting_down_lock);
    bool flag = pool->is_shutting_down;
    pthread_mutex_unlock_c(&pool->shutting_down_lock);
    return flag;
}



static void worker_free(struct worker *worker)
{
    fprintf(stdout, "> called %s(f)\n", "worker_free");
    assert(worker != NULL);

    pthread_mutex_destroy(&worker->local_deque_lock); // Causing problem when run with helgrind
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

    //fprintf(stderr, "%s\n", msg);
    exit(EXIT_FAILURE);
}

/* POSIX Threads (pthread.h) */
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
    return (unsigned long)pthread_self();
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

/*
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
*/

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



