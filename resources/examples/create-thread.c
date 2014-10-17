// APUE 387 11.2
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

// Global variable that will store tid of create thread when pthread_create
// returns successfully
pthread_t ntid;

// Called in both the 'main' thread and created threads
void printids(const char *s)
{
    pid_t pid;
    pthread_t tid;

    pid = getpid();

    /* Gets the tid of the calling thread. Why can't just check ntid, which is
       set after being passed into pthread_create()? Because it is not safe
       for the new thread to use it. The new thread could begin running before
       main thread returns from calling pthread_create(). */
    tid = pthread_self();
    printf("%s pid %lu tid %lu (0x%lx)\n", s, (unsigned long)pid,
        (unsigned long)tid, (unsigned long)tid);    
}

void *thread_func(void *arg)
{
    printids("new thread: ");
    return((void *)0);
}

int main(void)
{
    int err;

    err = pthread_create(&ntid, NULL, thread_func, NULL); // 2nd arg - default attributes, 4th - no args
    if (err != 0) {
        //perror("main thread:");
        // threads don't set global errno, see 386
        return 1;        
    }
    printids("main thread:");
    
    /* Sleep to prevent the main thread from exiting before the new thread gets
       a chance to run (if the main thread exits, the process terminates) */
    sleep(1);
    exit(0);
}