#include "savvy/core/queue.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

savvy_status_t savvy_queue_init(savvy_queue_t *q, size_t capacity, size_t item_size,
                                 savvy_queue_item_destroy_fn item_destroy_fn)
{
    if (q == NULL || capacity == 0 || item_size == 0) {
        return SAVVY_ERR_INVALID_ARGUMENT;
    }
    if (capacity > SIZE_MAX / item_size) {
        return SAVVY_ERR_INVALID_ARGUMENT; /* capacity * item_size would overflow */
    }

    q->storage = (unsigned char *)malloc(capacity * item_size);
    if (q->storage == NULL) {
        return SAVVY_ERR_OUT_OF_MEMORY;
    }
    if (pthread_mutex_init(&q->lock, NULL) != 0) {
        free(q->storage);
        q->storage = NULL;
        return SAVVY_ERR_UNKNOWN;
    }
    if (pthread_cond_init(&q->not_empty, NULL) != 0) {
        pthread_mutex_destroy(&q->lock);
        free(q->storage);
        q->storage = NULL;
        return SAVVY_ERR_UNKNOWN;
    }
    if (pthread_cond_init(&q->not_full, NULL) != 0) {
        pthread_cond_destroy(&q->not_empty);
        pthread_mutex_destroy(&q->lock);
        free(q->storage);
        q->storage = NULL;
        return SAVVY_ERR_UNKNOWN;
    }

    q->item_size = item_size;
    q->capacity = capacity;
    q->count = 0;
    q->head = 0;
    q->closed = false;
    q->item_destroy_fn = item_destroy_fn;
    return SAVVY_OK;
}

savvy_status_t savvy_queue_push(savvy_queue_t *q, const void *item)
{
    pthread_mutex_lock(&q->lock);
    while (q->count == q->capacity && !q->closed) {
        pthread_cond_wait(&q->not_full, &q->lock);
    }
    if (q->closed) {
        pthread_mutex_unlock(&q->lock);
        return SAVVY_ERR_CLOSED;
    }
    size_t tail = (q->head + q->count) % q->capacity;
    memcpy(q->storage + tail * q->item_size, item, q->item_size);
    q->count++;
    pthread_cond_signal(&q->not_empty);
    pthread_mutex_unlock(&q->lock);
    return SAVVY_OK;
}

savvy_status_t savvy_queue_try_push(savvy_queue_t *q, const void *item)
{
    pthread_mutex_lock(&q->lock);
    if (q->closed) {
        pthread_mutex_unlock(&q->lock);
        return SAVVY_ERR_CLOSED;
    }
    if (q->count == q->capacity) {
        pthread_mutex_unlock(&q->lock);
        return SAVVY_ERR_OVERFLOW;
    }
    size_t tail = (q->head + q->count) % q->capacity;
    memcpy(q->storage + tail * q->item_size, item, q->item_size);
    q->count++;
    pthread_cond_signal(&q->not_empty);
    pthread_mutex_unlock(&q->lock);
    return SAVVY_OK;
}

savvy_status_t savvy_queue_pop(savvy_queue_t *q, void *out_item)
{
    pthread_mutex_lock(&q->lock);
    while (q->count == 0 && !q->closed) {
        pthread_cond_wait(&q->not_empty, &q->lock);
    }
    if (q->count == 0 && q->closed) {
        pthread_mutex_unlock(&q->lock);
        return SAVVY_ERR_CLOSED;
    }
    memcpy(out_item, q->storage + q->head * q->item_size, q->item_size);
    q->head = (q->head + 1) % q->capacity;
    q->count--;
    pthread_cond_signal(&q->not_full);
    pthread_mutex_unlock(&q->lock);
    return SAVVY_OK;
}

void savvy_queue_close(savvy_queue_t *q)
{
    pthread_mutex_lock(&q->lock);
    q->closed = true;
    pthread_cond_broadcast(&q->not_empty);
    pthread_cond_broadcast(&q->not_full);
    pthread_mutex_unlock(&q->lock);
}

void savvy_queue_cancel(savvy_queue_t *q)
{
    pthread_mutex_lock(&q->lock);
    if (q->item_destroy_fn != NULL) {
        for (size_t i = 0; i < q->count; i++) {
            size_t idx = (q->head + i) % q->capacity;
            q->item_destroy_fn(q->storage + idx * q->item_size);
        }
    }
    q->closed = true;
    q->count = 0;
    q->head = 0;
    pthread_cond_broadcast(&q->not_empty);
    pthread_cond_broadcast(&q->not_full);
    pthread_mutex_unlock(&q->lock);
}

void savvy_queue_destroy(savvy_queue_t *q)
{
    if (q->item_destroy_fn != NULL) {
        for (size_t i = 0; i < q->count; i++) {
            size_t idx = (q->head + i) % q->capacity;
            q->item_destroy_fn(q->storage + idx * q->item_size);
        }
    }
    pthread_mutex_destroy(&q->lock);
    pthread_cond_destroy(&q->not_empty);
    pthread_cond_destroy(&q->not_full);
    free(q->storage);
    q->storage = NULL;
}
