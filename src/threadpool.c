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
static void worker_free(struct worker *worker);

/**
 * Each thread has this local variable. Even though it is declared like a 
 * global variable it is NOT. 
 * NOTE: remember that there is already 1 thread running the main code besides
 *       the worker threads you create in thread_pool_new().
 */
__thread bool is_worker; 


struct thread_pool {
    struct list/*<Future>*/ gs_queue;  
    pthread_mutex_t gs_queue_lock;      
    pthread_cond_t gs_queue_has_tasks;  
    // TODO: ask TA about conditional variables needed

    bool future_get_called; // if false don't call future_free() 
    struct list_elem gs_queue_elem; 
    struct list_elem deque_elem;

    // for leapfrogging
    int worker_id; // index of the worker that is evaluating the future, if any
    int creator_id; // index of the worker in whose work deque the future
                    // was placed when it was created.

    struct list/*<Worker>*/ worker_list; // TODO: make into array
    // maybe futures_list?
    bool shutdown_requested;                      
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
	if (pool == NULL) { print_error_and_exit("thread_pool_shutdown_and_destroy() pool arg cannot be NULL"); }
    if (pool->shutdown_requested == true) { return; } // if already called thread_pool_shutdown_and_destroy() 

    /* NOTES: 

        TODO: if shutdown called, just stop entire threadpool (ignore ) ???
            No, probably have to finish all futures related to the current external submission (e.g., mergesort)
        TODO: Spec 2.4 "The calling thread must join all worker threads before returning"
        but...says not to use pthread_cancel b/c currently executing futures not guaranteed to complete.
        i.e., all currently executing futures must be completed. And to evaluate the future result, all
        tasks (futures) spawned from the original external submission must complete and be joined.

        Currently there are no tests that have multiple external submissions. May not need to even consider
        what to do if there are >= 2 external submissions and the first is being evaluated and there are
        additional external submissions in the global queue. But if we have to, it seems reasonable to 
        finish the currently executing one, and just cancel any submissions in the global queue. */

    // Wake up any sleeping threads
    if (pthread_cond_broadcast(&pool->gs_queue_has_tasks) != 0) {
        print_error_and_exit("pthread_cond_broadcast() error\n");
    }

	// call pthread_join() on threads to wait for them to finish and reap their resources
	// DON'T use pthread_cancel()
	pool->shutdown_requested = true;

	struct list_elem *e;
	for (e = list_begin(&pool->worker_list); e != list_end(&pool->worker_list); e = list_next(e)) {
		struct worker *worker = list_entry(e, struct worker, elem);
		pthread_join(*worker->thread_id, NULL);

        worker_free(worker);
	}

	// destroy other stuff

}

struct future * thread_pool_submit(struct thread_pool *pool,
                                   fork_join_task_t task,
                                   void * data)
{
    if (pool == NULL) { print_error_and_exit("thread_pool_submit() pool arg cannot be NULL"); }
    if (task == NULL) { print_error_and_exit("thread_pool_submit() task arg cannot be NULL"); }

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
    /* NOTE: Spec 2.4 - "The calling thread may have to help in completing the future being joined, as described
       in section 2.2" */
    if (f == NULL) { print_error_and_exit("future_get() called with NULL parameter"); }

    if (is_worker) {
        if (f->status == COMPLETED) {
            return f->result;
        }
        // Below if statement is for when the threadpool has 1 thread and 
        // multiple futures. You cannot just simply call sem_wait() here
        // because if 1 worker thread calls sem_post() on the first future
        // and the first future generated 2 other futures then the 1 thread
        // would execute 1 of the 2 generated futures and then deadlock.
        else if (f->status == NOT_STARTED) {
            // TODO:??? f->result = (*(f->task_fp))(pool, f->param_for_task_fp);
            f->status = COMPLETED;
            sem_post(&f->semaphore); // increment_and_wake_a_waiting_thread_if_any()
        }
        return f->result;
    } else { 
        // thread executing this is not a worker thread so it is safe to 
        // sem_wait()
        sem_wait(&f->semaphore); // decrement_and_block_if_the_result_is_negative()
        // when the value is incremented by worker_function the result will be 
        // computed
        return f->result;
    }
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
	struct thread_pool *pool = pool_and_worker->pool;
	struct worker *worker = pool_and_worker->worker;

	while(true) {
		// if there are futures in local deque execute them first
		if(!list_empty(&worker->local_deque)) {
			
            // TODO: check errors
            pthread_mutex_lock(&worker->local_deque_lock);
			// "Workers execute tasks by removing them from the top" from 2.1 of spec
			struct future *future = list_entry(list_pop_front(&worker->local_deque), struct future, deque_elem);
			pthread_mutex_unlock(&worker->local_deque_lock);

			future->result = (*(future->task_fp))(pool, future->param_for_task_fp);
            future->status = COMPLETED;
			sem_post(&future->semaphore); // increment_and_wake_a_waiting_thread_if_any()
		} // else if there are futures in gs_queue execute them second 
		else if (!list_empty(&pool->gs_queue)) {
			pthread_mutex_lock(&pool->gs_queue_lock);
			struct future *future = list_entry(list_pop_front(&pool->gs_queue), struct future, gs_queue_elem);
			pthread_mutex_unlock(&pool->gs_queue_lock);

			future->result = (*(future->task_fp))(pool, future->param_for_task_fp);
            future->status = COMPLETED;
			sem_post(&future->semaphore); // increment_and_wake_a_waiting_thread_if_any()
		} 
		// TODO: else work stealing...
		// "Otherwise, the worker attempts to steal tasks to work on from the
		//  bottom of other threads' queues"
	}
	
    return NULL;
}

/**
 * Initialize the worker struct
 * @param worker = pointer to memory allocated for this struct
 * @param worker_thread_index = the index of the worker in the thread_pool's worker_list
 * @return pointer to the initialized worker struct
 */
static struct worker * worker_init(struct worker *worker, unsigned int worker_thread_index) 
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

/**
 * Free all memory allocated to the worker struct.
 * @param worker = pointer to the worker to free
 */
static void worker_free(struct worker *worker)
{
    if (worker == NULL) { print_error_and_exit("worker_free(): passed NULL arg\n"); }
    //free(worker->local_deque);
    if (pthread_mutex_destroy(&worker->local_deque_lock) != 0) {  print_error_and_exit("pthread_mutex_destroy\n"); }
    free(worker);
}
