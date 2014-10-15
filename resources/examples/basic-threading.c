/**
 * A pthread program illustrating how to
 * create a simple thread and some of the pthread API
 * 
 * This program implements the summation function where
 * the summation operation is run as a separate thread.
 *
 * Most Unix/Linux/OS X users
 * gcc thrd.c -lpthread
 *
 * Solaris users must enter
 * gcc thrd.c -lpthreads
 *
 * Figure 4.9
 *
 * @author Gagne, Galvin, Silberschatz
 * Operating System Concepts  - Ninth Edition
 * Copyright John Wiley & Sons - 2013
 */


#include <pthread.h>
#include <stdio.h>
#include <stdlib.h> // atoi

/* Global data - shared by threads */
int sum;

/* Threads call this function to begin execution */
void *runner(void *param);



int main(int argc, char *argv[])
{
    pthread_t tid;  /* thread id */
    pthread_attr_t attr; /* set of thread attributes */
    

    if (argc != 2) {
        fprintf(stderr, "usage: a.out <integer value>\n");
        return -1;
    }

    if (atoi(argv[1]) < 0) {
        fprintf(stderr, "%d must be >= 0\n", atoi(argv[1]));
        return -1;
    }

    /* Get the default attributes */
    pthread_attr_init(&attr);

    /* Create the thread */
    pthread_create(&tid, &attr, runner, argv[1]); // 3rd arg is f-ptr to routine to execute


    /* Wait for the thread to exit */
    pthread_join(tid, NULL);

    printf("sum = %d\n", sum);

    return 0;
}

/* The thread will begin control in this function. In Pthreads, separate 
   threads begin execution in a specified function */
void *runner(void *param)
{
    int i;
    int upper = atoi(param); 

    sum = 0; // sum is a global variable

    for (i = 0; i <= upper; i++) {
        sum += i;
    }

    pthread_exit(0);
}
