Threadpool
==========

## How to run code
1. Type to your command line:
   ```
   prompt> cd src
   prompt/src> make clean; make # build all code
   prompt/src> ~cs3214/bin/fjdriver.py 
   ```

## What each file/folder is for:
- src
  + Makefile = script for how to compile all files in src folder
  + fib_test.c = calls all methods in threadpool.c and tests parrellel
                 fibonacci function
  + list.c & list.h = generic doubly linked list
  + mergesort.c = calls all methods in threadpool.c and tests parrellel 
                  merge sort vs. serial(normal single threaded) merge sort
  + psum_test.c = calls all methods in threadpool.c and tests parrellel
                  sum function
  + quicksort.c = calls all methods in threadpool.c and tests parrellel
                  quick sort vs. serial(normal single threaded) quick sort

  + threadpool.c & threadpool.h = 2 structs and 5 functions to implement
  
  + threadpool_lib.c & threadpool_lib.h = helper functions for determing
                  time it takes to run code & count current # of threads

- .gitignore = files to not add to remote repository
- README.txt = the file you are reading right now
