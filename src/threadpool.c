#include "threadpool.h"

struct single_thread {
    struct list_elem dlln; // doubly linked list node to be able to add to
                           // generic coded in list.c & list.h

    // http://stackoverflow.com/questions/6420090/pthread-concepts-in-linux
    pthread_t* thread; 

    // local deque of futures
};

struct thread_pool {
    struct list listOfThreads;
    struct list GSQueue;    // global submission queue
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
    int indexWithinLocalDeque; // call list_size()
    bool inGSQueue;

    // enum futureState

    // any data to be passed to below function pointer
    // fork_join_task_t? = pointer to the function to be called

    void* result;
    int depth; // 
};

static void * functionToBeExecutedByEachThreadInParrallel(void *arg) {
    // what code to execute and how do we get it here???
    return NULL;
}

/**
 * @param nthreads = number of threads to create
 */
struct thread_pool * thread_pool_new(int nthreads) {
    if (nthreads < 1) {
        printf("You must create at least 1 thread\n");
        return NULL;
    }

    // http://stackoverflow.com/questions/1963780/when-should-i-use-malloc-in-c-and-when-dont-i
    struct thread_pool* threadPool = (struct thread_pool*) 
                                malloc(sizeof(struct thread_pool));
    list_init(&threadPool->listOfThreads);

    struct list_elem* e;
    struct list* threads = &threadPool->listOfThreads;
    for(e = list_begin(threads); e != list_end(threads); e = list_next(e)) {
        struct single_thread* currentThread = list_entry(e, 
                                                struct single_thread, dlln);
        // http://stackoverflow.com/questions/6990888/c-how-to-create-thread-using-pthread-create-function
        if (pthread_create(currentThread->thread, NULL, 
            functionToBeExecutedByEachThreadInParrallel, NULL) == -1) {
            printf("In thread_pool_new() error creating pthread\n"); 
            exit(-1);
        }
    }
    
    return threadPool;
}

void thread_pool_shutdown_and_destroy(struct thread_pool *threadPool) {

    // call pthread_join() on threads to wait for them to finish and reap
    // their resources

}

struct future * thread_pool_submit(struct thread_pool *threadPool, 
                                   fork_join_task_t task, void * data) {
    return NULL;
}

/**
 * Get result of computation.
 */
void * future_get(struct future *futureStruct) {
    // How do you find future? iterate through list of GSQueue and worker thread deques?

    /*
    if future has completed
      return result
    else 
      a unbusy worker thread can steal it and execute it itself
    */ 

    return NULL;
}

void future_free(struct future *futureStruct) {

}