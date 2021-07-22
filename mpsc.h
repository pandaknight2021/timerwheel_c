#ifndef __MPSCQ_H__
#define __MPSCQ_H__



#ifdef __cplusplus
extern "C" {
#endif

typedef struct mpscq_node_t
{
    struct mpscq_node_t* volatile  next;
    void*                   pdata;
}mpscq_node;

typedef struct mpscq_t
{
    struct mpscq_node_t* volatile  head;
    struct mpscq_node_t*           tail;
}mpscq;

void mpscq_create(mpscq* self);
void mpscq_destroy(mpscq* self);
void mpscq_push(mpscq* self, void* n);
void* mpscq_pop(mpscq* self);
int mpscq_empty(mpscq* self);



#ifdef __cplusplus
}
#endif

#endif

