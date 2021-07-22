[英文](README.md) | 🇨🇳中文

## 📖 简介

TimerManager 是一个基于C11 实现的跨平台异步定时器管理器, 采用时间轮实现.



## 接口：

```c
TimerManager* CreateTimerManager();

void DestroyTimerManager(TimerManager* tm);

//timer step 
void OnTick(TimerManager *tm);

Timer* SetTimer(TimerManager *tm, uint32 msDelay,uint32 msPeriod, TimerCallback cb, void* arg);
void KillTimer(TimerManager *tm, Timer* t);

```




## 使用


``` cpp
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "timer.h"
#include "mpsc.h"
#include <stdatomic.h>
#include <pthread.h>


TimerManager *pwheel = NULL;
int stop = 0;
int g[10000] = {0};
int gNum = 0;
Timer* tmr[10000];


void mytimer1(void* arg)
{
    int* p = (int*)arg;
    *p = 1;
    int i = p-&g[0];
    KillTimer(pwheel,tmr[i]);
    tmr[i] = 0;
    gNum++;
    if(gNum >= 10000) stop = 1;
}

void* worker1 (void* p){
    int n = (int*)p - &g[0];
    for(int i = 0; i != 100; i++){
        tmr[i + n] = SetTimer(pwheel, 0, rand() % 1000, &mytimer1, (int*)p+i);
    }
    return 0;
}


static uint64 Now(void)
{
    struct timespec ts;                 
    clock_gettime(CLOCK_MONOTONIC, &ts);  
    return (ts.tv_sec * 1000 + ts.tv_nsec / 1e6);  //  milliseconds
}
 
int main(int argc,char *argv[])
{
    pwheel = CreateTimerManager();
    if(NULL == pwheel)
        return -1;
  
    pthread_t  th;
    uint64 t0 = Now();

    for(int i = 0; i < 100; i++)
    {
        pthread_create(&th, NULL, &worker1, (void *)&g[i*100]);
        pthread_detach(th);
    }

    printf("create thread: %lu\n",Now() - t0);
    t0 = Now();

    while(1)
    {  
        usleep(1000);
        OnTick(pwheel);
        if(stop) break;
    }
    t0 = Now() - t0;
            
    int fails = 0;
  
    printf("******elapsed: %lu*****\n",t0);
    for(int i = 0; i < 10000;i++)
    {
        if(!g[i]) ++fails;
    }
    printf("fail: %d\n",fails);
    printf("**********************\n");
         
    return 0;
}


```


## 📄 证书

源码允许用户在遵循 [MIT 开源证书](/LICENSE) 规则的前提下使用。

