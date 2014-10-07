Threadpool
==========

## How to run code
1. Type to your command line:
   ```
   prompt> cd src
   prompt/src> make clean; make # build all code
   prompt/src> ./mergesort -b number 
   prompt/src> ./quicksort -b number
   prompt/src> ./fib_test ???
   prompt/src> ./psum_test ???
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

## How to inspect our commit logs
I used Git for version control and Github.com to host our PRIVATE repository.
To view our 100+ commit messages please follow the following instructions:

1. Go to www.github.com
2. Login with the following credientials that have been created for you:
   USERNAME = TA3214
   PASSWORD = ilovecomputersystems
3. After you login you will see under your repositories "quinnliu/threadPool". 
   Click on that blue link.
4. Near the top you will see "100+ commits". If you click on that you will see 
   our commit log.
