/* Using a condition variable - APU Fig. 11.15 p. 416 */

#include <pthread.h>
#include <stdbool.h>
#include <stdio.h> // perror()

struct msg {
    struct msg *next_msg;   // linked list
    /* ... more stuff here ... */
};

struct msg *work_queue;

/* Condition variable - statically allocated. If part of struct or otherwise
   dynamically-allocated, must use pthread_cond_init() */
pthread_cond_t queue_ready = PTHREAD_COND_INITIALIZER;

/* Mutex (lock) */
pthread_mutex_t queue_lock = PTHREAD_MUTEX_INITIALIZER;

void process_msg(void)
{
    struct msg *p_msg;

    while (true) {

        /* Lock the mutex - thread will block if already in use */
        // added error exit based off of (http://www.cs.kent.edu/~ruttan/sysprog/lectures/multi-thread/multi-thread.html)
        int rc;
        rc = pthread_mutex_lock(&queue_lock);
        if (rc) {  // 0 if ok, non-zero otherwise. Not sure all are < 0
            perror("pthread_mutex_lock");
            pthread_exit(NULL);
        }
        /* iterate through the queue */
        p_msg = work_queue; // linked list - queue consists of msg
        work_queue = p_msg->next_msg;

        /* unlock */
        rc = pthread_mutex_unlock(&queue_lock);
        if (rc) {
            perror("pthread_mutex_unlock");
            pthread_exit(NULL);
        }
        /* now process the message p_msg */        
    }
}

void enqueue_msg(struct msg *p_msg)
{
    // lock the mutex before accessing work queue
    int rc;
    rc = pthread_mutex_lock(&queue_lock);
    if (rc) {  // 0 if ok, non-zero otherwise. Not sure all are < 0
        perror("pthread_mutex_lock");
        pthread_exit(NULL);
    }
    p_msg->next_msg = work_queue;
    work_queue = p_msg; // fifo?
    rc = pthread_mutex_unlock(&queue_lock);
    if (rc) {
        perror("pthread_mutex_unlock");
        pthread_exit(NULL);
    }

    // "Signal a condition variable" - wake up 1 thread [_broadcast() for all threads waiting on the variable]
    // see note: why ok to not lock before signaling here (416)
    rc = pthread_cond_signal(&queue_ready);
    if (rc) {
        perror("pthread_cond_signal");
        pthread_exit(NULL);
    }    
}
