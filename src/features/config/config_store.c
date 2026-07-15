#include "config_store.h"

#include <stdlib.h>
#include <string.h>

static void config_payload_free(void *payload) {
    free(payload);
}

savvy_status_t sensor_config_store_init(sensor_config_store_t *store) {
    if (store == NULL) {
        return SAVVY_ERR_INVALID_ARGUMENT;
    }

    savvy_status_t st = savvy_snapshot_owner_init(&store->snapshot, config_payload_free);
    if (st != SAVVY_OK) {
        return st;
    }
    if (pthread_mutex_init(&store->write_lock, NULL) != 0) {
        savvy_snapshot_owner_destroy(&store->snapshot);
        return SAVVY_ERR_UNKNOWN;
    }

    savvy_config_set_defaults(&store->working);
    return SAVVY_OK;
}

void sensor_config_store_destroy(sensor_config_store_t *store) {
    if (store == NULL) {
        return;
    }
    pthread_mutex_destroy(&store->write_lock);
    savvy_snapshot_owner_destroy(&store->snapshot);
}

savvy_status_t sensor_config_store_load_cached(sensor_config_store_t *store,
                                                const char *cached_json,
                                                size_t cached_len) {
    if (store == NULL) {
        return SAVVY_ERR_INVALID_ARGUMENT;
    }

    pthread_mutex_lock(&store->write_lock);

    savvy_config_t scratch;
    savvy_config_set_defaults(&scratch);

    savvy_status_t st = SAVVY_OK;
    if (cached_json != NULL && cached_len > 0) {
        st = savvy_config_parse(cached_json, cached_len, &scratch, NULL);
    }
    if (st != SAVVY_OK) {
        pthread_mutex_unlock(&store->write_lock);
        return st;
    }

    savvy_config_t *copy = malloc(sizeof(*copy));
    if (copy == NULL) {
        pthread_mutex_unlock(&store->write_lock);
        return SAVVY_ERR_OUT_OF_MEMORY;
    }
    memcpy(copy, &scratch, sizeof(*copy));

    st = savvy_snapshot_publish(&store->snapshot, copy);
    if (st != SAVVY_OK) {
        free(copy);
        pthread_mutex_unlock(&store->write_lock);
        return st;
    }

    store->working = scratch;

    pthread_mutex_unlock(&store->write_lock);
    return SAVVY_OK;
}

savvy_status_t sensor_config_store_apply_runtime(sensor_config_store_t *store,
                                                  const char *json, size_t len,
                                                  sensor_config_apply_result_t *out_result) {
    if (store == NULL || json == NULL || out_result == NULL) {
        return SAVVY_ERR_INVALID_ARGUMENT;
    }

    pthread_mutex_lock(&store->write_lock);

    /* Parse onto a scratch copy of the current value so a rejected parse
     * (S-003: malformed input -> reject, never crash) leaves `store`
     * completely untouched - Foundation's own parse contract doesn't
     * promise atomicity on a rejected call, so this store supplies it. */
    savvy_config_t scratch = store->working;
    savvy_status_t st = savvy_config_parse(json, len, &scratch, NULL);
    if (st != SAVVY_OK) {
        pthread_mutex_unlock(&store->write_lock);
        return st;
    }

    savvy_config_t *copy = malloc(sizeof(*copy));
    if (copy == NULL) {
        pthread_mutex_unlock(&store->write_lock);
        return SAVVY_ERR_OUT_OF_MEMORY;
    }
    memcpy(copy, &scratch, sizeof(*copy));

    st = savvy_snapshot_publish(&store->snapshot, copy);
    if (st != SAVVY_OK) {
        free(copy);
        pthread_mutex_unlock(&store->write_lock);
        return st;
    }

    /* Diff against the pre-apply value (Android actionConfig()'s
     * snapshot-old-scalars-then-compare pattern), restricted to this
     * session's two owned reactions. Computed after publish succeeds so
     * *out_result and the published snapshot can never disagree. */
    sensor_config_apply_result_t result;
    memset(&result, 0, sizeof(result));
    result.server_ip_changed = (strcmp(store->working.server_ip, scratch.server_ip) != 0);
    if (result.server_ip_changed) {
        memcpy(result.server_ip, scratch.server_ip, sizeof(result.server_ip));
    }
    result.use_rknn_raw_changed = (store->working.use_rknn != scratch.use_rknn);
    result.use_rknn_raw_old = store->working.use_rknn;
    result.use_rknn_raw_new = scratch.use_rknn;

    store->working = scratch;
    *out_result = result;

    pthread_mutex_unlock(&store->write_lock);
    return SAVVY_OK;
}

savvy_snapshot_handle_t *sensor_config_store_acquire(sensor_config_store_t *store, uint64_t *out_version) {
    if (store == NULL) {
        return NULL;
    }
    return savvy_snapshot_acquire(&store->snapshot, out_version);
}

const savvy_config_t *sensor_config_snapshot_payload(savvy_snapshot_handle_t *handle) {
    if (handle == NULL) {
        return NULL;
    }
    return (const savvy_config_t *)savvy_snapshot_payload(handle);
}

void sensor_config_store_release(sensor_config_store_t *store, savvy_snapshot_handle_t *handle) {
    if (store == NULL || handle == NULL) {
        return;
    }
    savvy_snapshot_release(&store->snapshot, handle);
}
