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
 *   - All wrapped functions have the same parameters as the original and
 *     same name with _c appended to the end (for 'checked')
 *   - Not all pthread or semaphore functions included
 ***************************************************************************/


void print_error_and_exit(char *fn_name, int err_code)
{
    fprintf(stderr, "%s(): returned error code: %s\n", fn_name, strerror(err_code));
    exit(EXIT_FAILURE);
}

/* malloc */
 void * malloc_c(int size)
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

void pthread_join_c(pthread_t thread, void **value_ptr)
{
    int rc;
    rc = pthread_join(thread, value_ptr);
    if (rc != 0) {
        print_error_and_exit("pthread_join", rc);
    }
}

//int pthread_cancel(pthread_t thread)

pthread_t pthread_self_c(void)
{
    // does not return any errors, included for convenience
    return pthread_self(void);
}

// Mutex Routines
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


/* Semaphores (semaphore.h) */
void sem_init_c(sem_t *sem, int pshared, unsigned int value)
{
    int rc;
    rc = sem_init(sem, pshared, value);
    if (rc < 0) {
        print_error_and_exit("sem_init", rc);
    }
}

void sem_destroy_c(sem_t *sem)
{
    int rc;
    rc = sem_destroy(sem);
    if (rc < 0) {
        print_error_and_exit("sem_destroy", rc);
    }
}

void sem_post_c(sem_t *sem)
{
    int rc;
    rc = sem_post(sem);
    if (rc < 0) {
        print_error_and_exit("sem_post", rc);
    }
}


void sem_wait_c(sem_t *sem)
{
    int rc;
    rc = sem_wait(sem);
    if (rc < 0) {
        print_error_and_exit("sem_wait", rc);
    }
}


// int    sem_timedwait_c(sem_t *sem, const time_t abs_timeout);
// int    sem_trywait_c(sem_t *sem);