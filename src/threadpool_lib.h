#include <time.h>
#include <unistd.h> // write()
#include <string.h> // strlen()


struct timespec timespec_diff(struct timespec start, struct timespec end);
void timespec_print(struct timespec ts, char *buf, size_t buflen);
int count_number_of_threads(void);

/* Prints the specified error message to STDERR */
void print_error_and_exit(char* error_message); 