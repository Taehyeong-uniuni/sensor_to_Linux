#include "device_store.h"

#include <stdlib.h>
#include <string.h>

static void device_payload_free(void *payload) {
    free(payload);
}

typedef struct device_snapshot_payload {
    savvy_device_t value;
    size_t raw_json_len;
    char raw_json[];
} device_snapshot_payload_t;

static device_snapshot_payload_t *device_payload_create(const savvy_device_t *value,
                                                         const char *raw_json,
                                                         size_t raw_json_len) {
    if (raw_json_len > SIZE_MAX - sizeof(device_snapshot_payload_t) - 1u) {
        return NULL;
    }

    device_snapshot_payload_t *payload = malloc(sizeof(*payload) + raw_json_len + 1u);
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

/* Android MainActivity.setJsonDeviceDto(true), lines 1902-1912: zeroes
 * exactly these 9 stateful fields, regardless of what was just loaded
 * from cache/JSON. deviceSerial/deviceMAC/deviceIp/btName/appMgr/
 * appSensor/os/t_name/toilet/stall/appRknn/verRknn/smokeValue/
 * dataCollection are all left untouched by this step. */
static void reset_stateful_fields(savvy_device_t *device) {
    device->blue_tooth = 0;
    device->mic = 0;
    device->wifi = 0;
    device->tof = 0;
    device->led = 0;
    device->buzzer = 0;
    device->move_sensor = 0;
    device->beacon = 0;
    device->reboot = 0;
}

savvy_status_t sensor_device_store_init(sensor_device_store_t *store) {
    if (store == NULL) {
        return SAVVY_ERR_INVALID_ARGUMENT;
    }

    savvy_status_t st = savvy_snapshot_owner_init(&store->snapshot, device_payload_free);
    if (st != SAVVY_OK) {
        return st;
    }
    if (pthread_mutex_init(&store->write_lock, NULL) != 0) {
        savvy_snapshot_owner_destroy(&store->snapshot);
        return SAVVY_ERR_UNKNOWN;
    }

    savvy_device_set_defaults(&store->working);
    return SAVVY_OK;
}

void sensor_device_store_destroy(sensor_device_store_t *store) {
    if (store == NULL) {
        return;
    }
    pthread_mutex_destroy(&store->write_lock);
    savvy_snapshot_owner_destroy(&store->snapshot);
}

savvy_status_t sensor_device_store_load_cached(sensor_device_store_t *store,
                                                const char *cached_json,
                                                size_t cached_len) {
    if (store == NULL) {
        return SAVVY_ERR_INVALID_ARGUMENT;
    }

    pthread_mutex_lock(&store->write_lock);

    savvy_device_t scratch;
    savvy_device_set_defaults(&scratch);

    savvy_status_t st = SAVVY_OK;
    if (cached_json != NULL && cached_len > 0) {
        st = savvy_device_parse(cached_json, cached_len, &scratch, NULL);
    }
    if (st != SAVVY_OK) {
        pthread_mutex_unlock(&store->write_lock);
        return st;
    }

    reset_stateful_fields(&scratch);

    const char *raw_json = cached_json != NULL ? cached_json : "";
    size_t raw_json_len = (cached_json != NULL && cached_len > 0) ? cached_len : 0;
    device_snapshot_payload_t *copy = device_payload_create(&scratch, raw_json, raw_json_len);
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

savvy_status_t sensor_device_store_apply_runtime(sensor_device_store_t *store,
                                                  const char *json, size_t len,
                                                  sensor_device_apply_result_t *out_result) {
    if (store == NULL || json == NULL || out_result == NULL) {
        return SAVVY_ERR_INVALID_ARGUMENT;
    }

    pthread_mutex_lock(&store->write_lock);

    /* Runtime actionDevice() replaces its DTO with gson.fromJson(); do not
     * let Foundation's partial parser preserve fields from the old DTO. */
    savvy_device_t scratch;
    savvy_device_set_defaults(&scratch);
    savvy_status_t st = savvy_device_parse(json, len, &scratch, NULL);
    if (st != SAVVY_OK) {
        pthread_mutex_unlock(&store->write_lock);
        return st;
    }
    /* No reset_stateful_fields() here - Android's actionDevice() always
     * calls setJsonDeviceDto(false), never re-zeroing the 9 fields at
     * runtime, only at startup. */

    device_snapshot_payload_t *copy = device_payload_create(&scratch, json, len);
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

    sensor_device_apply_result_t result;
    result.data_collection_raw_old = store->working.data_collection;
    result.data_collection_raw_new = scratch.data_collection;

    store->working = scratch;
    *out_result = result;

    pthread_mutex_unlock(&store->write_lock);
    return SAVVY_OK;
}

savvy_snapshot_handle_t *sensor_device_store_acquire(sensor_device_store_t *store, uint64_t *out_version) {
    if (store == NULL) {
        return NULL;
    }
    return savvy_snapshot_acquire(&store->snapshot, out_version);
}

const savvy_device_t *sensor_device_snapshot_payload(savvy_snapshot_handle_t *handle) {
    if (handle == NULL) {
        return NULL;
    }
    return (const savvy_device_t *)savvy_snapshot_payload(handle);
}

const char *sensor_device_snapshot_raw_json(savvy_snapshot_handle_t *handle, size_t *out_len) {
    if (handle == NULL) {
        return NULL;
    }
    const device_snapshot_payload_t *payload =
        (const device_snapshot_payload_t *)savvy_snapshot_payload(handle);
    if (payload == NULL) {
        return NULL;
    }
    if (out_len != NULL) {
        *out_len = payload->raw_json_len;
    }
    return payload->raw_json;
}

void sensor_device_store_release(sensor_device_store_t *store, savvy_snapshot_handle_t *handle) {
    if (store == NULL || handle == NULL) {
        return;
    }
    savvy_snapshot_release(&store->snapshot, handle);
}
