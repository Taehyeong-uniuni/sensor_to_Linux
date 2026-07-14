#ifndef SAVVY_CORE_QUEUE_H
#define SAVVY_CORE_QUEUE_H

#include <stddef.h>
#include <stdbool.h>
#include <pthread.h>
#include "savvy/core/error.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Fixed-capacity, fixed-item-size blocking queue with explicit close/cancel
 * semantics (FND-04):
 *  - close(): stop accepting new pushes; pops keep draining buffered items,
 *    then return SAVVY_ERR_CLOSED once empty.
 *  - cancel(): immediately discard buffered items and wake all blocked
 *    callers with SAVVY_ERR_CLOSED. */
typedef struct savvy_queue {
    pthread_mutex_t lock;
    pthread_cond_t not_empty;
    pthread_cond_t not_full;
    unsigned char *storage;
    size_t item_size;
    size_t capacity;
    size_t count;
    size_t head;
    bool closed;
} savvy_queue_t;

savvy_status_t savvy_queue_init(savvy_queue_t *q, size_t capacity, size_t item_size);
savvy_status_t savvy_queue_push(savvy_queue_t *q, const void *item);
savvy_status_t savvy_queue_pop(savvy_queue_t *q, void *out_item);
void savvy_queue_close(savvy_queue_t *q);
void savvy_queue_cancel(savvy_queue_t *q);
void savvy_queue_destroy(savvy_queue_t *q);

#ifdef __cplusplus
}
#endif

#endif
