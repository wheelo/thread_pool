/* APU p. 401 Figure 11.10
 * Illustrates a mutex used to protect a data structure
 *      - specifically demonstrates reference counting for a 
 *        dynamically-allocated object [malloc'd] with a mutex so that it is
 *        not freed while threads still using it
 *
 *
 * Before using data_structure, threads are expected to add a reference to it
 * by calling ds_hold() [which calls mutex lock, increments ds_count, then unlocks].
 * 
 * Threads are expected to call ds_release() when done to release the reference
 */


/* Note: this example is overly simplified - SEE mutex-2.c and mutex-3.c

In this example, we have ignored how threads find an object before calling 
foo_hold. Even though the reference count is zero, it would be a mistake for 
foo_rele to free the object’s memory if another thread is blocked on the mutex 
in a call to foo_hold. We can avoid this problem by ensuring that the object 
can’t be found before freeing its memory. We’ll see how to do this in the 
examples that follow. */

#include <stdlib.h>
#include <pthread.h>


/* The critical resource */
typedef struct data_structure_ {
    int ds_count; // # of references (i.e., # of threads using this object)
    pthread_mutex_t ds_lock; // the mutex
    int ds_id;   // id for the data structure
    /* more stuff here */
} data_structure;

/* allocate the object */
data_structure *ds_allocate(int id)
{
    data_structure *ds;
    ds = malloc(sizeof(data_structure));
    if (ds == NULL) {
        printf("malloc error");        
        return 1;
    }
    ds->ds_count = 1;
    ds->ds_id = id;

    /* initialize mutex */
    if (pthread_mutex_init(&ds->ds_lock, NULL) != 0) {
        /* error */
        free(ds);
        return(NULL);
    }
    return ds;
}

/* Threads should call this before using data structure
 * 
 * Adds a reference to the data_structure (i.e., increase count by 1. see ds_release() - 
 * memory for ds won't be freed unless ds_count decremented to 0) 
 */
void ds_hold(data_structure *ds)
{
    pthread_mutex_lock(&ds->ds_lock); // shouldn't the ret value be checked??
    ds->ds_count++;
    pthread_mutex_unlock(&ds->ds_lock);
}

/* Threads should call this when done using data structure
 *
 * Release a reference to the object 
 */
void ds_release(data_structure *ds)
{
    pthread_mutex_lock(&ds->ds_lock); // return value should be checked to make sure successful
    if (--ds->ds_count == 0) { /* last reference. if 0, all threads have released it */
        pthread_mutex_unlock(&ds->ds_lock);
        // destroy lock & free ds
        pthread_mutex_destroy(&ds->ds_lock); // must call if dyn allocated
        free(ds);
    } else {
        // just unlock
        pthread_mutex_unlock(&ds->ds_lock);
    }
}


/* Notes:
 * In example, we lock the mutex before critical sections: 
 *     incrementing the reference count (ds_count)
 *     decrementing the reference count & checking if counter is 0
 *  
 * In ds_allocate, don't need to lock when setting ds_count = 1, since the
 * thread calling ds_allocate() is the only reference to it so far. 
 */