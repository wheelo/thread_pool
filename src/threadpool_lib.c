#include <time.h>
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <semaphore.h>
#include <string.h>
#include <errno.h>
#include "threadpool_lib.h"



#define ERR     2


// http://www.guyrutenberg.com/2007/09/22/profiling-code-using-clock_gettime/
struct timespec timespec_diff(struct timespec start, struct timespec end)
{
    struct timespec temp;
    if ((end.tv_nsec-start.tv_nsec)<0) {
        temp.tv_sec = end.tv_sec-start.tv_sec-1;
        temp.tv_nsec = 1000000000+end.tv_nsec-start.tv_nsec;
    } else {
        temp.tv_sec = end.tv_sec-start.tv_sec;
        temp.tv_nsec = end.tv_nsec-start.tv_nsec;
    }
    return temp;
}

void timespec_print(struct timespec ts, char *buf, size_t buflen)
{
    snprintf(buf, buflen, "%lld.%.9ld", (long long)ts.tv_sec, ts.tv_nsec);
}

/**
 * Count number of threads by scanning /proc/self/status
 * for the Threads: ... line
 */
 int
 count_number_of_threads(void)
 {
    FILE * p = fopen("/proc/self/status", "r");
    while (!feof(p)) {
        int threadsleft;
        char buf[128];
        fgets(buf, sizeof buf, p);
        if (sscanf(buf, "Threads: %d\n", &threadsleft) != 1)
            continue;

        fclose(p);
        return threadsleft;
    }
    printf("Internal error, please send email to gback@cs.vt.edu\n");
    abort();
}

void print_error_and_exit(char* error_message) 
{
    write(ERR, error_message, strlen(error_message));
    exit(EXIT_FAILURE);
}


/***************************************************************************
 * Wrapper functions for malloc, pthread.h, and semaphore.h
 *   - These make code more readable by moving the original function and 
 *     checking of the return value for errors.
 *   - All wrapped functions have the same parameters as the original
 ***************************************************************************/


void print_error_and_exit(char *fn_name, int err_code)
{
    fprintf(stderr, "%s(): returned error code: %s\n", fn_name, strerror(err_code));
    exit(EXIT_FAILURE);
}

/* malloc */
 void * checked_malloc(int size)
 {
    void *p = malloc(size);
    if (p == NULL) {
        perror("malloc() failed to allocate memory\n");
        exit(EXIT_FAILURE);
    }
    return p;
}


/* POSIX Threads (pthread.h) */
// Thread Routines
void pthread_create_c(pthread_t *thread, const pthread_attr_t *attr, 
                      void *(*start_routine)(void *), void *arg)
{
    int rc; // return code
    rc = pthread_create(thread, attr, start_routine, (void *)arg);
    if (rc != 0) {
        print_error_and_exit("pthread_create", rc);
    }
}

//int pthread_detach(pthread_t thread)
//int pthread_equal(pthread_t t1, pthread_t t2)
//void pthread_exit(void *value_ptr)

void pthread_join_c(pthread_t thread, void **value_ptr)
{
    int rc;
    rc = pthread_join(thread, value_ptr);
    if (rc != 0) {
        print_error_and_exit("pthread_join", rc);
    }
}

//int pthread_cancel(pthread_t thread)
//int pthread_once(pthread_once_t *once_control, void
//    (*init_routine)(void))

pthread_t pthread_self_c(void)
{
    // does not return any errors, included for convenience
    return pthread_self(void);
}

//int pthread_atfork(void (*prepare)(void), void (*parent)(void), void
//    (*child)(void))


// Mutex Routines

//int pthread_mutexattr_destroy(pthread_mutexattr_t *attr)
//int pthread_mutexattr_init(pthread_mutexattr_t *attr)
void pthread_mutex_destroy_c(pthread_mutex_t *mutex)
{
    int rc;
    rc = pthread_mutex_destroy(mutex);
    if (rc != 0) {
        print_error_and_exit("pthread_mutex_destroy", rc);
    }
}

void pthread_mutex_init_c(pthread_mutex_t *mutex, const pthread_mutexattr_t *attr)
{
    int rc;
    rc = pthread_mutex_init(mutex, attr);
    if (rc != 0) {
        print_error_and_exit("pthread_mutex_init", rc);
    }
}

void pthread_mutex_lock_c(pthread_mutex_t *mutex)
{
    int rc;
    rc = pthread_mutex_lock(mutex);
    if (rc != 0) {
        print_error_and_exit("pthread_mutex_lock", rc);
    }
}

//void pthread_mutex_trylock(pthread_mutex_t *mutex)
void pthread_mutex_unlock_c(pthread_mutex_t *mutex)
{
    int rc;
    rc = pthread_mutex_unlock(mutex);
    if (rc != 0) {
        print_error_and_exit("pthread_mutex_unlock", rc);
    }
}

// Condition Variable Routines
//void pthread_condattr_init(pthread_condattr_t *attr)
//int pthread_condattr_destroy(pthread_condattr_t *attr)


void pthread_cond_init_c(pthread_cond_t *cond, const pthread_condattr_t *attr)
{
    int rc;
    rc = pthread_cond_init(cond, attr);
    if (rc != 0) {
        print_error_and_exit("pthread_cond_init", rc);
    }
}
void pthread_cond_destroy_c(pthread_cond_t *cond)
{
    int rc;
    rc = pthread_cond_destroy(cond);
    if (rc != 0) {
        print_error_and_exit("pthread_cond_destroy", rc);
    }
}


void pthread_cond_signal_c(pthread_cond_t *cond)
{
    int rc;
    rc = pthread_cond_signal(cond);
    if (rc != 0) {
        print_error_and_exit("pthread_cond_signal", rc);
    }
}

void pthread_cond_broadcast_c(pthread_cond_t *cond)
{
    int rc;
    rc = pthread_cond_broadcast(cond);
    if (rc != 0) {
        print_error_and_exit("pthread_cond_broadcast", rc);
    }
}

void pthread_cond_wait_c(pthread_cond_t *restrict cond, 
                         pthread_mutex_t *restrict mutex);
{
    int rc;
    rc = pthread_cond_wait(cond, mutex);
    if (rc != 0) {
        print_error_and_exit("pthread_cond_wait", rc);
    }
}

void pthread_cond_timedwait_c(pthread_cond_t *cond, pthread_mutex_t *mutex,
                              const struct timespec *abstime)
{
    int rc;
    rc = pthread_mutex_timedwait(cond, mutex, abstime);
    if (rc != 0) {
        print_error_and_exit("pthread_mutex_timedwait", rc);
    }
}


