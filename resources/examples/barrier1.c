/*
 *  barrier1.c
 *  Source: http://www.qnx.com/developers/docs/660/index.jsp?topic=%2Fcom.qnx.doc.neutrino.sys_arch%2Ftopic%2Fkernel_Barriers.html
 *  
 */

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <time.h>
#include <pthread.h>

pthread_barrier_t barrier; // barrier synchronization object

pthread_t thread1_tid;
pthread_t thread2_tid;

void * thread1(void *not_used)
{
    time_t  now;

    time(&now); // store current time in now
    printf("thread1 starting at %s", ctime(&now)); // ctime() returns formatted time string

    // do the computation
    // let's just do a sleep here...
    sleep(5);
    pthread_barrier_wait(&barrier);
    // after this point, all three threads have completed.
    time(&now);
    printf("barrier in thread1() done at %s", ctime(&now));
}

void * thread2(void *not_used)
{
    time_t  now;

    time(&now); 
    printf("thread2 starting at %s", ctime(&now));

    // do the computation
    // let's just do a sleep here...
    sleep(10);
    pthread_barrier_wait(&barrier);
    // after this point, all three threads have completed.
    time(&now);
    printf("barrier in thread2() done at %s", ctime(&now));
}

int main(void)
{
    time_t  now;

    // create a barrier object with a count of 3
    pthread_barrier_init(&barrier, NULL, 3);

    // start up two threads, thread1 and thread2
    pthread_create(&thread1_tid, NULL, thread1, NULL);
    pthread_create(&thread2_tid, NULL, thread2, NULL);

    // at this point, thread1 and thread2 are running

    // now wait for completion
    time(&now);
    printf("main() waiting for barrier at %s", ctime(&now));
    pthread_barrier_wait(&barrier);

    // after this point, all three threads have completed.
    time(&now);
    printf("barrier in main() done at    %s", ctime(&now));
    pthread_exit(NULL);
    return(EXIT_SUCCESS);
}


/* 
OUTPUT:

main() waiting for barrier at Sun Oct 12 23:40:07 2014
thread1 starting at Sun Oct 12 23:40:07 2014
thread2 starting at Sun Oct 12 23:40:07 2014
barrier in thread2() done at Sun Oct 12 23:40:17 2014
barrier in main() done at    Sun Oct 12 23:40:17 2014
barrier in thread1() done at Sun Oct 12 23:40:17 2014
*/