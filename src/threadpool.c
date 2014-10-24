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
#define DEBUG // comment out to turn off debug print statements
#define FAIL_ON_ERROR

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

    struct list/*<Future>*/ local_deque;
    pthread_mutex_t local_deque_lock;

    struct list_elem elem;
};

struct thread_pool {
    struct list/*<Future>*/ gs_queue; // global submission queue
    pthread_mutex_t gs_queue_lock;      
   
    bool shutting_down;     // threadpool_shutdown_and_destroy() has been called        
    pthread_mutex_t shutting_down_lock; // lock to synchronize the shutting_down flag

    struct list workers_list;
    //pthread_mutex_t workers_list_lock;

    // pthread_cond_t tasks_available;
    // pthread_mutex_t tasks_available_lock;

    // unsigned int num_workers;     // number of worker threads in the threadpool              
    // pthread_mutex_t num_workers_lock;  
};


/* Helper functions */
static void set_shutting_down_flag(struct thread_pool* pool, bool shutdown_was_requested);
static bool is_shutting_down(struct thread_pool* pool);
static void set_shutting_down_flag(struct thread_pool* pool, bool shutting_down_value);
// private functions for this class that must be declared here to be called below
static void * worker_function(void *pool_and_worker_arg);
// static void worker_free(struct worker *worker);

static bool is_shutting_down(struct thread_pool* pool);

/* TODO:  For debugging, won't really need */
// static void decrement_num_workers(struct thread_pool* pool);
// static int get_number_of_workers(struct thread_pool* pool);
// static void set_num_workers(struct thread_pool* pool, int num_workers);

//static struct worker * remove_calling_thread_from_workers_list(struct thread_pool *pool);
static void error_exit(char *msg, int err_code);


/* Wrapper functions with error checking for pthreads and semaphores */
/* POSIX Threads (pthread.h) */
// Thread Routines
static void pthread_create_c(pthread_t *thread, const pthread_attr_t *attr, 
                      void *(*start_routine)(void *), void *arg);
//static void pthread_join_c(pthread_t thread, void **value_ptr);
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
        fprintf(stdout, "[Thread ID: %lu] in %s: malloc error", (unsigned long)pthread_self(), "thread_pool_new");
        #endif
        #ifdef FAIL_ON_ERROR
        exit(EXIT_FAILURE);
        #endif
    }
    /* 
    assert(pool != NULL); 
    */

    list_init(&pool->gs_queue);    
    pthread_mutex_init_c(&pool->gs_queue_lock, NULL);

    // Initialize condition variable used to broadcast to worker threads that 
    // tasks are available in the global submission queue 
    
    //pthread_cond_init_c(&pool->tasks_available, NULL);
    //pthread_mutex_init_c(&pool->tasks_available_lock, NULL);

    pthread_mutex_init_c(&pool->shutting_down_lock, NULL);
    set_shutting_down_flag(pool, false); // synchronized
    // set_num_workers(pool, nthreads);


    // Initialize workers list
    //pthread_mutex_init_c(&pool->workers_list_lock, NULL);

    list_init(&pool->workers_list);
    int i;
    for(i = 0; i < nthreads; i++) {
        struct worker *worker = (struct worker*) malloc(sizeof(struct worker));
        worker->thread_id = (pthread_t *) malloc(sizeof(pthread_t));
        if (worker == NULL || worker->thread_id == NULL) { 
            #ifdef DEBUG 
            fprintf(stdout, "[Thread ID: %lu] in %s:", (unsigned long)pthread_self(), "thread_pool_new");
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
    fprintf(stdout, "[Thread ID: %lu] in %s():  ENTER thread_pool_shutdown_and_destroy\n", (unsigned long)pthread_self(), "thread_pool_shutdown_and_destroy");
    #endif
    /*

    assert(pool != NULL);
    
    */

    if (pool == NULL) {
        #ifdef DEBUG 
        fprintf(stdout, "[Thread ID: %lu] in %s: pool == NULL ?", (unsigned long)pthread_self(), "thread_pool_shutdown_and_destroy");
        #endif
        #ifdef FAIL_ON_ERROR
        exit(EXIT_FAILURE);
        #endif
    }
    assert(!is_shutting_down(pool));    /* should not be called twice. If it is, it is an error either
                                          in our logic or in how the client is calling it. */

    
    pthread_mutex_lock_c(&pool->gs_queue_lock);

    set_shutting_down_flag(pool, true); // synchronized
    pthread_mutex_unlock_c(&pool->gs_queue_lock);

    // Join all worker threads
    struct list_elem *e;
    for (e = list_begin(&pool->workers_list); e != list_end(&pool->workers_list); e = list_next(e)) {
        #ifdef DEBUG 
        fprintf(stdout, ">> in %s, in join all workers loop\n", "thread_pool_shutdown_and_destroy");
        #endif
        struct worker *current_worker = list_entry(e, struct worker, elem);
        // unfortunately it causes gdb to also deadlock...

        assert(current_worker != NULL);
        assert(*current_worker->thread_id != 0);

        /* pthread_join_c(*current_worker->thread_id, NULL); */
        if (pthread_join(*current_worker->thread_id, NULL) != 0) {
            #ifdef DEBUG 
            fprintf(stdout, ">> in %s, PTHREAD_JOIN FAILED \n", "thread_pool_shutdown_and_destroy");
            #endif            
        }

        /* TODO if not here, need to add somewhere else */
        /* ????? */
        /****************************************************
        * maybe don't need this. always worker thread joined?? 
        *
        *
        * remove_calling_thread_from_workers_list(pool); 
        *****************************************************/
        /** could do if (is_worker) { remove.. } */
        /*** worker_free(current_worker); ***/
        // decrement number of workers since one has terminated via joining.
        #ifdef DEBUG
         // fprintf(stdout, "[Thread ID: %lu] in %s(): Num_workers after worker_free will be %d\n", (unsigned long)pthread_self(), 
         //                "thread_pool_shutdown_and_destroy", get_number_of_workers(pool));
        #endif
        // decrement_num_workers(pool);
    }

    #ifdef DEBUG
        
    #endif
    pthread_mutex_destroy_c(&pool->gs_queue_lock);
    pthread_mutex_destroy_c(&pool->shutting_down_lock);
    //pthread_mutex_destroy_c(&pool->workers_list_lock);
    // TODO cond vars
    //pthread_cond_destroy_c(&pool->tasks_available); 
    //#ifdef DEBUG fprintf(stdout, "cond_destroy : prob. still locked!\n");
    //#endif
    free(pool);
    return;
}

struct future * thread_pool_submit(struct thread_pool *pool, fork_join_task_t task, void *data)
{
    #ifdef DEBUG 
    fprintf(stdout, "[Thread ID: %lu] in %s():  ENTER thread_pool_submit \n", (unsigned long)pthread_self(), 
                                    "thread_pool_submit");  
    #endif
    if (pool == NULL) {
        #ifdef DEBUG 
        fprintf(stdout, "[Thread ID: %lu] in %s: malloc error", (unsigned long)pthread_self(), "thread_pool_new");
        #endif
        #ifdef FAIL_ON_ERROR
        exit(EXIT_FAILURE);
        #endif
    }
    //assert(pool != NULL);
    assert(task != NULL);
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
        /* TODO: add back. right now debugging without ever making threads sleep  */
        //pthread_cond_broadcast_c(&pool->tasks_available);
       
        pthread_mutex_unlock_c(&pool->gs_queue_lock);

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
                pthread_mutex_unlock_c(&current_worker->local_deque_lock);
            }
        }
    }
    return p_future;
}

void * future_get(struct future *f) 
{
    #ifdef DEBUG 
    fprintf(stdout, "[Thread ID: %lu] in %s():\n", (unsigned long)pthread_self(), "future_get");
    #endif
    assert(f != NULL);
    if (is_worker) { /* internal worker threads */
        pthread_mutex_lock_c(&f->f_lock);

        if (f->status == COMPLETED) {
            pthread_mutex_unlock_c(&f->f_lock);

            return f->result;
        }
        // Below if statement is for when the threadpool has 1 thread and 
        // multiple futures. You cannot just simply call sem_wait() here
        // because if 1 worker thread calls sem_post() on the first future
        // and the first future generated 2 other futures then the 1 thread
        // would execute 1 of the 2 generated futures and then deadlock.
        else if (f->status == NOT_STARTED) {
            // Execute task in worker thread      

            void *result = (*(f->task_fp))(f->p_pool, f->param_for_task_fp);
            f->result = result;
            f->status = COMPLETED;
            sem_post_c(&f->result_sem);
            pthread_mutex_unlock_c(&f->f_lock);
            return f->result;
        } else {
            pthread_mutex_unlock_c(&f->f_lock);
            return f->result;
        }
    } 
    else { // external threads 
        // External threads always block here
        sem_wait_c(&f->result_sem);
        // when the value is incremented by worker_function the result will be 
        // computed and returned
        return f->result;
    }
}

void future_free(struct future *f) 
{
    #ifdef DEBUG 
    fprintf(stdout, "[Thread ID: %lu] in %s():  ENTER thread_pool_submit \n", pthread_self(), "thread_pool_submit");  
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
     fprintf(stdout, "[Thread ID: %lu] in %s():  ENTER worker_function \n", (unsigned long)pthread_self(), "worker_function");
    #endif
    is_worker = true; // = thread local variable
    struct thread_pool_and_current_worker *pool_and_worker = (struct thread_pool_and_current_worker *) pool_and_worker_arg;
    pthread_mutex_lock_c(&pool_and_worker->lock);
    struct thread_pool *pool = pool_and_worker->pool;
    struct worker *worker = pool_and_worker->worker;
    pthread_mutex_unlock_c(&pool_and_worker->lock);
    // FREE p&w
            
    // The worker thread checks three potential locations for futures to execute 
    #ifdef DEBUG
     fprintf(stdout, "[Thread ID: %lu] in %s(): if shutdown_requested while(true) \n", (unsigned long)pthread_self(), "worker_function");
    #endif
    while (true) {
        // check if threadpool has been shutdown
        if (is_shutting_down(pool)) {
            #ifdef DEBUG 
            // fprintf(stdout, "[Thread ID: %lu] in %s(): in while(true) shutdown; Num_workers after exit will be %d\n",
            //                 (unsigned long)pthread_self(), "worker_function", get_number_of_workers(pool));
            #endif
            /*****************************
            remove_calling_thread_from_workers_list(pool);
            decrement_num_workers(pool);
            ******************************/
            /* TODO: remove worker from workers list before it exits */
            /* TODO: free_worker(worker); */
            pthread_exit(NULL);
        }
        

        /* 1) Checks its own local deque first */
        #ifdef DEBUG
         //fprintf(stdout, "[Thread ID: %lu] in %s(): (1) Check its own local deque \n", (unsigned long)pthread_self(), "worker_function");
        #endif
        pthread_mutex_lock_c(&worker->local_deque_lock);
        if (!list_empty(&worker->local_deque)) {
            #ifdef DEBUG 
            fprintf(stdout, "[Thread ID: %lu] in %s(): (1) Check its own local deque ---> NOT EMPTY \n", 
                            (unsigned long)pthread_self(), "worker_function");
            #endif
            struct future *future = list_entry(list_pop_front(&worker->local_deque), struct future, deque_elem);
            pthread_mutex_unlock_c(&worker->local_deque_lock);        

            pthread_mutex_lock_c(&future->f_lock);
            void *result = (*(future->task_fp))(pool, future->param_for_task_fp);  /* execute future task */
            future->result = result;
            future->status = COMPLETED;            
            // increment_and_wake_a_waiting_thread_if_any()
            pthread_mutex_unlock_c(&future->f_lock);
            sem_post_c(&future->result_sem);
            continue; // there might be another future in local deque to execute
        }
        // 'if' must be false to get to this point. When 'if' true, releases lock. 
        pthread_mutex_unlock_c(&worker->local_deque_lock);   // fails with EPERM if not owner

        /* 2) Check for futures in global threadpool queue  */
        #ifdef DEBUG
        //fprintf(stdout, "[Thread ID: %lu] in %s(): (2) Check Global Queue \n", (unsigned long)pthread_self(), "worker_function"); 
        #endif
        /** THIS MUTEX RETURNING EINVAL **/  
        pthread_mutex_lock_c(&pool->gs_queue_lock);
        if (!list_empty(&pool->gs_queue)) {
            #ifdef DEBUG 
            //fprintf(stdout, "[Thread ID: %lu] in %s(): (2) Check Global Queue--->  NOT EMPTY\n", (unsigned long)pthread_self(), "worker_function");
            #endif
            struct future *future = list_entry(list_pop_front(&pool->gs_queue), struct future, gs_queue_elem);
            pthread_mutex_unlock_c(&pool->gs_queue_lock);
            
            //pthread_mutex_lock_c(&future->f_lock);
            //void *result = (*(future->task_fp))(pool, future->param_for_task_fp);
            //future->result = result;
            //future->status = COMPLETED;            
            //sem_post_c(&future->result_sem); // increment_and_wake_a_waiting_thread_if_any()
            //pthread_mutex_unlock_c(&future->f_lock);
           
            // add it to this worker's deque;
            pthread_mutex_lock_c(&worker->local_deque_lock);
            list_push_front(&worker->local_deque, &future->deque_elem);

            if (!list_empty(&worker->local_deque)) {
                #ifdef DEBUG 
                //fprintf(stdout, "[Thread ID: %lu] in %s(): (2) {XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX}|ADDED FROM QUEUE TO LOCAL DEQUE \n", (unsigned long)pthread_self(), "worker_function");
                #endif
            } else {
                #ifdef DEBUG 
                fprintf(stdout, "[Thread ID: %lu] in %s(): (2) {XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX} ERROR!!!!! COULD NOT ADD FROM QUEUE TO LOCAL DEQUE \n", (unsigned long)pthread_self(), "worker_function");                
                #endif
            }
            pthread_mutex_unlock_c(&worker->local_deque_lock);

            continue; // // there might be another future in global submission queue to execute   
        } 
        pthread_mutex_unlock_c(&pool->gs_queue_lock);

        /* 3) The worker attempts steals a task to work on from the bottom of other threads' deques */
        #ifdef DEBUG 
        //fprintf(stdout, "[Thread ID: %lu] in %s(): (3) STEAL starting \n", (unsigned long)pthread_self(), "worker_function");
        #endif
        // iterate through other worker threads' deques
        struct list_elem *e;
        bool stole_a_future = false;

        //pthread_mutex_lock_c(&pool->workers_list_lock);

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
                #ifdef DEBUG
                //fprintf(stdout, "[Thread ID: %lu] in %s(): (3) Steal: ******at least 1 worker deque not empty****** \n", (unsigned long)pthread_self(), "worker_function");                
                #endif
                struct future *stolen_future = list_entry(list_pop_back(&other_worker->local_deque), struct future, deque_elem);
                pthread_mutex_unlock_c(&other_worker->local_deque_lock);
                stole_a_future = true;

                pthread_mutex_lock_c(&other_worker->local_deque_lock);
                pthread_mutex_lock_c(&worker->local_deque_lock);
                // now add this stolen future to the current worker's local deque
                list_push_front(&worker->local_deque, &stolen_future->deque_elem);
                #ifdef DEBUG 
                fprintf(stdout, "[Thread ID: %lu] in %s(): (3) Steal: STOLE_A_FUTURE = true \n", (unsigned long)pthread_self(), "worker_function");
                #endif
            } 
            else {
                pthread_mutex_unlock_c(&other_worker->local_deque_lock);
            }
            #ifdef DEBUG
             //fprintf(stdout, "[Thread ID: %lu] in %s(): (3) Steal: COULD NOT STEAL \n", (unsigned long)pthread_self(), "worker_function"); 
            #endif
            // if (get_number_of_workers(pool) <= 0) { 
                #ifdef DEBUG 
                // fprintf(stdout, "[Thread ID: %lu] in %s(): (3) Steal: BREAK STEAL LOOP: num_workers <= 0 \n", (unsigned long)pthread_self(), "worker_function"); 
                #endif
                
                break; 
            }             
        }
        //pthread_mutex_unlock_c(&pool->workers_list_lock);


                                
        #ifdef DEBUG 
        fprintf(stdout, "[Thread ID: %lu] in %s(): (4) No Tasks [found by current algorithm] - nothing executed afterwards \n", (unsigned long)pthread_self(), "worker_function");
        #endif

            /* Failing that, the worker thread should block until a task becomes available */
              // TODO: Must change logic so that the thread blocks (sleeps) only until a task becomes available
              // *either* in global queue *or* in another worker's deque. Currently, sleeps til global queue
              
              // How to implement: counter or semaphore which is incremented each time a task is submitted to the pool 
              // (internal or external) and decremented each time a task is executed.

        // pthread_mutex_lock_c(&pool->tasks_available_lock);


        // while (cond) { 
                // wrap in while loop due to possible spurious wake ups
           // pthread_cond_wait_c(&pool->tasks_available, &pool->tasks_available_lock); 
        // }
        // pthread_mutex_unlock_c(&pool->tasks_available_lock);

        /*
        if (pool->shutdown_requested) {   // in while loop?
            pthread_mutex_unlock_c(&pool->tasks_available_lock); // change
            pthread_exit(NULL);
        }
        */
    

    return NULL;
}



/* Sets the shutting_down flag on or off in a synchronized way 
 * @param shutdown_was_requested - true or false 
 */
static void set_shutting_down_flag(struct thread_pool* pool, bool shutting_down_value) 
{
    assert(pool != NULL);
    pthread_mutex_lock_c(&pool->shutting_down_lock);
    pool->shutting_down = shutting_down_value; c
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
    bool flag = pool->shutting_down;
    pthread_mutex_unlock_c(&pool->shutting_down_lock);
    return flag;
}


// /* Decrements the number of workers in the thread pool
//  * @param 
//  */
// static void decrement_num_workers(struct thread_pool* pool) 
// {
//     assert(pool != NULL);
//     pthread_mutex_lock_c(&pool->num_workers_lock);
//     pool->num_workers--;
//     pthread_mutex_unlock_c(&pool->num_workers_lock);
// }

// /* Set the number of workers in the thread pool
//  * @param 
//  */
// static void set_num_workers(struct thread_pool* pool, int num_workers) 
// {
//     assert(pool != NULL);
//     pthread_mutex_lock_c(&pool->num_workers_lock);
//     pool->num_workers = num_workers;
//     pthread_mutex_unlock_c(&pool->num_workers_lock);
// }


// /* Get the number of workers in the pool
//  * @param pool A pointer to the pool
//  */
// static int get_number_of_workers(struct thread_pool* pool)
// {
//     assert(pool != NULL);
//     pthread_mutex_lock_c(&pool->num_workers_lock);
//     int n = pool->num_workers;
//     pthread_mutex_unlock_c(&pool->num_workers_lock);
//     return n;
// }


// static void worker_free(struct worker *worker)
// {
//     assert(worker != NULL);
//     //pthread_mutex_destroy_c(&worker->local_deque_lock); // Causing problem when run with helgrind
//     free(worker);
// }


/* Removes the worker struct that corresponds to the calling thread from the thread_pool's worker_list.
 * Is synchronized. If the calling thread's tid does not match the tid of any worker's 'thread_id'
 * member, than an error message is printed and the program exits.
 * @param pool The thread_pool
 * @return A pointer to the worker that was removed from the list, or NULL if not found
 *
 static struct worker * remove_calling_thread_from_workers_list(struct thread_pool *pool)
 {
    assert(pool != NULL);
    assert(&pool->workers_list != NULL);
    assert(&pool->workers_list_lock != NULL);

    pthread_t calling_tid = pthread_self();
    pthread_mutex_lock_c(&pool->workers_list_lock);
    struct worker * removed_worker = NULL;
    bool found = true;
    struct list_elem *e;
    for (e = list_begin(&pool->workers_list); e != list_end(&pool->workers_list);
         e = list_next(e)) {
        struct worker *w = list_entry(e, struct worker, elem);
        if (*w->thread_id == calling_tid) { // found calling worker thread's position in the list 
            // remove worker from the list
            // list_remove returns list_elem not element itself
            list_remove(e);
            removed_worker = w;
            break;
        }
    }
    pthread_mutex_unlock_c(&pool->workers_list_lock);
    // if was found, return pointer to the worker, otherwise exit (error in logic of how this is called)
    if (found) {
        return removed_worker; 
    }
    else { 
        #ifdef DEBUG 
        fprintf(stderr, "The worker for the calling thread was not found in remove_calling_thread_from_workers_list()!"
          " Some logical error! Investigate!\n"); 
        #endif
        return NULL;
    }
 }
*****************/

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
    fprintf(stderr, "\n\n\n\n\n\n ERROR \n  %s: returned error code: %d \n\n", msg, err_code);
    #ifdef FAIL_ON_ERROR
        exit(EXIT_FAILURE);
    #endif
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
/*
static void pthread_join_c(pthread_t thread, void **value_ptr)
{
    int rc;
    rc = pthread_join(thread, value_ptr);
    if (rc != 0) {
        error_exit("pthread_join", rc);
    }
}
*/

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



