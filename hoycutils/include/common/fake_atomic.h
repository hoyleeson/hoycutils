#ifndef _COMMON_FAKE_ATOMIC_H_
#define _COMMON_FAKE_ATOMIC_H_

#include <pthread.h>

typedef struct _fake_atomic {
    int count;
    pthread_mutex_t lock;
} fake_atomic_t;


static inline void fake_atomic_init(fake_atomic_t *v, int val)
{
    pthread_mutex_init(&v->lock, NULL);
    v->count = val;
}

static inline void fake_atomic_inc(fake_atomic_t *v)
{
    pthread_mutex_lock(&v->lock);
    v->count++;
    pthread_mutex_unlock(&v->lock);
}

static inline void fake_atomic_dec(fake_atomic_t *v)
{
    pthread_mutex_lock(&v->lock);
    v->count--;
    pthread_mutex_unlock(&v->lock);
}


static inline void fake_atomic_add(int i, fake_atomic_t *v)
{
    pthread_mutex_lock(&v->lock);
    v->count += i;
    pthread_mutex_unlock(&v->lock);
}


static inline void fake_atomic_sub(int i, fake_atomic_t *v)
{
    pthread_mutex_lock(&v->lock);
    v->count -= i;
    pthread_mutex_unlock(&v->lock);
}


static inline int fake_atomic_inc_and_test(fake_atomic_t *v)
{
    int val;

    pthread_mutex_lock(&v->lock);
    val = (++(v->count) == 0);
    pthread_mutex_unlock(&v->lock);

    return val;
}

static inline int fake_atomic_dec_and_test(fake_atomic_t *v)
{
    int val;

    pthread_mutex_lock(&v->lock);
    val = (--(v->count) == 0);
    pthread_mutex_unlock(&v->lock);

    return val;
}

static inline int fake_atomic_inc_return(fake_atomic_t *v)
{
    int val;

    pthread_mutex_lock(&v->lock);
    val = ++(v->count);
    pthread_mutex_unlock(&v->lock);

    return val;

}

static inline int fake_atomic_dec_return(fake_atomic_t *v)
{
    int val;

    pthread_mutex_lock(&v->lock);
    val = --(v->count);
    pthread_mutex_unlock(&v->lock);

    return val;
}


static inline int fake_atomic_add_return(int i, fake_atomic_t *v)
{
    int val;

    pthread_mutex_lock(&v->lock);
    val = (v->count += i);
    pthread_mutex_unlock(&v->lock);

    return val;
}

static inline int fake_atomic_sub_return(int i, fake_atomic_t *v)
{
    int val;

    pthread_mutex_lock(&v->lock);
    val = (v->count -= i);
    pthread_mutex_unlock(&v->lock);

    return val;
}

#endif
