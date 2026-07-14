#ifndef SAVVY_CORE_QUEUE_H
#define SAVVY_CORE_QUEUE_H

#include <stddef.h>
#include <stdbool.h>
#include <pthread.h>
#include "savvy/core/error.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Invoked on any item still buffered when cancel()/destroy() runs, so an
 * item that owns a heap pointer/FD isn't silently leaked. Must be fast
 * and must NOT call back into this queue's own API (cancel() calls it
 * while still holding the queue lock) - keep it to freeing/closing
 * resources the item embeds, nothing that blocks or reenters. Pass NULL
 * if items are plain value types needing no cleanup. */
typedef void (*savvy_queue_item_destroy_fn)(void *item);

/* Fixed-capacity, fixed-item-size blocking queue with explicit close/cancel
 * semantics (FND-04):
 *  - close(): stop accepting new pushes; pops keep draining buffered items,
 *    then return SAVVY_ERR_CLOSED once empty.
 *  - cancel(): immediately discard buffered items (running item_destroy_fn
 *    on each first, if set) and wake all blocked callers with
 *    SAVVY_ERR_CLOSED. */
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
    savvy_queue_item_destroy_fn item_destroy_fn;
} savvy_queue_t;

/* Fails with SAVVY_ERR_INVALID_ARGUMENT if capacity * item_size would
 * overflow size_t (checked before the allocation, not after). */
savvy_status_t savvy_queue_init(savvy_queue_t *q, size_t capacity, size_t item_size,
                                 savvy_queue_item_destroy_fn item_destroy_fn);
savvy_status_t savvy_queue_push(savvy_queue_t *q, const void *item);
/* Non-blocking: fails immediately with SAVVY_ERR_OVERFLOW if the queue is
 * currently full, instead of waiting for room. */
savvy_status_t savvy_queue_try_push(savvy_queue_t *q, const void *item);
savvy_status_t savvy_queue_pop(savvy_queue_t *q, void *out_item);
void savvy_queue_close(savvy_queue_t *q);
void savvy_queue_cancel(savvy_queue_t *q);
void savvy_queue_destroy(savvy_queue_t *q);

#ifdef __cplusplus
}
#endif

#endif
