#include <time.h>
#include <unistd.h> // write()
#include <string.h> // strlen()


struct timespec timespec_diff(struct timespec start, struct timespec end);
void timespec_print(struct timespec ts, char *buf, size_t buflen);
int count_number_of_threads(void);

/* Print error message and exit */
void error_exit(char *msg, int err_code);
void exception_exit(char *msg);

/* Wrapper functions with error checking for pthreads and semaphores */

/* malloc */
 void * malloc_c(int size);

/* POSIX Threads (pthread.h) */
// Thread Routines
void pthread_create_c(pthread_t *thread, const pthread_attr_t *attr, 
                      void *(*start_routine)(void *), void *arg);
void pthread_join_c(pthread_t thread, void **value_ptr);
pthread_t pthread_self_c(void);
// Mutex Routines
void pthread_mutex_destroy_c(pthread_mutex_t *mutex);
void pthread_mutex_init_c(pthread_mutex_t *mutex, const pthread_mutexattr_t *attr);
void pthread_mutex_lock_c(pthread_mutex_t *mutex);
void pthread_mutex_unlock_c(pthread_mutex_t *mutex);
// Condition Variable Routines
void pthread_cond_init_c(pthread_cond_t *cond, const pthread_condattr_t *attr);
void pthread_cond_destroy_c(pthread_cond_t *cond);
void pthread_cond_signal_c(pthread_cond_t *cond);
void pthread_cond_broadcast_c(pthread_cond_t *cond);
void pthread_cond_wait_c(pthread_cond_t *cond, pthread_mutex_t *mutex);
void pthread_cond_timedwait_c(pthread_cond_t *cond, pthread_mutex_t *mutex, const struct timespec *abstime);

/* Semaphores (semaphore.h) */
void sem_init_c(sem_t *sem, int pshared, unsigned int value);
void sem_destroy_c(sem_t *sem);
void sem_post_c(sem_t *sem);
void sem_wait_c(sem_t *sem);