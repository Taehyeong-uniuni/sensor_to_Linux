#ifndef SAVVY_CORE_LIFECYCLE_H
#define SAVVY_CORE_LIFECYCLE_H

#include <pthread.h>
#include <stdbool.h>
#include "savvy/core/error.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum savvy_lifecycle_state {
    SAVVY_LIFECYCLE_STOPPED = 0,
    SAVVY_LIFECYCLE_RUNNING
} savvy_lifecycle_state_t;

/* Single mutable owner of start/stop state (FND-04). start()/stop() are
 * idempotent: calling start() while already RUNNING, or stop() while
 * already STOPPED, returns SAVVY_OK as a no-op rather than an error. */
typedef struct savvy_lifecycle {
    pthread_mutex_t lock;
    savvy_lifecycle_state_t state;
} savvy_lifecycle_t;

void savvy_lifecycle_init(savvy_lifecycle_t *lc);

/* Always returns SAVVY_OK; *out_transitioned (if non-NULL) reports whether
 * THIS call performed STOPPED->RUNNING (false when already RUNNING). */
savvy_status_t savvy_lifecycle_start(savvy_lifecycle_t *lc, bool *out_transitioned);
savvy_status_t savvy_lifecycle_stop(savvy_lifecycle_t *lc, bool *out_transitioned);
savvy_lifecycle_state_t savvy_lifecycle_get(savvy_lifecycle_t *lc);
void savvy_lifecycle_destroy(savvy_lifecycle_t *lc);

#ifdef __cplusplus
}
#endif

#endif
