##
# Temporary Makefile
#
# Treat warnings as errors. This seems to be the only way to 
# convince some students of the importance of ensuring that
# their code compiles without warnings before starting to debug.
#
# Do not change this line.  We will not use your copy of the Makefile 
# we will use *this* Makefile to run check.py when grading.
#
# for benchmarking
#`CFLAGS=-Wall -O3 -Werror -Wmissing-prototypes -ggdb3
#for debugging
CFLAGS=-Wall -O0 -g -Werror -Wmissing-prototypes
LDLIBS=-lpthread -lrt

OBJ=threadpool.o list.o threadpool_lib.o

all: quicksort psum_test fib_test mergesort

# not used
threadpool_test: threadpool_test.o $(OBJ)

quicksort: quicksort.o $(OBJ)

mergesort: mergesort.o $(OBJ)

psum_test: psum_test.o $(OBJ)

fib_test: fib_test.o $(OBJ)

clean:
	rm -f *.o threadpool_test quicksort psum_test mergesort fib_test
