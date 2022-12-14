#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <omp.h>
#include <string.h>
#include <stdint.h>

#include "definitions.h"

/******************************************************************************/
/* The lock                                                                   */
/******************************************************************************/
#include <stdatomic.h>
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <sched.h>


typedef struct clh_mutex_node_ clh_mutex_node_t;

struct clh_mutex_node_
{
    _Atomic char succ_must_wait;
};

typedef struct
{
    clh_mutex_node_t * mynode;
    char padding[64];  // To avoid false sharing with the tail
    _Atomic (clh_mutex_node_t *) tail;
} clh_mutex_t;


void clh_mutex_init(clh_mutex_t * self);
void clh_mutex_destroy(clh_mutex_t * self);
void clh_mutex_lock(clh_mutex_t * self);
void clh_mutex_unlock(clh_mutex_t * self);

static clh_mutex_node_t * clh_mutex_create_node(char islocked)
{
    clh_mutex_node_t * new_node = (clh_mutex_node_t *)malloc(sizeof(clh_mutex_node_t));
    atomic_store_explicit(&new_node->succ_must_wait, islocked, memory_order_relaxed);
    return new_node;
}


/*
 * Initializes the mutex, creating a sentinel node.
 *
 * Progress Condition: Wait-Free Population Oblivious
 */
void clh_mutex_init(clh_mutex_t * self)
{
    // We create the first sentinel node unlocked, with islocked=0
    clh_mutex_node_t * node = clh_mutex_create_node(0);
    self->mynode = node;
    atomic_store(&self->tail, node);
}


/*
 * Destroy the mutex, clearing all memory.
 * You must be sure that there are no other threads currently holding
 * the lock or attempting to.
 *
 * Progress Condition: Wait-Free Population Oblivious
 */
void clh_mutex_destroy(clh_mutex_t * self)
{
    free(atomic_load(&self->tail));
}


/*
 * Locks the mutex for the current thread. Will wait for other threads
 * that did the atomic_exchange() before this one.
 *
 * Progress Condition: Blocking
 */
void clh_mutex_lock(clh_mutex_t * self)
{
    // Create the new node locked by default, setting islocked=1
    clh_mutex_node_t *mynode = clh_mutex_create_node(1);
    clh_mutex_node_t *prev = atomic_exchange(&self->tail, mynode);

    // This thread's node is now in the queue, so wait until it is its turn
    char prev_islocked = atomic_load_explicit(&prev->succ_must_wait, memory_order_relaxed);
    if (prev_islocked) {
        while (prev_islocked) {
            sched_yield();  // Replace this with thrd_yield() if you use <threads.h>
            prev_islocked = atomic_load(&prev->succ_must_wait);
        }
    }
    // This thread has acquired the lock on the mutex and it is now safe to
    // cleanup the memory of the previous node.
    free(prev);

    // Store mynode for clh_mutex_unlock() to use. We could replace
    // this with a thread-local, not sure which is faster.
    self->mynode = mynode;
}


/*
 * Unlocks the mutex. Assumes that the current thread holds the lock on the
 * mutex.
 *
 * Progress Condition: Wait-Free Population Oblivious
 */
void clh_mutex_unlock(clh_mutex_t * self)
{
    // We assume that if this function was called, it is because this thread is
    // currently holding the lock, which means that self->mynode is pointing to
    // the current thread's mynode.
    if (self->mynode == NULL) {
        // ERROR: This will occur if unlock() is called without a lock()
        return;
    }
    atomic_store(&self->mynode->succ_must_wait, 0);
}

clh_mutex_t lock;

/******************************************************************************/

typedef struct ListNode {
    Object value; 
    volatile struct ListNode *next;
} ListNode;	

int counter = 0;
ListNode guard CACHE_ALIGN = {0, null};
volatile ListNode *Head CACHE_ALIGN = &guard;
int64_t d1 CACHE_ALIGN, d2;

inline static void push(Object arg, int pid) {
    volatile ListNode *n = getAlignedMemory(CACHE_LINE_SIZE, sizeof(ListNode));
    n->value = (Object)arg;

    /**************************************************************************/
    /* Critical section                                                       */
    /**************************************************************************/
    clh_mutex_lock(&lock);

    n->next = Head;
    Head = n;

    clh_mutex_unlock(&lock);
    /**************************************************************************/
}

inline static Object pop(int pid) {
    Object result;

    /**************************************************************************/
    /* Critical section                                                       */
    /**************************************************************************/
    clh_mutex_lock(&lock);

    if (Head->next == null) 
        result = -1;
    else {
        result = Head->next->value;
        Head = Head->next;
    }

    clh_mutex_unlock(&lock);
    /**************************************************************************/

    return result;
}

pthread_barrier_t barr;


inline static void Execute(void* Arg) {
    long i;
    long rnum;
    long id = (long) Arg;
    volatile int j;

    setThreadId(id);
    _thread_pin(id);
    simSRandom(id + 1);
    
    if (id == N_THREADS - 1)
        d1 = getTimeMillis();
    
    int rc = pthread_barrier_wait(&barr);
    
    if (rc != 0 && rc != PTHREAD_BARRIER_SERIAL_THREAD) {
        printf("Could not wait on barrier\n");
        exit(-1);
    }
    
    for (i = 0; i < RUNS; i++) {
        push((Object)1, id);
        rnum = simRandomRange(1, MAX_WORK);
        for (j = 0; j < rnum; j++)
            ;
        pop(id);
        rnum = simRandomRange(1, MAX_WORK);
        for (j = 0; j < rnum; j++)
            ;
    }
}

inline static void* EntryPoint(void* Arg) {
    Execute(Arg);
    return null;
}
    

inline static pthread_t StartThread(int arg) {
    long id = (long) arg;
    void *Arg = (void*) id;
    pthread_t thread_p;

    pthread_attr_t my_attr;
    pthread_attr_init(&my_attr);
    pthread_create(&thread_p, &my_attr, EntryPoint, Arg);

    return thread_p;
}

int main(void) {
    pthread_t threads[N_THREADS];
    int i;

    clh_mutex_init(&lock);
    if (pthread_barrier_init(&barr, NULL, N_THREADS)) {
        printf("Could not create the barrier\n");
        return -1;
    }

    for (i = 0; i < N_THREADS; i++)
        threads[i] = StartThread(i);

    for (i = 0; i < N_THREADS; i++)
        pthread_join(threads[i], NULL);
    d2 = getTimeMillis();

    printf("time: %d ns\n", (int) (d2 - d1));
    
    if (pthread_barrier_destroy(&barr)) {
        printf("Could not destroy the barrier\n");
        return -1;
    }

    return 0;
}

