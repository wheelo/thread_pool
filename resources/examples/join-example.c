// APU Fig. 11.3 (389)
// Using pthread_join to fetch the exit code from a thread that has terminated
// see basic-pthread.c or semaphore.c for other examples


#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>


/* This thread exits by returning */
void *thr_fn1(void *arg)
{
    printf("thread 1 returning\n");
    return((void *)1);   // put into pthread_join() rval_ptr argument
}

/* This thread exits by calling pthread_exit */
void *thr_fn2(void *arg)
{
    printf("thread 2 returning\n");
    pthread_exit((void *) 2);   // put into pthread_join() rval_ptr argument
}

int main(void)
{
    int err;    // because threads don't have a global errno [?] - shared by process?
    pthread_t tid1, tid2;
    void *t_ret;

    /* Create threads */
    err = pthread_create(&tid1, NULL, thr_fn1, NULL);
    if (err != 0) {
        printf("error creating thread 1\n");
    }

    err = pthread_create(&tid2, NULL, thr_fn2, NULL);
    if (err != 0) {
        printf("error creating thread 2\n");
    }

    /* Join threads */
    err = pthread_join(tid1, &t_ret);
    if (err != 0) {
        printf("error joining with thread 1");
    }
    printf("thread 1 exit code: %ld\n", (long)t_ret);

    err = pthread_join(tid2, &t_ret);
    if (err != 0) {
        printf("error joining with thread 2\n");
    }
    printf("thread 2 exit code: %ld\n", (long)t_ret);

    exit(0); // terminates process
}