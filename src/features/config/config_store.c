#include "config_store.h"

#include <stdlib.h>
#include <string.h>

static void config_payload_free(void *payload) {
    free(payload);
}

/* Keep the typed value and the exact successful input in one Foundation
 * snapshot.  `value` is deliberately first so the established typed
 * accessor remains a pointer to savvy_config_t, while the raw accessor
 * below can use the same handle and therefore the same reader lifetime. */
typedef struct config_snapshot_payload {
    savvy_config_t value;
    size_t raw_json_len;
    char raw_json[];
} config_snapshot_payload_t;

static config_snapshot_payload_t *config_payload_create(const savvy_config_t *value,
                                                         const char *raw_json,
                                                         size_t raw_json_len) {
    if (raw_json_len > SIZE_MAX - sizeof(config_snapshot_payload_t) - 1u) {
        return NULL;
    }

    config_snapshot_payload_t *payload = malloc(sizeof(*payload) + raw_json_len + 1u);
    if (payload == NULL) {
        return NULL;
    }
    payload->value = *value;
    payload->raw_json_len = raw_json_len;
    if (raw_json_len > 0) {
        memcpy(payload->raw_json, raw_json, raw_json_len);
    }
    payload->raw_json[raw_json_len] = '\0';
    return payload;
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

    const char *raw_json = cached_json != NULL ? cached_json : "";
    size_t raw_json_len = (cached_json != NULL && cached_len > 0) ? cached_len : 0;
    config_snapshot_payload_t *copy = config_payload_create(&scratch, raw_json, raw_json_len);
    if (copy == NULL) {
        pthread_mutex_unlock(&store->write_lock);
        return SAVVY_ERR_OUT_OF_MEMORY;
    }
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

    /* Android actionConfig() assigns gson.fromJson()'s newly-created DTO;
     * it never merges a partial message into the previous live DTO.  The
     * Foundation codec follows its documented partial-write contract, so
     * start from Foundation defaults on every runtime message. */
    savvy_config_t scratch;
    savvy_config_set_defaults(&scratch);
    savvy_status_t st = savvy_config_parse(json, len, &scratch, NULL);
    if (st != SAVVY_OK) {
        pthread_mutex_unlock(&store->write_lock);
        return st;
    }

    config_snapshot_payload_t *copy = config_payload_create(&scratch, json, len);
    if (copy == NULL) {
        pthread_mutex_unlock(&store->write_lock);
        return SAVVY_ERR_OUT_OF_MEMORY;
    }
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

const char *sensor_config_snapshot_raw_json(savvy_snapshot_handle_t *handle, size_t *out_len) {
    if (handle == NULL) {
        return NULL;
    }
    const config_snapshot_payload_t *payload =
        (const config_snapshot_payload_t *)savvy_snapshot_payload(handle);
    if (payload == NULL) {
        return NULL;
    }
    if (out_len != NULL) {
        *out_len = payload->raw_json_len;
    }
    return payload->raw_json;
}

void sensor_config_store_release(sensor_config_store_t *store, savvy_snapshot_handle_t *handle) {
    if (store == NULL || handle == NULL) {
        return;
    }
    savvy_snapshot_release(&store->snapshot, handle);
}
