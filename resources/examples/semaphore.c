/* 
 * Semaphore Example - using a semaphore as a mutex
 *
 * Source: http://www.amparo.net/ce155/sem-ex.html
 *
 * Notes: The semaphore in this program is used as a mutex, a binary semaphore,
 * to implement mutual exclusion between 2 processes which use a shared 
 * resource.
 *
 * To compile and link: 
 * gcc -o sem-ex sem-ex.c -Wall -Werror -lpthread
 */

#include <unistd.h>     /* Symbolic Constants */
#include <sys/types.h>  /* Primitive System Data Types (like pthread_t) */
#include <errno.h>      /* Errors */
#include <stdio.h>      /* Input/Output */
#include <stdlib.h>     /* General Utilities */
#include <pthread.h>    /* POSIX Threads */
#include <string.h>     /* String handling */
#include <semaphore.h>  /* Semaphore */

/* prototype for thread routine */
void handler(void *ptr);

/* GLOBAL VARIABLES */

/* Semaphores are declared global, so that they can be accessed in main() and
   in the thread routine.
   Here, the semaphore is used as a mutex */
sem_t mutex;
int counter; // shared variable that the semaphore protects

int main(void)
{
    int i[2];
    pthread_t thread_a;
    pthread_t thread_b;

    i[0] = 0;   /* argument to thread A */
    i[1] = 1;   /* argument to thread B */

    /* initialize mutex to 1 - binary semaphore */
    sem_init(&mutex, 0, 1);     /* 2nd param = 0 --> semaphore is local */

    /* Note: you can check if thread has been successfully created by checking
       the return value of pthread_create */
    pthread_create(&thread_a, NULL, handler, (void *) &i[0]);
    pthread_create(&thread_b, NULL, handler, (void *) &i[1]);

    // note: threads have started running after pthread_create()

    /* suspend main until threads terminate (ensures main doesn't exit before 
       threads complete their work) */
    pthread_join(thread_a, NULL);
    pthread_join(thread_b, NULL);

    sem_destroy(&mutex);    /* destroy semaphore */

    exit(0);
}

/* Routine run by thread_a and thread_b */
void handler(void *ptr)
{
    int x;
    x = *( (int *) ptr); // cast void ptr to int ptr and dereference
    printf("Thread %d: Waiting to enter critical section...\n", x);
    sem_wait(&mutex);   /* down */
    /* START CRITICAL REGION */
    printf("Thread %d: Now in critical region...\n", x);
    printf("Thread %d: Counter value (initial): %d\n", x, counter);
    printf("Thread %d: Incrementing counter...\n", x);
    counter++;
    printf("Thread %d: New counter value: %d\n", x, counter);
    printf("Thread %d: Exiting critical region...\n", x);
    /* END CRITICAL REGION */
    sem_post(&mutex);   /* up */

    pthread_exit(0);
}



