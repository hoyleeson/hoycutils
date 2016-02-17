#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <common/iowait.h>
#include <common/hash.h>

#define RES_SLOT_SHIFT          (6)
#define RES_SLOT_CAPACITY       (1 << RES_SLOT_SHIFT)
#define RES_SLOT_MASK           (RES_SLOT_CAPACITY - 1)


int iowait_init(iowait_t *wait)
{
    int i;

    pthread_mutex_init(&wait->lock, NULL);
    wait->slots = malloc(sizeof(struct hlist_head) * RES_SLOT_CAPACITY);

    for (i = 0; i < RES_SLOT_CAPACITY; i++)
        INIT_HLIST_HEAD(&wait->slots[i]);
    return 0;
}

static struct hlist_head *res_slot_head(iowait_t *wait, int type, int seq)
{
    unsigned long key;

    key = type << 16 | seq;
    key = hash_long(key, RES_SLOT_SHIFT);
    return &wait->slots[key];
}

int wait_for_response_data(iowait_t *wait, int type, int seq,
        void *result, int *count)
{
    int ret;
    struct res_info res;
    struct hlist_head *rsh;

    res.type = type;
    res.seq = seq;
    res.res = result;
    res.count = (count) ? *count : 0;

    init_completion(&res.done);
    rsh = res_slot_head(wait, type, seq);

    pthread_mutex_lock(&wait->lock);
    hlist_add_head(&res.hentry, rsh);
    pthread_mutex_unlock(&wait->lock);

    ret = wait_for_completion_timeout(&res.done, WAIT_RES_DEAD_LINE);

    if(count != NULL)
        *count = res.count;

    pthread_mutex_lock(&wait->lock);
    hlist_del_init(&res.hentry);
    pthread_mutex_unlock(&wait->lock);
    return ret;
}


int post_response_data(iowait_t *wait, int type, int seq, 
        void *result, int count)
{
    struct res_info *res = NULL;
    struct hlist_head *rsh;
    struct hlist_node *tmp;

    rsh = res_slot_head(wait, type, seq);

    pthread_mutex_lock(&wait->lock);
    hlist_for_each_entry(res, tmp, rsh, hentry) {
        if(res->type == type && res->seq == seq) {
            pthread_mutex_unlock(&wait->lock);
            goto found; 
        }
    }
    pthread_mutex_unlock(&wait->lock);
    return -EINVAL;

found:
    if((res->count == 0) || 
            (res->count != 0 && res->count > count))
        res->count = count;

    memcpy(res->res, result, res->count);

    complete(&res->done);
    return 0;
}


int wait_for_response(iowait_t *wait, int type, int seq, void *result)
{
    return wait_for_response_data(wait, type, seq, result, NULL);
}


int post_response(iowait_t *wait, int type, int seq, void *result,
        void (*fn)(void *dst, void *src))
{
    struct hlist_head *rsh;
    struct hlist_node *r;
    struct res_info *res;

    rsh = res_slot_head(wait, type, seq);

    pthread_mutex_lock(&wait->lock);
    hlist_for_each_entry(res, r, rsh, hentry) {
        if(res->type == type && res->seq == seq) {
            pthread_mutex_unlock(&wait->lock);
            goto found; 
        }
    }
    pthread_mutex_unlock(&wait->lock);
    return -EINVAL;

found:
    fn(res->res, result);

    complete(&res->done);
    return 0;
}

