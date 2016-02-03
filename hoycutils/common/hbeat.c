#include <stdio.h>

#include <common/timer.h>
#include <common/log.h>
#include <common/hbeat.h>
#include <common/list.h>


void user_heartbeat(hbeat_node_t *hbeat)
{
    hbeat->count = HBEAT_INIT;
    hbeat->online = 1;
}

void hbeat_add_to_god(hbeat_god_t *god, hbeat_node_t *hbeat) 
{
    hbeat->count = HBEAT_INIT;
    hbeat->online = 1;

    pthread_mutex_lock(&god->lock);
    list_add_tail(&god->list, &hbeat->node);
    pthread_mutex_unlock(&god->lock);
}

void hbeat_rm_from_god(hbeat_god_t *god, hbeat_node_t *hbeat) 
{
    pthread_mutex_lock(&god->lock);
    list_remove(&hbeat->node);
    pthread_mutex_unlock(&god->lock);
}

void hbeat_god_handle(unsigned long data)
{
    hbeat_node_t *hbeat;
    hbeat_god_t *god = (hbeat_god_t *)data;

    list_for_each_entry(hbeat, &god->list, node) {
        hbeat->count--;

        if(hbeat->count <= 0) {
            hbeat->online = 0;
            god->dead(hbeat);
        }
    }

    mod_timer(god->timer, get_clock_ns() + HBEAD_DEAD_TIME);
}

void hbeat_god_init(hbeat_god_t *god, void (*dead)(hbeat_node_t *))
{
    list_init(&god->list);
    god->dead = dead;
    god->timer = new_timer(hbeat_god_handle, (unsigned long)god);
    pthread_mutex_init(&god->lock, NULL);

    add_timer(god->timer, get_clock_ns() + HBEAD_DEAD_TIME);
}


