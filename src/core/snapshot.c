#include "savvy/core/snapshot.h"
#include <stdlib.h>

struct savvy_snapshot_handle {
    void *payload;
    uint32_t refcount; /* protected by the owning savvy_snapshot_owner_t::lock */
    savvy_snapshot_free_fn free_fn;
};

savvy_status_t savvy_snapshot_owner_init(savvy_snapshot_owner_t *owner, savvy_snapshot_free_fn free_fn)
{
    if (pthread_mutex_init(&owner->lock, NULL) != 0) {
        return SAVVY_ERR_UNKNOWN;
    }
    owner->current = NULL;
    owner->free_fn = free_fn;
    owner->version = 0;
    return SAVVY_OK;
}

/* Decrements refcount under the caller's lock. Returns the handle itself
 * if this was the last reference (caller must finalize_unlocked() it
 * AFTER releasing the lock), or NULL if other references remain. */
static savvy_snapshot_handle_t *unref_locked(savvy_snapshot_handle_t *handle)
{
    handle->refcount--;
    return (handle->refcount == 0) ? handle : NULL;
}

/* Must be called with the owner lock NOT held - runs the user-provided
 * free_fn, which may take nontrivial time. */
static void finalize_unlocked(savvy_snapshot_handle_t *handle)
{
    if (handle == NULL) {
        return;
    }
    if (handle->free_fn != NULL) {
        handle->free_fn(handle->payload);
    }
    free(handle);
}

savvy_status_t savvy_snapshot_publish(savvy_snapshot_owner_t *owner, void *payload)
{
    savvy_snapshot_handle_t *next = (savvy_snapshot_handle_t *)malloc(sizeof(*next));
    if (next == NULL) {
        return SAVVY_ERR_OUT_OF_MEMORY; /* ownership of `payload` stays with the caller */
    }
    next->payload = payload;
    next->refcount = 1; /* owner's own reference */
    next->free_fn = owner->free_fn;

    pthread_mutex_lock(&owner->lock);
    savvy_snapshot_handle_t *prev = owner->current;
    owner->current = next;
    owner->version++;
    savvy_snapshot_handle_t *to_finalize = (prev != NULL) ? unref_locked(prev) : NULL;
    pthread_mutex_unlock(&owner->lock);

    finalize_unlocked(to_finalize);
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
    savvy_snapshot_handle_t *to_finalize = unref_locked(handle);
    pthread_mutex_unlock(&owner->lock);

    finalize_unlocked(to_finalize);
}

void savvy_snapshot_owner_destroy(savvy_snapshot_owner_t *owner)
{
    pthread_mutex_lock(&owner->lock);
    savvy_snapshot_handle_t *to_finalize = NULL;
    if (owner->current != NULL) {
        to_finalize = unref_locked(owner->current);
        owner->current = NULL;
    }
    pthread_mutex_unlock(&owner->lock);

    finalize_unlocked(to_finalize);
    pthread_mutex_destroy(&owner->lock);
}
