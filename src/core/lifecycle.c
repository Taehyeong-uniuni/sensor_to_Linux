#include "savvy/core/lifecycle.h"
#include <stddef.h>

savvy_status_t savvy_lifecycle_init(savvy_lifecycle_t *lc)
{
    if (pthread_mutex_init(&lc->lock, NULL) != 0) {
        return SAVVY_ERR_UNKNOWN;
    }
    lc->state = SAVVY_LIFECYCLE_STOPPED;
    return SAVVY_OK;
}

savvy_status_t savvy_lifecycle_start(savvy_lifecycle_t *lc, bool *out_transitioned)
{
    pthread_mutex_lock(&lc->lock);
    bool transitioned = (lc->state == SAVVY_LIFECYCLE_STOPPED);
    lc->state = SAVVY_LIFECYCLE_RUNNING;
    pthread_mutex_unlock(&lc->lock);
    if (out_transitioned != NULL) {
        *out_transitioned = transitioned;
    }
    return SAVVY_OK;
}

savvy_status_t savvy_lifecycle_stop(savvy_lifecycle_t *lc, bool *out_transitioned)
{
    pthread_mutex_lock(&lc->lock);
    bool transitioned = (lc->state == SAVVY_LIFECYCLE_RUNNING);
    lc->state = SAVVY_LIFECYCLE_STOPPED;
    pthread_mutex_unlock(&lc->lock);
    if (out_transitioned != NULL) {
        *out_transitioned = transitioned;
    }
    return SAVVY_OK;
}

savvy_lifecycle_state_t savvy_lifecycle_get(savvy_lifecycle_t *lc)
{
    pthread_mutex_lock(&lc->lock);
    savvy_lifecycle_state_t s = lc->state;
    pthread_mutex_unlock(&lc->lock);
    return s;
}

void savvy_lifecycle_destroy(savvy_lifecycle_t *lc)
{
    pthread_mutex_destroy(&lc->lock);
}
