#include "threadpool.h"

struct thread_pool {
	struct list listOfThreads;
	// ???
};

/**
 * From 2.4 Basic Strategy
 * Should store a pointer to the function to be called, any data to be passed
 * to that function, as well as the result(when available).
 */
struct future { // = ???
	// define appropriate variables to record the state of a future
	// started, in progress, has completed
	// which 
};

struct thread_pool * thread_pool_new(int nthreads) {
	return NULL;
}

void thread_pool_shutdown_and_destroy(struct thread_pool *pool) {

}

struct future * thread_pool_submit(struct thread_pool *pool, 
	                               fork_join_task_t task, void * data) {
	return NULL;
}

void * future_get(struct future *futureStruct) {
	return NULL;
}

void future_free(struct future *futureStruct) {

}