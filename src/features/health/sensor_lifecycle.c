#include "sensor_lifecycle.h"

#include <string.h>

/* A fan-out always uses a value snapshot. Registration performed by a
 * callback therefore affects the next notification, never the one already
 * being dispatched. No callback runs with registry_lock held. */
static size_t snapshot_modules(sensor_lifecycle_t *lc,
                               sensor_lifecycle_hooks_t snapshot[SENSOR_LIFECYCLE_MAX_MODULES]) {
    pthread_mutex_lock(&lc->registry_lock);
    size_t count = lc->module_count;
    memcpy(snapshot, lc->modules, count * sizeof(snapshot[0]));
    lc->callback_depth += 1;
    pthread_mutex_unlock(&lc->registry_lock);
    return count;
}

static void finish_module_snapshot(sensor_lifecycle_t *lc) {
    pthread_mutex_lock(&lc->registry_lock);
    lc->callback_depth -= 1;
    pthread_mutex_unlock(&lc->registry_lock);
}

savvy_status_t sensor_lifecycle_init(sensor_lifecycle_t *lc) {
    if (lc == NULL) {
        return SAVVY_ERR_INVALID_ARGUMENT;
    }

    /* Keep observable fields defined even when a later primitive init
     * fails. destroy() is valid only after this function returns SAVVY_OK. */
    memset(lc->modules, 0, sizeof(lc->modules));
    lc->module_count = 0;
    lc->callback_depth = 0;

    savvy_status_t st = savvy_lifecycle_init(&lc->base);
    if (st != SAVVY_OK) {
        return st;
    }
    if (pthread_mutex_init(&lc->registry_lock, NULL) != 0) {
        savvy_lifecycle_destroy(&lc->base);
        return SAVVY_ERR_UNKNOWN;
    }

    return SAVVY_OK;
}

void sensor_lifecycle_destroy(sensor_lifecycle_t *lc) {
    if (lc == NULL) {
        return;
    }
    pthread_mutex_lock(&lc->registry_lock);
    /* The public destroy API has no error return. Refuse a callback's
     * reentrant destroy rather than invalidating the snapshot currently on
     * that callback's stack; its owner can destroy after fan-out returns. */
    if (lc->callback_depth != 0) {
        pthread_mutex_unlock(&lc->registry_lock);
        return;
    }
    pthread_mutex_unlock(&lc->registry_lock);
    pthread_mutex_destroy(&lc->registry_lock);
    savvy_lifecycle_destroy(&lc->base);
}

savvy_status_t sensor_lifecycle_register_module(sensor_lifecycle_t *lc,
                                                 const sensor_lifecycle_hooks_t *hooks) {
    if (lc == NULL || hooks == NULL || hooks->module_id == NULL) {
        return SAVVY_ERR_INVALID_ARGUMENT;
    }

    pthread_mutex_lock(&lc->registry_lock);

    if (lc->module_count >= SENSOR_LIFECYCLE_MAX_MODULES) {
        pthread_mutex_unlock(&lc->registry_lock);
        return SAVVY_ERR_OVERFLOW;
    }

    lc->modules[lc->module_count] = *hooks;
    lc->module_count += 1;

    pthread_mutex_unlock(&lc->registry_lock);
    return SAVVY_OK;
}

savvy_status_t sensor_lifecycle_start(sensor_lifecycle_t *lc) {
    if (lc == NULL) {
        return SAVVY_ERR_INVALID_ARGUMENT;
    }

    bool transitioned = false;
    savvy_status_t st = savvy_lifecycle_start(&lc->base, &transitioned);
    if (st != SAVVY_OK || !transitioned) {
        return st;
    }

    sensor_lifecycle_hooks_t snapshot[SENSOR_LIFECYCLE_MAX_MODULES];
    size_t count = snapshot_modules(lc, snapshot);
    for (size_t i = 0; i < count; i++) {
        if (snapshot[i].on_start != NULL) {
            snapshot[i].on_start(snapshot[i].user_data);
        }
    }
    finish_module_snapshot(lc);

    return SAVVY_OK;
}

void sensor_lifecycle_notify_config_applied(sensor_lifecycle_t *lc) {
    if (lc == NULL) {
        return;
    }

    sensor_lifecycle_hooks_t snapshot[SENSOR_LIFECYCLE_MAX_MODULES];
    size_t count = snapshot_modules(lc, snapshot);
    for (size_t i = 0; i < count; i++) {
        if (snapshot[i].on_config_applied != NULL) {
            snapshot[i].on_config_applied(snapshot[i].user_data);
        }
    }
    finish_module_snapshot(lc);
}

savvy_status_t sensor_lifecycle_stop(sensor_lifecycle_t *lc) {
    if (lc == NULL) {
        return SAVVY_ERR_INVALID_ARGUMENT;
    }

    bool transitioned = false;
    savvy_status_t st = savvy_lifecycle_stop(&lc->base, &transitioned);
    if (st != SAVVY_OK || !transitioned) {
        return st;
    }

    sensor_lifecycle_hooks_t snapshot[SENSOR_LIFECYCLE_MAX_MODULES];
    size_t count = snapshot_modules(lc, snapshot);
    for (size_t i = 0; i < count; i++) {
        if (snapshot[i].on_shutdown != NULL) {
            snapshot[i].on_shutdown(snapshot[i].user_data);
        }
    }
    finish_module_snapshot(lc);

    return SAVVY_OK;
}

savvy_lifecycle_state_t sensor_lifecycle_get_state(sensor_lifecycle_t *lc) {
    return savvy_lifecycle_get(&lc->base);
}
