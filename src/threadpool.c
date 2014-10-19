#include "threadpool.h"

// private functions for this class that must be declared here to be called below
static void * thread_function(void *arg);
static struct worker * worker_init(struct worker * wkr, unsigned int worker_number);

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
    pthread_cond_t gs_queue_has_tasks;  /* i.e., global queue not empty */
    // TODO: ask TA about conditional variables needed

	struct list/*<Worker>*/ worker_list;
	struct list/*<Future>*/ future_list;
	pthread_mutex_t future_list_lock;

	bool shutdown_requested;                                        
};

/**
 * A 'worker' consists of both the thread, the local deque of futures, and other
 * associated data 
 */
struct worker {
    pthread_t* thread;

    unsigned int worker_thread_idx;

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
    void* param_for_thread_fp; 
    // Note: fork_join_task_t defn
    // void * (* fork_join_task_t) (struct thread_pool *pool, void *data);
    fork_join_task_t thread_fp;   /* pointer to the function to be called */

    void* result;
	
    future_status status;       // NOT_STARTED, IN_PROGRESS, or COMPLETED
    //bool is_done; 

    bool is_internal_task;      // if thread_pool_submit() called by a worker thread

    /* ADD SEMAPHORE !? */

    /* https://piazza.com/class/hz79cl74dfv3pf?cid=192
    since future_get() takes only a ptr to a future, for stealing, need to
    access the bool 'currently_has_internal_submission' member in worker.
    so must be able to access this from future. If we go this route, then we
    need the following: 

    bool is_internal_task;  // i.e., not external
    struct worker *owning_worker;
    // worker must have ref to pool...or have TLS variable for pool

    */

	
    // FOR LEAPFROGGING 
    // int idx_in_local_deque;    // call list_size()
	// int depth; // see the leap frogging paper

    /* prob not necessary: bool in_gs_queue; */

    // ?? piazza __thread bool is_internal_submission; // if false: external submission

    // bool future_get_called;     // don't call future_free() if false

    struct list_elem elem;
};

/**
 * @param nthreads = number of threads to create
 */
struct thread_pool * thread_pool_new(int nthreads) {
	if (nthreads < 1) {
		print_error("You must create at least 1 thread\n");
		return NULL;
	}

	is_worker = false;

    // Initialize the thread pool
	struct thread_pool* pool = (struct thread_pool*) 
                                   malloc(sizeof(struct thread_pool));
	if (pool == NULL) {
      print_error("malloc() error\n");
	} 

    list_init(&pool->gs_queue);
    
    // Initialize mutex for the global queue
    if (pthread_mutex_init(&pool->gs_queue_lock, NULL) != 0) {
        print_error("pthread_mutex_init() failed\n");
        exit(EXIT_FAILURE);
    }

    // Initialize condition variable used to broadcast to worker threads that
    // tasks are available in the global submission queue 
    if (pthread_cond_init(&pool->gs_queue_has_tasks, NULL) != 0) {
        print_error("pthread_cond_init() failed\n");
        exit(EXIT_FAILURE);
    }

    // initialize all the workers 
    list_init(&pool->worker_list);
    list_init(&pool->future_list);

    // Initialize mutex for the list of available futures
    if (pthread_mutex_init(&pool->gs_queue_lock, NULL) != 0) {
        print_error("pthread_mutex_init() failed\n");
        exit(EXIT_FAILURE);
    }

	int i;
	for (i = 0; i < nthreads; ++i) {
	    // malloc worker struct and initialize all of its members 
		struct worker *wkr = (struct worker *) malloc(sizeof(struct worker));        
        wkr = worker_init(wkr, i);

        // add to the pool's list of workers 
        list_push_back(&pool->worker_list, &wkr->elem);
	}

    pool->shutdown_requested = false;

    // iterate through the list of workers and create their threads.
	struct list_elem* e;
	for (e = list_begin(&pool->worker_list); e != list_end(&pool->worker_list);
         e = list_next(e)) {

        struct worker* current_worker = list_entry(e, struct worker, elem);
        /* note: unlike process functions this and other pthread_ and sem_ functions
             can return error codes other than  -1, and return 0 if successful, so check if != 0 */

        // NOTE: changed 4th arg, the arguments for thread_function, to be
        //       the worker struct itself. When current_worker's thread is 
        //       created and thread_function exectures, it will have to have
        //       a way to do stuff like access its local_deque.
        // remove these notes later
        if ( pthread_create(current_worker->thread,  // the worker's thread member [will be set to the thread id]
                            NULL, // default attributes
                            (void *) &thread_function, // what thread executes
                            (struct worker *) current_worker) /* argument to thread_function */ 
                                != 0) {
	  		print_error("pthread_create()\n");
			exit(EXIT_FAILURE);
	    }
    }
	return pool;
}

void thread_pool_shutdown_and_destroy(struct thread_pool *pool) {

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
    if (pool == NULL) {
        print_error("thread_pool_submit: pool arg is NULL"); exit(EXIT_FAILURE);
    }
    if (task == NULL) {
        print_error("thread_pool_submit: task arg is NULL"); exit(EXIT_FAILURE);
    }
    // check data?

    /* Initialize Future struct */
    struct future *p_future = (struct future*) malloc(sizeof(struct future));
    p_future->param_for_thread_fp = data;
    p_future->thread_fp = task;
    p_future->result = NULL;
    p_future->status = NOT_STARTED;

    // p_future->semaphore???

    /* if this thread is not a worker, add future to global queue */
    if (!is_worker) {

    	/* Acquire lock for the global submission queue */
    	if (pthread_mutex_lock(&pool->gs_queue_lock) != 0) {
        	print_error("pthread_mutex_lock() error\n");
        	exit(EXIT_FAILURE);
    	}
	
	    	/* DO ALL NEW TASKS SUBMITTED TO THE POOL ALWAYS GO TO THE GLOBAL QUEUE? no
	       - if calling thread is external, then add to global queue.
	            - currently tests only have 1 external (initial) submission. More may be added
	       - if internal: add to its own queue
	       - only IDLE threads look at global queue
	       (https://piazza.com/class/hz79cl74dfv3pf?cid=186) */


	    /* add future to global queue (critical section) */
	    list_push_back(&pool->gs_queue, &p_future->elem);

	    /* Broadcast to sleeping threads that work is available (in queue) */
	    if (pthread_cond_broadcast(&pool->gs_queue_has_tasks) != 0) {
	        print_error("pthread_cond_broadcast() error\n");
	        exit(EXIT_FAILURE);        
	    }

	    /* release mutex lock */
	    if (pthread_mutex_unlock(&pool->gs_queue_lock) != 0) {
	        print_error("pthread_mutex_unlock() error\n");
	        exit(EXIT_FAILURE);
	    }

	} 
    else { /* is a worker thread */
        // add to the future list
        if (pthread_mutex_lock(&pool->future_list_lock) != 0) {
            print_error("pthread_mutex_lock() error\n");
            exit(EXIT_FAILURE);            
        }
        list_push_back(&pool->future_list, &p_future->elem);
        if (pthread_mutex_unlock(&pool->future_list_lock) != 0) {
            print_error("pthread_mutex_unlock() error\n");
            exit(EXIT_FAILURE);            
        }        

        // add to the top of local_deque of the worker thread calling the thread_pool_submit()
        pthread_t this_thread_id = pthread_self();
        // loop through pool's worker_list to find the worker struct with this thread's tid
        struct list_elem* e;
        for (e = list_begin(&pool->worker_list); e != list_end(&pool->worker_list);
            e = list_next(e)) {

            struct worker* current_worker = list_entry(e, struct worker, elem);

            if (*current_worker->thread == this_thread_id) {
                if (pthread_mutex_lock(&current_worker->local_deque_lock) != 0) {
                    print_error("pthread_mutex_lock() error\n");
                    exit(EXIT_FAILURE);                  
                }
                // add future to the worker thread's local dequeue
                list_push_front(&current_worker->local_deque, &p_future->elem);       
                if (pthread_mutex_unlock(&current_worker->local_deque_lock) != 0) {
                    print_error("pthread_mutex_unlock() error\n");
                    exit(EXIT_FAILURE);            
                }                            
            }
        }





	}

	return NULL;
}

/**
 * Get result of computation.
 * Leapfrogging Paper = http://cseweb.ucsd.edu/~calder/papers/PPoPP-93.pdf
 */
void * future_get(struct future *f) {
    if (f == NULL) {
        print_error("future_get() called with NULL parameter");
        exit(EXIT_FAILURE);
    }

    if (is_worker) {
    	// TODO:
 		return NULL;
    } else {
    	// TODO:
    	return NULL;
    }
}

void future_free(struct future *f) {
    if (f == NULL) {
        print_error("future_free() called with NULL parameter");
        exit(EXIT_FAILURE);
    }

    // ...
}

static void * thread_function(void *arg) {
	is_worker = true;
    struct worker *worker;      // the worker that has this thread as a member...
    /* arg should be the worker executing this thread */
    if (arg == NULL) {
        print_error("thread_function argument null\n");
        exit(EXIT_FAILURE);
    }
    worker = (struct worker *) arg; /* typecast arg */


    /* ADD task to local_deque */
    
    /* where are the tasks taken from gs_queue added to local deque? 
    (1) worker inits its local_deque_lock (moved to worker_init())
        worker locks its local_deque  

        execute tasks in own deque.
        internal submissions added to top of deque (list_push_front)
        thread executes them in LIFO [stack] order. 
        [continue to (2) if completed (1)]
    (2) Check the gs_queue
            [must acquire gs_queue_lock mutex to check size]
            while (gs_queue.size != 0):
                dequeue [list_pop_back()] from gs_queue
                release gs_queue_lock
                add dequeued task to local_deque
                execute task
            [no more tasks in global q]    
            DO NOT CALL pthread_cond_wait() yet! Must do (3), then 
    (3) Try to steal from ** BOTTOM ** of other worker's dequeue's [list_pop_back]
                // need to read leapfrog paper make sure...but what spec says

    (4) acquire lock for gs_queue
        pthread_cond_wait( worker's cond var for gs queue )
        ...when it awakens due to broadcast being called, it will have reacquired the 
        gsqueue lock.



    */
    return NULL;
}

/* Initialize the worker struct
 * Arguments:
 *    wkr - pointer to memory allocated for this struct
 *    worker_number - the index of the worker in the thread_pool's (( array? list? ))
 * Return: pointer to the initialized worker struct
 */
static struct worker * worker_init(struct worker * wkr, unsigned int worker_number) {
    // malloc the worker's thread
    pthread_t *ptr_thread = (pthread_t *) malloc(sizeof(pthread_t)); 
    if (ptr_thread == NULL) {
        print_error("malloc error\n");
        exit(EXIT_FAILURE);
    }
    wkr->thread = ptr_thread;

    // initialize the worker's deque
    list_init(&wkr->local_deque); // ...its local_deque

    // lock for the worker's deque
    if (pthread_mutex_init(&wkr->local_deque_lock, NULL) != 0) {
        print_error("pthread_mutex_init()\n");
        exit(EXIT_FAILURE);
    }

    // the index of the worker in the thread pool's array [list?]
    wkr->worker_thread_idx = worker_number;

    wkr->currently_has_internal_submission = false;

    return wkr;
}