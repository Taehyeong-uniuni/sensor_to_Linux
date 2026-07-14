#include "savvy/core/snapshot.h"
#include <stdlib.h>

struct savvy_snapshot_handle {
    void *payload;
    uint32_t refcount; /* protected by the owning savvy_snapshot_owner_t::lock */
    savvy_snapshot_free_fn free_fn;
};

void savvy_snapshot_owner_init(savvy_snapshot_owner_t *owner, savvy_snapshot_free_fn free_fn)
{
    pthread_mutex_init(&owner->lock, NULL);
    owner->current = NULL;
    owner->free_fn = free_fn;
    owner->version = 0;
}

static void handle_unref_locked(savvy_snapshot_handle_t *handle)
{
    handle->refcount--;
    if (handle->refcount == 0) {
        if (handle->free_fn != NULL) {
            handle->free_fn(handle->payload);
        }
        free(handle);
    }
}

savvy_status_t savvy_snapshot_publish(savvy_snapshot_owner_t *owner, void *payload)
{
    savvy_snapshot_handle_t *next = (savvy_snapshot_handle_t *)malloc(sizeof(*next));
    if (next == NULL) {
        return SAVVY_ERR_OUT_OF_MEMORY;
    }
    next->payload = payload;
    next->refcount = 1; /* owner's own reference */
    next->free_fn = owner->free_fn;

    pthread_mutex_lock(&owner->lock);
    savvy_snapshot_handle_t *prev = owner->current;
    owner->current = next;
    owner->version++;
    if (prev != NULL) {
        handle_unref_locked(prev);
    }
    pthread_mutex_unlock(&owner->lock);
    return SAVVY_OK;
}

savvy_snapshot_handle_t *savvy_snapshot_acquire(savvy_snapshot_owner_t *owner, uint64_t *out_version)
{
    pthread_mutex_lock(&owner->lock);
    savvy_snapshot_handle_t *h = owner->current;
    if (h != NULL) {
        h->refcount++;
    }
    if (out_version != NULL) {
        *out_version = owner->version;
    }
    pthread_mutex_unlock(&owner->lock);
    return h;
}

void *savvy_snapshot_payload(savvy_snapshot_handle_t *handle)
{
    return (handle != NULL) ? handle->payload : NULL;
}

void savvy_snapshot_release(savvy_snapshot_owner_t *owner, savvy_snapshot_handle_t *handle)
{
    if (handle == NULL) {
        return;
    }
    pthread_mutex_lock(&owner->lock);
    handle_unref_locked(handle);
    pthread_mutex_unlock(&owner->lock);
}

void savvy_snapshot_owner_destroy(savvy_snapshot_owner_t *owner)
{
    if (owner->current != NULL) {
        handle_unref_locked(owner->current);
        owner->current = NULL;
    }
    pthread_mutex_destroy(&owner->lock);
}
