/* 
 * pthread Example - using a semaphore as a mutex
 *
 * Source: http://www.amparo.net/ce155/thread-ex.html
 *
 * Basic POSIX threads example.
 *
 * To compile and link: 
 * gcc -o basic-pthread basic-pthread.c -Wall -Werror -lpthread
 */

#include <unistd.h>     /* Symbolic Constants */
#include <sys/types.h>  /* Primitive System Data Types (like pthread_t) */
#include <errno.h>      /* Errors */
#include <stdio.h>      /* Input/Output */
#include <stdlib.h>     /* General Utilities */
#include <pthread.h>    /* POSIX Threads */
#include <string.h>     /* String handling */

/* Thread routine prototype */
void print_message(void *ptr);

/* Struct to hold data to be passed to a thread
   this shows how multiple data items can be passed to a thread */
typedef struct str_thdata {
    int thread_no;
    char message[100];
} thdata;

int main(void)
{
    pthread_t thread1, thread2; /* thread variables (will store TIDs) */
    thdata data1, data2;        /* structs to be passed to threads */

    /* Initialize data to pass to thread 1 */
    data1.thread_no = 1;
    strcpy(data1.message, "Hello!");

    /* Initialize data to pass to thread 2 */
    data2.thread_no = 2;
    strcpy(data2.message, "Hi!");

    /* create threads 1 and 2 */
    pthread_create(&thread1, NULL, (void *) &print_message, (void *) &data1);
    // note: '&'' before fn ptrs unnec.
    pthread_create(&thread2, NULL, (void *) print_message, (void *) &data2);     

    // note: threads have started running after pthread_create()

    /* suspend main until threads terminate  */

    /* Main block (thread) now waits for both threads to terminate before it exits
       Ensures main doesn't exit (thus killing the threads) before threads 
       complete their work */
    pthread_join(thread1, NULL);
    pthread_join(thread2, NULL);

    exit(0);  // exit vs return (https://stackoverflow.com/questions/17383015/difference-between-return-0-and-exit-0)
}

/* Used as the start routine for the threads used. Accepts a void pointer */
void print_message(void *ptr)
{
    thdata *data;
    data = (thdata *) ptr; /* type cast from void pointer to thdata pointer */

    /* do the work */
    printf("Thread %d says %s\n", data->thread_no, data->message);

    /* exit the thread */
    pthread_exit(0);
}



