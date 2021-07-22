/**********************************************************************
基于时间轮的定时器: 
1. 接口简单
2. 线程安全, 
3. 高性能, 采用无锁队列
4. 易于移植适配.

**********************************************************************/
#ifndef __TIEMRWHEEL_H__
#define __TIEMRWHEEL_H__

#include <stdlib.h>
#include "mpsc.h"
#include "hashset.h"


#define TVN_BITS 6
#define TVR_BITS 8
#define TVN_SIZE (1 << TVN_BITS)
#define TVR_SIZE (1 << TVR_BITS)
#define TVN_MASK (TVN_SIZE - 1)
#define TVR_MASK (TVR_SIZE - 1)



typedef int int32;
typedef unsigned int uint32;
typedef long int64;
typedef unsigned long uint64;



typedef void (*TimerCallback)(void*);



struct list_head {
    struct list_head *next, *prev;
};

typedef struct TimerNode{
    struct list_head entry;
    uint64 expires;     
    uint32 period;
    TimerCallback timer_cb;
    void* param;
}Timer;


typedef struct TimerManager_{
    struct list_head _tvr[TVR_SIZE];    //  1 级时间轮。在这里表示存储未来的 0 ~ 255 毫秒的计时器。tick 的粒度为 1 毫秒
    
    // 2 级时间轮。存储未来的 256 ~ 256*64-1 毫秒的计时器。tick 的粒度为 256 毫秒
    // 3 级时间轮。存储未来的 256*64 ~ 256*64*64-1 毫秒的计时器。tick 的粒度为 256*64 毫秒
    // 4 级时间轮。存储未来的 256*64*64 ~ 256*64*64*64-1 毫秒的计时器。tick 的粒度为 256*64*64 毫秒
    // 5 级时间轮。存储未来的 256*64*64*64 ~ 256*64*64*64*64-1 毫秒的计时器。tick 的粒度为 256*64*64 毫秒
    struct list_head _tvn[4][TVN_SIZE]; 
    
   
    hashset_t _alive_set;
    mpscq _add_queue;               // add timer  queue
    mpscq _free_queue;              // kill timer queue
    uint64 _tick;       //当前时刻

}TimerManager;


TimerManager* CreateTimerManager();
void DestroyTimerManager(TimerManager* tm);

//时间轮驱动, 周期调用即可
void OnTick(TimerManager *tm);

Timer* SetTimer(TimerManager *tm, uint32 msDelay,uint32 msPeriod, TimerCallback cb, void* arg);
void KillTimer(TimerManager *tm, Timer* t);


#endif