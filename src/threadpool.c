#include "threadpool.h"

static void * thread_function(void *arg);

struct worker_thread {
	struct list_elem elem; // doubly linked list node to be able to add to
						   // generic coded in list.c & list.h

	// http://stackoverflow.com/questions/6420090/pthread-concepts-in-linux
	pthread_t* thread;
	// TODO: use thread local storage 2.4 in spec

	// local deque of futures
	struct list/*<Future>*/ local_deque;
    bool currently_has_internal_submission; // if false: external submission
};

struct thread_pool {
	struct list/*<worker_thread>*/ threads_list; 

	struct list/*<Future>*/ gs_queue; /* global submission queue */
	bool is_shutting_down;

    pthread_mutex_t gs_queue_lock;  /* mutex lock for global submission queue */
    // TODO: semaphore or condition variable
};

typedef enum future_status_ {
  NOT_STARTED,
  IN_PROGRESS,
  COMPLETED
} future_status;

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
	
    future_status status;   // NOT_STARTED, IN_PROGRESS, or COMPLETED

    /* ADD SEMAPHORE !? */


	
    // FOR LEAPFROGGING 
    // int idx_in_local_deque;    // call list_size()
	// int depth; // see the leap frogging paper

    /* prob not necessary: bool in_gs_queue; */

    // ?? piazza __thread bool is_internal_submission; // if false: external submission
    /* bool is_internal_submission; // if false: external submission */

    // bool future_get_called;     // don't call future_free() if false

    struct list_elem elem;     // necessary to add to struct list gs_queue
};

/**
 * @param nthreads = number of threads to create
 */
struct thread_pool * thread_pool_new(int nthreads)
{
	if (nthreads < 1) {
		print_error("You must create at least 1 thread\n");
		return NULL;
	}

	// http://stackoverflow.com/questions/1963780/when-should-i-use-malloc-in-c-and-when-dont-i
	struct thread_pool* pool = (struct thread_pool*)
								malloc(sizeof(struct thread_pool));
	if (pool == NULL) {
		print_error("malloc() error\n");
	}


    // TODO: 
    // at some point (not nec. right here) init semaphore w/ value nthreads,
    //       or condition var(s)
    

    if ( pthread_mutex_init(&pool->gs_queue_lock, NULL) != 0 ) {
        print_error("pthread_mutex_init(&gs_queue_lock, NULL)\n");
    }

    pool->is_shutting_down = false;
    list_init(&pool->threads_list);
	list_init(&pool->gs_queue);

	int i;
	for (i = 0; i < nthreads; ++i) {
	    // malloc worker thread
		struct worker_thread *p_thread_i = (struct worker_thread *) malloc(sizeof(struct worker_thread));

		// malloc its thread
		pthread_t *ptr_thread = (pthread_t *) malloc(sizeof(pthread_t));

        p_thread_i->thread = ptr_thread;

        // Q: why not malloc list_elem? 


		// malloc the worker thread's local deque of futures
        list_push_back(&pool->threads_list, &p_thread_i->elem);
	}

	struct list_elem* e;
	for (e = list_begin(&pool->threads_list); e != list_end(&pool->threads_list);
         e = list_next(e)) {

        struct worker_thread* current_thread = list_entry(e, struct worker_thread, elem);
        /* note: unlike process functions this and other pthread_ and sem_ functions
             can return error codes other than  -1, and return 0 if successful, so check if != 0 */

        if ( pthread_create(current_thread->thread, NULL, thread_function, NULL) != 0) {
	  		print_error("In thread_pool_new() error creating pthread\n");
			exit(EXIT_FAILURE);
	    }
    }

	return pool;
}

void thread_pool_shutdown_and_destroy(struct thread_pool *pool)
{
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
        print_error("thread_pool_submit: pool arg is NULL");
        exit(EXIT_FAILURE);
    }

    if (task == NULL) {
        print_error("thread_pool_submit: pool arg is NULL");
        exit(EXIT_FAILURE);
    }

    // check data?

    /* Initialize Future struct */
    struct future *p_future = (struct future*) malloc(sizeof(struct future));
    p_future->param_for_thread_fp = data;
    p_future->thread_fp = task;
    p_future->result = NULL;
    p_future->status = NOT_STARTED;

	// future pointer gets added to gs_queue
    list_push_back(&gs_queue, &p_future->elem);

	return NULL;
}

/**
 * Get result of computation.
 * Leapfrogging Paper = http://cseweb.ucsd.edu/~calder/papers/PPoPP-93.pdf
 */
void * future_get(struct future *f)
{
    if (f == NULL) {
        print_error("future_free() called with NULL parameter");
        exit(EXIT_FAILURE);
    }
    // How do you find future? iterate through list of gs_queue and worker thread deques?

    /*
    if future is completed
      return result
    else if future has notStarted
      a unbusy worker thread can steal it and execute it itself
    else // future inProgress
      2.2 in spec "help executing tasks spawned by the task being joined" ??
    */

    return NULL;
}

void future_free(struct future *f)
{
    if (f == NULL) {
        print_error("future_free() called with NULL parameter");
        exit(EXIT_FAILURE);
    }

    // ...
}

static void * thread_function(void *arg)
{
    // what code to execute and how do we get it here???
    return NULL;
}
