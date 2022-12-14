#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <omp.h>
#include <string.h>
#include <stdint.h>
#include <stdatomic.h>

#include "definitions.h"

/******************************************************************************/
/* The lock                                                                   */
/******************************************************************************/
union ticket_lock {
    volatile unsigned int data;
    struct {
        volatile unsigned short next_ticket;
        volatile unsigned short now_serving;
    } s;
};

void ticket_init (union ticket_lock *lock) {
    lock->s.now_serving = 0;
    lock->s.next_ticket = 0;
}

void ticket_destroy (union ticket_lock *lock) {
    lock->s.now_serving = 0;
    lock->s.next_ticket = 0;
}

int ticket_acquire (union ticket_lock *lock) {
    unsigned int my_ticket;

    my_ticket = __sync_fetch_and_add(&(lock->s.next_ticket), 1);
    while (lock->s.now_serving != my_ticket);

    return 0;
}

void ticket_release (union ticket_lock *lock) {
    lock->s.now_serving += 1;
}

union ticket_lock lock; 
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
    ticket_acquire(&lock);


    n->next = Head;
    Head = n;

    ticket_release(&lock);

    /**************************************************************************/
}

inline static Object pop(int pid) {
    Object result;

    /**************************************************************************/
    /* Critical section                                                       */
    /**************************************************************************/
    ticket_acquire(&lock);

    if (Head->next == null) 
        result = -1;
    else {
        result = Head->next->value;
        Head = Head->next;
    }
    ticket_release(&lock);
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

    ticket_init(&lock);

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

