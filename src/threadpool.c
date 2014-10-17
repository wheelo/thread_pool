#include "threadpool.h"

struct single_thread {
<<<<<<< HEAD
	struct list_elem elem; // doubly linked list node to be able to add to
						   // generic coded in list.c & list.h

	// http://stackoverflow.com/questions/6420090/pthread-concepts-in-linux
	pthread_t* thread; 
	bool internalSubmission; // if false external submission
	// TODO: use thread local storage 2.4 in spec

	// local deque of futures
	struct list/*<Future>*/ localDeque;
};

struct thread_pool {
	struct list/*<pthread_t>*/ listOfThreads;
	struct list/*<Future>*/ GSQueue; // global submission queue
	bool isShuttingDown;
=======
    struct list_elem dlln; // doubly linked list node to be able to add to
                           // generic coded in list.c & list.h

    // http://stackoverflow.com/questions/6420090/pthread-concepts-in-linux
    pthread_t* thread; 

    // local deque of futures
};

struct thread_pool {
    struct list listOfThreads;
    struct list GSQueue;    // global submission queue
>>>>>>> a4ff2a568fdd03adb23b3de086b01373bb1cd257
};

/*
enum futureState {
  notStated
  inProgress
  completed
}
*/   

/**
 * From 2.4 Basic Strategy
 * Should store a pointer to the function to be called, any data to be passed
 * to that function, as well as the result(when available).
 */
struct future { 
	struct list_elem elem; // necessary to add to struct list GSQueue
	int indexWithinLocalDeque; // call list_size()
	bool inGSQueue;

    // enum futureState

	// any data to be passed to below function pointer
    void* data;
	fork_join_task_t functionPointer; // = pointer to the function to be called

    void* result;
    int depth; // see the leap frogging paper
};

static void * functionToBeExecutedByEachThreadInParrallel(void *arg) {
    // what code to execute and how do we get it here???
    return NULL;
}

void printError(char* errorMessage) {
	write(2, errorMessage, strlen(errorMessage));
}

/**
 * @param nthreads = number of threads to create
 */
struct thread_pool * thread_pool_new(int nthreads) {
	if (nthreads < 1) {
		printError("You must create at least 1 thread\n");
		return NULL;
	}

	// http://stackoverflow.com/questions/1963780/when-should-i-use-malloc-in-c-and-when-dont-i
	struct thread_pool* threadPool = (struct thread_pool*) 
								malloc(sizeof(struct thread_pool));
	if (threadPool == NULL) {
		printError("malloc() error\n");
	}
	list_init(&threadPool->listOfThreads);
	list_init(&threadPool->GSQueue);

	struct list_elem* e;
	struct list* threads = &threadPool->listOfThreads;
	for(e = list_begin(threads); e != list_end(threads); e = list_next(e)) {
		struct single_thread* currentThread = list_entry(e, 
												struct single_thread, elem);
		// http://stackoverflow.com/questions/6990888/c-how-to-create-thread-using-pthread-create-function
		if (pthread_create(currentThread->thread, NULL, 
			functionToBeExecutedByEachThreadInParrallel, NULL) == -1) {
			printError("In thread_pool_new() error creating pthread\n"); 
    		exit(-1);
		}
	}
	
	return threadPool;
}

void thread_pool_shutdown_and_destroy(struct thread_pool *threadPool) {
	// call pthread_join() on threads to wait for them to finish and reap
	// their resources
	// DON'T use pthread_cancel()

	// what signaling strategy to use?
}

struct future * thread_pool_submit(struct thread_pool *threadPool, 
	                               fork_join_task_t task, void * data) {
	// initialize fields in Future struct

	// future pointer gets added to GSQueue

	return NULL;
}

/**
 * Get result of computation.
 */
void * future_get(struct future *futureStruct) {
    // How do you find future? iterate through list of GSQueue and worker thread deques?

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

void future_free(struct future *futureStruct) {

}