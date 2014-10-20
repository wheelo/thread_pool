#include "threadpool.h"

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
static void * worker_function(struct thread_pool_and_current_worker *pool_and_worker);
static struct worker * worker_init(struct worker * worker, unsigned int worker_thread_index);

/**
 * Each thread has this local variable. Even though it is declared like a 
 * global variable it is NOT. 
 * NOTE: remember that there is already 1 thread running the main code besides
 *       the worker threads you create in thread_pool_new().
 */
__thread bool is_worker; 

/**
 * From 2.4 Basic Strategy
 * Should store a pointer to the function to be called, any data to be passed
 * to that function, as well as the result(when available).
 */
struct future {
    void* param_for_task_fp; 
    fork_join_task_t task_fp; // pointer to the function to be executed by worker

    void* result;
    sem_t semaphore; // for if result is finished computing
	
    FutureStatus status; // NOT_STARTED, IN_PROGRESS, or COMPLETED

    // TODO: TA question = How to steal future from another worker if you don't
    //                     wont to take bottom external
    bool is_internal_task; // if thread_pool_submit() called by a worker thread

    bool future_get_called; // if false don't call future_free() 
    struct list_elem gs_queue_elem; 
    struct list_elem deque_elem;
};

/**
 * A 'worker' consists of both the thread, the local deque of futures, and other
 * associated data 
 */
struct worker {
    pthread_t* thread_id;

    unsigned int worker_thread_index;

    struct list/*<Future>*/ local_deque;
    pthread_mutex_t local_deque_lock;

    bool currently_has_internal_submission; // if false: external submission

    struct list_elem elem; // doubly linked list node to be able to add to
                           // generic coded in list.c & list.h
};

struct thread_pool {
    struct list/*<Future>*/ gs_queue;  
    pthread_mutex_t gs_queue_lock;      
    pthread_cond_t gs_queue_has_tasks;  /* i.e., global queue not empty */
    // TODO: ask TA about conditional variables needed

	struct list/*<Worker>*/ worker_list; // TODO: make into array

	bool shutdown_requested;                                        
};

/**
 * @param nthreads = number of threads to create
 */
struct thread_pool * thread_pool_new(int nthreads) 
{
	if (nthreads < 1) { print_error_and_exit("You must create at least 1 thread\n"); }

	is_worker = false;

    // Initialize the thread pool
	struct thread_pool* pool = (struct thread_pool*) malloc(sizeof(struct thread_pool));
	if (pool == NULL) { print_error_and_exit("malloc() error\n"); } 

    list_init(&pool->gs_queue);
    
    // Initialize mutex for the global submission queue
    if (pthread_mutex_init(&pool->gs_queue_lock, NULL) != 0) { print_error_and_exit("pthread_mutex_init() error\n"); }

    // Initialize condition variable used to broadcast to worker threads that
    // tasks are available in the global submission queue 
    if (pthread_cond_init(&pool->gs_queue_has_tasks, NULL) != 0) { print_error_and_exit("pthread_cond_init() error\n"); }

    list_init(&pool->worker_list);

    if (pthread_mutex_init(&pool->gs_queue_lock, NULL) != 0) { print_error_and_exit("pthread_mutex_init() error\n"); }

    // Initialize all workers and add to threadpool worker list
	int i;
	for (i = 0; i < nthreads; ++i) {
		struct worker *worker = (struct worker *) malloc(sizeof(struct worker));        
        worker = worker_init(worker, i); 

        list_push_back(&pool->worker_list, &worker->elem);
	}

    pool->shutdown_requested = false;

    // Iterate through the list of workers and create their threads.
	struct list_elem* e;
	for (e = list_begin(&pool->worker_list); e != list_end(&pool->worker_list);
         e = list_next(e)) {

        struct worker* current_worker = list_entry(e, struct worker, elem);

        // to be passed as a parameter to worker_function()
    	struct thread_pool_and_current_worker *pool_and_worker = 
    	        (struct thread_pool_and_current_worker*) malloc(sizeof(struct thread_pool_and_current_worker));
    	pool_and_worker->pool = pool;
    	pool_and_worker->worker = current_worker;

        if (pthread_create(current_worker->thread_id, NULL, (void *) worker_function, pool_and_worker) != 0) { 
        	print_error_and_exit("pthread_create() error\n"); 
        }
    }
	return pool;
}

void thread_pool_shutdown_and_destroy(struct thread_pool *pool) 
{

    // broadcast - wake up threads asleep


	// call pthread_join() on threads to wait for them to finish and reap
	// their resources
	// DON'T use pthread_cancel()

	// what signaling strategy to use?
}

struct future * thread_pool_submit(struct thread_pool *pool,
                                   fork_join_task_t task,
                                   void * data)
{
    if (pool == NULL) { print_error_and_exit("thread_pool_submit: pool arg is NULL"); }
    if (task == NULL) { print_error_and_exit("thread_pool_submit: task arg is NULL"); }

    /* Initialize Future struct */
    struct future *p_future = (struct future*) malloc(sizeof(struct future));
    p_future->param_for_task_fp = data;
    p_future->task_fp = task;
    p_future->result = NULL;
    p_future->status = NOT_STARTED;
    if (sem_init(&p_future->semaphore, 0, 0) < 0) { print_error_and_exit("sem_init() error"); }

    // if this thread is not a worker, add future to global queue 
    if (!is_worker) {
    	// Acquire lock for the global submission queue 
    	if (pthread_mutex_lock(&pool->gs_queue_lock) != 0) { print_error_and_exit("pthread_mutex_lock() error\n"); }
	
	    // add external future to global queue (critical section) 
	    list_push_back(&pool->gs_queue, &p_future->gs_queue_elem);

	    /* Broadcast to sleeping threads that work is available (in queue) */
	    if (pthread_cond_broadcast(&pool->gs_queue_has_tasks) != 0) { print_error_and_exit("pthread_cond_broadcast() error\n"); }

	    /* release mutex lock */
	    if (pthread_mutex_unlock(&pool->gs_queue_lock) != 0) { print_error_and_exit("pthread_mutex_unlock() error\n"); }
	} 
    else { /* is a worker thread */        

        // add to the top of local_deque of the worker thread calling the thread_pool_submit()
        pthread_t this_thread_id = pthread_self();
        // loop through pool's worker_list to find the worker struct with this thread's tid

        struct list_elem* e;
        for (e = list_begin(&pool->worker_list); e != list_end(&pool->worker_list);
            e = list_next(e)) {

            struct worker* current_worker = list_entry(e, struct worker, elem);

            if (*current_worker->thread_id == this_thread_id) {
                if (pthread_mutex_lock(&current_worker->local_deque_lock) != 0) { print_error_and_exit("pthread_mutex_lock() error\n"); }
                // add internal future to the worker thread's local dequeue
                list_push_front(&current_worker->local_deque, &p_future->deque_elem);       
                if (pthread_mutex_unlock(&current_worker->local_deque_lock) != 0) { print_error_and_exit("pthread_mutex_unlock() error\n"); }                            
            }
        }
	}
	return p_future;
}

/**
 * Get result of computation.
 * Leapfrogging Paper = http://cseweb.ucsd.edu/~calder/papers/PPoPP-93.pdf
 */
void * future_get(struct future *f) 
{
    if (f == NULL) { print_error_and_exit("future_get() called with NULL parameter"); }
    sem_wait(&f->semaphore);
    return f->result;
}

void future_free(struct future *f) 
{
    if (f == NULL) { print_error_and_exit("future_free() called with NULL parameter"); }
    free(f);
}

/**
 * This is the logic for how a worker thread decides to execute a 
 * task.
 */
static void * worker_function(struct thread_pool_and_current_worker *pool_and_worker) 
{
	is_worker = true; // = thread local variable
	//struct thread_pool *pool = pool_and_worker->pool;
	//struct worker *worker = pool_and_worker->worker;

	while(true) {
		// if there are futures in local deque execute them first
		if(!list_empty(&worker->local_deque)) {
			// "Workers execute tasks by removing them from the top" from 2.1 of spec
			//struct future *future = list_entry(list_pop_front(&worker->local_deque), struct future, deque_elem);
		}

		// acquire local deque lock
		// 1) execute future in local deque

		// future = pop_off_local_deque

		// future->result

		// 2) execute future at top of gs_queue


		//future->task_fp
		//sem_post(&future->semaphore);
	}
	
    return NULL;
}

/**
 * Initialize the worker struct
 * @param worker = pointer to memory allocated for this struct
 * @param worker_thread_index = the index of the worker in the thread_pool's worker_list
 * @return pointer to the initialized worker struct
 */
static struct worker * worker_init(struct worker * worker, unsigned int worker_thread_index) 
{
    // malloc the worker's thread
    pthread_t *ptr_thread = (pthread_t *) malloc(sizeof(pthread_t)); 
    if (ptr_thread == NULL) { print_error_and_exit("malloc error\n"); }
    worker->thread_id = ptr_thread;

    worker->worker_thread_index = worker_thread_index;

    list_init(&worker->local_deque); 
    if (pthread_mutex_init(&worker->local_deque_lock, NULL) != 0) { print_error_and_exit("pthread_mutex_init()\n"); }

    worker->currently_has_internal_submission = false; 
    return worker;
}