#include <stdlib.h>
#include <stdatomic.h>
#include "mpsc.h"

void mpscq_create(mpscq* self)
{
    mpscq_node* stub = malloc(sizeof(mpscq_node));
    self->head = stub;
    self->tail = stub;
    stub->next = 0;
}

#define XCHG(ptr,value) atomic_exchange(ptr, value)
void mpscq_push(mpscq* self, void* val)
{
    if(self && val) {
        mpscq_node* n = malloc(sizeof(mpscq_node));
        if(!n) return;
        n->next = 0;
        n->pdata = val;
        mpscq_node* prev = XCHG(&self->head, n); 
        prev->next = n;  
    }
}

void* mpscq_pop(mpscq* self)
{
    mpscq_node* tail = self->tail;
    mpscq_node* next = tail->next; 
    if (next)
    {
        self->tail = next;
        free(tail);
        return next->pdata;
    }

    return 0;
}

int mpscq_empty(mpscq* self)
{
    return self->head == self->tail;
}

void mpscq_destroy(mpscq* self)
{
    while(!mpscq_empty(self)){
        void* p = mpscq_pop(self);
    }
    free(self->tail);
}


