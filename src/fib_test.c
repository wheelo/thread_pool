/*
 * Thread pool test program.
 * Parallel fibonacci.
 * C version of http://en.cppreference.com/w/cpp/thread/async
 *
 * Written by G. Back for CS3214 Fall 2014.
 */
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <time.h>

#include "threadpool.h"
#include "threadpool_lib.h"

/* Data to be passed to callable. */
struct problem_parameters {
    unsigned n;
};

#define GRANULARITY 100

static void *
fibonacci(struct thread_pool * pool, void * _data)
{
    struct problem_parameters * p = _data;
    if (p->n <= 1)
        return (void *) 1;

    struct problem_parameters left_half = { .n = p->n - 1 };
    struct problem_parameters right_half = { .n = p->n - 2 };
    struct future * f = thread_pool_submit(pool, fibonacci, &right_half);
    uintptr_t lresult = (uintptr_t) fibonacci(pool, &left_half);
    uintptr_t rresult = (uintptr_t) future_get(f);
    future_free(f);
    return (void *)(lresult + rresult);
}

int
main(int ac, char *av[])
{
    int nthreads = atoi(av[1]);
    struct thread_pool * pool = thread_pool_new(nthreads);
    int n = atoi(av[2]);

    struct problem_parameters roottask = { .n = n };

    unsigned long long F[n+1];
    F[0] = F[1] = 1;
    int i;
    for (i = 2; i < n + 1; i++) {
        F[i] = F[i-1] + F[i-2];
    }

    printf("starting...\n");
    struct timespec start, end;
    clock_gettime(CLOCK_REALTIME, &start);
    struct future *f = thread_pool_submit(pool, fibonacci, &roottask);
    unsigned long long Fvalue = (unsigned long long) future_get(f);
    clock_gettime(CLOCK_REALTIME, &end);

    if (Fvalue != F[n]) {
        printf("result %lld should be %lld\n", Fvalue, F[n]);
        return -1;
    } else {
        char buf[80];
        timespec_print(timespec_diff(start, end), buf, sizeof buf);
        printf("result ok. took %s\n", buf);
    }

    thread_pool_shutdown_and_destroy(pool);
    return 0;
}

