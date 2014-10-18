#include <time.h>
#include <stdlib.h>
#include <stdio.h>
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

void print_error(char* err_msg) 
{
    write(ERR, err_msg, strlen(err_msg));
}