#ifndef SENSOR_CORE_DEVICE_STORE_H
#define SENSOR_CORE_DEVICE_STORE_H

#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "savvy/core/error.h"
#include "savvy/core/snapshot.h"
#include "savvy/protocol/device_codec.h"

#ifdef __cplusplus
extern "C" {
#endif

/* SNC-02 Device store: full JSON persistence + immutable snapshot publish,
 * mirroring Android MainActivity.setJsonDeviceDto()/actionDevice() (pinned
 * savvy_sensor@48e2d1442cd867cc60f8ff3186d53fce1c08f308, lines 1892-1914
 * and 2112-2131). */
typedef struct sensor_device_store {
    savvy_snapshot_owner_t snapshot;
    pthread_mutex_t write_lock;
    savvy_device_t working;
} sensor_device_store_t;

/* Diff of a runtime Device apply. dataCollection has NO change-gate in
 * Android's actionDevice() (unlike Config's useRknn) - it is
 * unconditionally re-derived on every apply, so this always reports the
 * new raw value; mode_state's live rule must be invoked every time,
 * not just when the raw value changes. */
typedef struct sensor_device_apply_result {
    int32_t data_collection_raw_old;
    int32_t data_collection_raw_new;
} sensor_device_apply_result_t;

savvy_status_t sensor_device_store_init(sensor_device_store_t *store);
void sensor_device_store_destroy(sensor_device_store_t *store);

/* Startup path - mirrors Android setJsonDeviceDto(true): cached_json
 * NULL/empty -> savvy_device_set_defaults(); otherwise parses cached_json
 * onto defaults (full replace). Then UNCONDITIONALLY zeroes the 9
 * stateful fields regardless of what was cached/parsed - Android line
 * 1902-1912 zeroes blueTooth/mic/wifi/tof/led/buzzer/moveSensor/beacon/
 * reboot every time isInited=true, i.e. only at this startup call, never
 * from apply_runtime below. Always publishes an initial snapshot on
 * success. */
savvy_status_t sensor_device_store_load_cached(sensor_device_store_t *store,
                                                const char *cached_json,
                                                size_t cached_len);

/* Runtime path - mirrors Android actionDevice(): parses `json` onto the
 * current working value (missing keys retain their existing value), does
 * NOT reset the 9 stateful fields (isInited=false path), diffs
 * dataCollection unconditionally. A rejected parse (S-003) leaves `store`
 * and *out_result untouched. */
savvy_status_t sensor_device_store_apply_runtime(sensor_device_store_t *store,
                                                  const char *json, size_t len,
                                                  sensor_device_apply_result_t *out_result);

savvy_snapshot_handle_t *sensor_device_store_acquire(sensor_device_store_t *store, uint64_t *out_version);
const savvy_device_t *sensor_device_snapshot_payload(savvy_snapshot_handle_t *handle);
void sensor_device_store_release(sensor_device_store_t *store, savvy_snapshot_handle_t *handle);

#ifdef __cplusplus
}
#endif

#endif
