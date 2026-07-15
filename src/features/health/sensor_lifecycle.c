#include "sensor_lifecycle.h"

#include <string.h>

savvy_status_t sensor_lifecycle_init(sensor_lifecycle_t *lc) {
    if (lc == NULL) {
        return SAVVY_ERR_INVALID_ARGUMENT;
    }

    savvy_status_t st = savvy_lifecycle_init(&lc->base);
    if (st != SAVVY_OK) {
        return st;
    }
    if (pthread_mutex_init(&lc->registry_lock, NULL) != 0) {
        savvy_lifecycle_destroy(&lc->base);
        return SAVVY_ERR_UNKNOWN;
    }

    memset(lc->modules, 0, sizeof(lc->modules));
    lc->module_count = 0;
    return SAVVY_OK;
}

void sensor_lifecycle_destroy(sensor_lifecycle_t *lc) {
    if (lc == NULL) {
        return;
    }
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

    pthread_mutex_lock(&lc->registry_lock);
    for (size_t i = 0; i < lc->module_count; i++) {
        if (lc->modules[i].on_start != NULL) {
            lc->modules[i].on_start(lc->modules[i].user_data);
        }
    }
    pthread_mutex_unlock(&lc->registry_lock);

    return SAVVY_OK;
}

void sensor_lifecycle_notify_config_applied(sensor_lifecycle_t *lc) {
    if (lc == NULL) {
        return;
    }

    pthread_mutex_lock(&lc->registry_lock);
    for (size_t i = 0; i < lc->module_count; i++) {
        if (lc->modules[i].on_config_applied != NULL) {
            lc->modules[i].on_config_applied(lc->modules[i].user_data);
        }
    }
    pthread_mutex_unlock(&lc->registry_lock);
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

    pthread_mutex_lock(&lc->registry_lock);
    for (size_t i = 0; i < lc->module_count; i++) {
        if (lc->modules[i].on_shutdown != NULL) {
            lc->modules[i].on_shutdown(lc->modules[i].user_data);
        }
    }
    pthread_mutex_unlock(&lc->registry_lock);

    return SAVVY_OK;
}

savvy_lifecycle_state_t sensor_lifecycle_get_state(sensor_lifecycle_t *lc) {
    return savvy_lifecycle_get(&lc->base);
}
