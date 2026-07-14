#ifndef SAVVY_CORE_SNAPSHOT_H
#define SAVVY_CORE_SNAPSHOT_H

#include <pthread.h>
#include <stdint.h>
#include "savvy/core/error.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Immutable snapshot + single mutable owner (FND-04). One owner publishes
 * refcounted, immutable payloads (e.g. Config/Device state); readers
 * acquire a handle, read the payload lock-free, then release it. A
 * publish() never mutates a snapshot a reader still holds - the previous
 * payload is only freed once its last reader releases it, and that free
 * always happens with the owner lock NOT held (so free_fn may safely take
 * nontrivial time without blocking concurrent acquire()/release() calls -
 * it must still not call back into THIS SAME owner, which would self-
 * deadlock on publish()/release() re-entering this lock). */
typedef void (*savvy_snapshot_free_fn)(void *payload);

typedef struct savvy_snapshot_handle savvy_snapshot_handle_t; /* opaque */

typedef struct savvy_snapshot_owner {
    pthread_mutex_t lock;
    savvy_snapshot_handle_t *current;
    savvy_snapshot_free_fn free_fn;
    uint64_t version;
} savvy_snapshot_owner_t;

/* Returns SAVVY_ERR_UNKNOWN if the underlying mutex fails to initialize
 * (practically never happens, but the caller must not use *owner if this
 * returns non-OK). */
savvy_status_t savvy_snapshot_owner_init(savvy_snapshot_owner_t *owner, savvy_snapshot_free_fn free_fn);

/* On success, takes ownership of `payload` (owner will eventually free it
 * via free_fn once no reader holds it). On any error return
 * (SAVVY_ERR_OUT_OF_MEMORY), ownership is NOT transferred - the caller
 * still owns `payload` and must free it themselves. */
savvy_status_t savvy_snapshot_publish(savvy_snapshot_owner_t *owner, void *payload);

/* Returns NULL if nothing has been published yet. Every non-NULL return
 * must be paired with exactly one savvy_snapshot_release() of the same
 * handle. */
savvy_snapshot_handle_t *savvy_snapshot_acquire(savvy_snapshot_owner_t *owner, uint64_t *out_version);
void *savvy_snapshot_payload(savvy_snapshot_handle_t *handle);
void savvy_snapshot_release(savvy_snapshot_owner_t *owner, savvy_snapshot_handle_t *handle);

/* Frees the current payload (if any) and destroys the owner. Caller must
 * ensure no reader still holds a handle. */
void savvy_snapshot_owner_destroy(savvy_snapshot_owner_t *owner);

#ifdef __cplusplus
}
#endif

#endif
