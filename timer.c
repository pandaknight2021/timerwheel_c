#include <stdio.h>
#include <malloc.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <stdatomic.h>

#ifndef __STDC_NO_THREADS__
#include <threads.h>
#endif

#include "timer.h"


static uint64 Now(void)
{
    struct timespec ts;                 
    clock_gettime(CLOCK_MONOTONIC, &ts);  
    return (ts.tv_sec * 1000 + ts.tv_nsec / 1e6);  //  milliseconds
}


static inline void INIT_LIST_HEAD(struct list_head *list) {
    list->next = list;
    list->prev = list;
}

static inline void list_add_tail(struct list_head *node, struct list_head *head)
{
    node->prev = head->prev;
    node->next = head;
    head->prev = node;
    node->prev->next = node;
}

static inline void list_replace(struct list_head *old, struct list_head *node) {
    node->next = old->next;
    node->next->prev = node;
    node->prev = old->prev;
    node->prev->next = node;
}

static inline void list_replace_init(struct list_head *old, struct list_head *node) {
    list_replace(old, node);
    INIT_LIST_HEAD(old);
}

static inline int list_empty(const struct list_head *head) {
    return head->next == head;
}


#define list_entry(ptr, type, member) \
((type *)((char *)(ptr)-(unsigned long)(&((type *)0)->member)))


#define list_for_each(pos, head) \
	for (pos = (head)->next; pos != (head); pos = pos->next)

#define container_of(ptr, type, member) ({                              \
        void *__mptr = (void *)(ptr);                                   \
        ((type *)(__mptr - ((size_t)&((type *)0)->member))); })

/**
 * list_for_each_safe - iterate over elements in a list, but don't dereference
 *                      pos after the body is done (in case it is freed)
 * @pos:	the &struct list_head to use as a loop counter.
 * @pnext:	the &struct list_head to use as a pointer to the next item.
 * @head:	the head for your list (not included in iteration).
 */
#define list_for_each_safe(pos, pnext, head) \
	for (pos = (head)->next, pnext = pos->next; pos != (head); \
	     pos = pnext, pnext = pos->next)


#define list_for_each_entry_safe(pos, n, head, member)\
for (pos = list_entry((head)->next, typeof(*pos), member),\
n = list_entry(pos->member.next, typeof(*pos), member);\
&pos->member != (head);\
pos = n, n = list_entry(n->member.next, typeof(*n), member))


static inline void list_del(struct list_head *node) {
    struct list_head *prev = node->prev;
    struct list_head *next = node->next;
    next->prev = prev;
    prev->next = next;
}



TimerManager* CreateTimerManager() 
{
    TimerManager *tm = (TimerManager*)malloc(sizeof(TimerManager));
    if(tm)
    {
        memset(tm, 0, sizeof(TimerManager));
    
        for (int j = 0; j < TVR_SIZE; j++) {
            INIT_LIST_HEAD(tm->_tvr + j);
        }
        // 初始化其他轮刻度为64
        for (int j = 0; j < TVN_SIZE; j++) {
            INIT_LIST_HEAD(tm->_tvn[0] + j);
            INIT_LIST_HEAD(tm->_tvn[1] + j);
            INIT_LIST_HEAD(tm->_tvn[2] + j);
            INIT_LIST_HEAD(tm->_tvn[3] + j);
        }
        tm->_tick = Now();

        tm->_alive_set = hashset_create();
        mpscq_create(&tm->_add_queue);
        mpscq_create(&tm->_free_queue);
    }
    return tm;
}


void DestroyTimerManager(TimerManager* tm)
{
    if(tm) {
        hashset_destroy(tm->_alive_set);
        mpscq_destroy(&tm->_add_queue);
        mpscq_destroy(&tm->_free_queue);
    }
}



static void AddTimer(TimerManager *tm, Timer *timer) 
{
    uint64 expires = timer->expires;
    uint32 dueTime = expires - tm->_tick;
    
    struct list_head *lst;
    int i = 0;
 
    if (dueTime < TVR_SIZE) {
        i = expires & TVR_MASK;
        lst = tm->_tvr + i;
    } else if (dueTime < (1 << (TVR_BITS + TVN_BITS))) {
        i = (expires >> TVR_BITS) & TVN_MASK;
        lst = tm->_tvn[0] + i;
    } else if (dueTime < (1 << (TVR_BITS + 2 * TVN_BITS))) {
        i = (expires >> (TVR_BITS + TVN_BITS)) & TVN_MASK;
        lst = tm->_tvn[1] + i;
    } else if (dueTime < (1 << (TVR_BITS + 3 * TVN_BITS))) {
        i = (expires >> (TVR_BITS + 2 * TVN_BITS)) & TVN_MASK;
        lst = tm->_tvn[2] + i;
    } else if ((long)dueTime < 0) {
        i = tm->_tick & TVR_MASK;
        lst = tm->_tvr + i;
    } else {
        /* If the timeout is larger than 0xffffffff on 64-bit
         * architectures then we use the maximum timeout:
         */
        if (dueTime > 0xffffffffUL)
        {
            dueTime = 0xffffffffUL;
            expires = dueTime + tm->_tick;
        }
        i = (expires >> (TVR_BITS + 3 * TVN_BITS)) & TVN_MASK;
        lst = tm->_tvn[3] + i;
    }
    
    list_add_tail(&timer->entry, lst);
}


Timer* SetTimer(TimerManager *tm, uint32 msDelay,uint32 msPeriod, TimerCallback cb, void* arg)
{
    if((!cb) || (!tm))  return NULL;

    Timer *timer = (Timer*)malloc(sizeof(Timer));
    if(timer == NULL) return NULL;

    INIT_LIST_HEAD(&timer->entry);
    timer->entry.next = NULL;

    uint t0 = Now();
    timer->expires = (msDelay > 0 ? t0 + msDelay : t0 + msPeriod);
    timer->period = msPeriod;
    timer->timer_cb = cb;
    timer->param = arg;
    
    mpscq_push(&tm->_add_queue,(void*)timer);
    return timer;
}


#define INDEX(v,n) (((v) >> (TVR_BITS + (n) * TVN_BITS)) & TVN_MASK)
static int cascade(TimerManager *tm, struct list_head *lst, int index) {
    Timer *timer, *tmp;
    struct list_head tv_list;
    list_replace_init(lst + index, &tv_list);
    list_for_each_entry_safe(timer, tmp, &tv_list, entry) {
        AddTimer(tm, timer);
    }
    return index;
}


static void AddTimers(TimerManager *tm){
    if(mpscq_empty(&tm->_add_queue)) return;
    
    Timer* p = mpscq_pop(&tm->_add_queue);
    while(p){
        AddTimer(tm, p);
        hashset_add(tm->_alive_set, p);
        p = mpscq_pop(&tm->_add_queue);
    }
}

static void RemoveTimers(TimerManager *tm){
    if(mpscq_empty(&tm->_free_queue)) return;

    Timer* p = mpscq_pop(&tm->_free_queue);
    while(p){
        if(hashset_is_member(tm->_alive_set,p)) p->timer_cb = 0;
        p = mpscq_pop(&tm->_free_queue);
    }
}


void OnTick(TimerManager *tm) {
    uint64 now = Now();

    AddTimers(tm);
    RemoveTimers(tm);

    while (tm->_tick <= now)    
    {
        struct list_head work_list;
        struct list_head *head = &work_list;
        
        int index = tm->_tick & TVR_MASK;
        if (!index 
            && (!cascade(tm, &tm->_tvn[0][0], INDEX(tm->_tick, 0))) 
            && (!cascade(tm, &tm->_tvn[1][0], INDEX(tm->_tick, 1)))
            && !cascade(tm, &tm->_tvn[2][0], INDEX(tm->_tick, 2)))
        {
            cascade(tm, &tm->_tvn[3][0], INDEX(tm->_tick, 3));
        }
        ++tm->_tick;
            
        list_replace_init(tm->_tvr + index, &work_list);
        while (!list_empty(head)) {
            Timer *timer = list_entry(head->next, Timer, entry);
            list_del(&timer->entry);  

            if(timer->timer_cb){
                timer->timer_cb(timer->param);

                if (timer->period) {
                    timer->expires = now + timer->period;
                    AddTimer(tm, timer);
                }else{
                    timer->timer_cb = 0;
                }
            } 

            if(!timer->timer_cb){
                hashset_remove(tm->_alive_set,timer);
                free(timer);
            }
        }
    }
}

void KillTimer(TimerManager *tm, Timer* timer)
{
    if(!tm || !timer) return;

    mpscq_push(&tm->_free_queue,(void*)timer);
}


