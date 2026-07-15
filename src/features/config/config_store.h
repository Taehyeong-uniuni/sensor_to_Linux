#ifndef SENSOR_CORE_CONFIG_STORE_H
#define SENSOR_CORE_CONFIG_STORE_H

#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "savvy/core/error.h"
#include "savvy/core/snapshot.h"
#include "savvy/protocol/config_codec.h"

#ifdef __cplusplus
extern "C" {
#endif

/* SNC-02 Config store: full JSON persistence + immutable snapshot publish,
 * mirroring Android MainActivity.setJsonConfigDto()/actionConfig() (pinned
 * savvy_sensor@48e2d1442cd867cc60f8ff3186d53fce1c08f308).
 *
 * `working` is used only for old-vs-new reaction diffs. Each successful
 * parse starts from Foundation defaults, then publishes one heap payload
 * containing both the typed Config and the exact input JSON. A reader must
 * retain its snapshot handle while using either accessor. */
typedef struct sensor_config_store {
    savvy_snapshot_owner_t snapshot;
    pthread_mutex_t write_lock;
    savvy_config_t working;
} sensor_config_store_t;

/* Diff of a runtime Config apply, restricted to the two keys this session
 * owns a reaction for (Android actionConfig() diffs about a dozen fields
 * total, but the pixel/millimeter, fracture, reset-frame, same-frame, and
 * decibel fields are ToF/mic consumers owned by CC-SENSOR-TOF and
 * CC-SENSOR-INPUT, and compress has no actionConfig reaction at all - own
 * only what CC-SENSOR-CORE owns). use_rknn_raw_changed mirrors
 * actionConfig()'s preUseRknn-vs-new guard exactly: mode_state's live rule
 * must only be (re)applied when this is true (see
 * sensor_mode_use_rknn_apply_live in the mode_state feature), never on
 * every apply. */
typedef struct sensor_config_apply_result {
    bool server_ip_changed;
    char server_ip[SAVVY_CONFIG_STR_LEN];
    bool use_rknn_raw_changed;
    int32_t use_rknn_raw_old;
    int32_t use_rknn_raw_new;
} sensor_config_apply_result_t;

savvy_status_t sensor_config_store_init(sensor_config_store_t *store);
void sensor_config_store_destroy(sensor_config_store_t *store);

/* Startup path - mirrors Android setJsonConfigDto(): cached_json
 * NULL/empty means "no cache" (Android's jsonData.isEmpty() branch) and
 * applies savvy_config_set_defaults(); otherwise parses cached_json onto
 * defaults (full replace - Android's gson.fromJson() always produces a
 * brand new object, it does not merge onto a prior in-memory instance).
 * Always publishes an initial snapshot on success. */
savvy_status_t sensor_config_store_load_cached(sensor_config_store_t *store,
                                                const char *cached_json,
                                                size_t cached_len);

/* Runtime path - mirrors Android actionConfig(): creates a fresh
 * Foundation-default Config for every `json`, so omitted fields reset to
 * Android/Foundation defaults rather than retaining prior live values.
 * Typed snapshot and byte-exact raw JSON are published together only on
 * SAVVY_OK. A rejected parse leaves both previous values and *out_result
 * untouched. */
savvy_status_t sensor_config_store_apply_runtime(sensor_config_store_t *store,
                                                  const char *json, size_t len,
                                                  sensor_config_apply_result_t *out_result);

/* Acquire/release the current immutable Config snapshot for readers.
 * Returns NULL from acquire if load_cached() has never been called. */
savvy_snapshot_handle_t *sensor_config_store_acquire(sensor_config_store_t *store, uint64_t *out_version);
const savvy_config_t *sensor_config_snapshot_payload(savvy_snapshot_handle_t *handle);
/* Returns the exact successful JSON bytes and length paired with this
 * handle's typed Config. The returned pointer remains valid only until the
 * corresponding sensor_config_store_release(). */
const char *sensor_config_snapshot_raw_json(savvy_snapshot_handle_t *handle, size_t *out_len);
void sensor_config_store_release(sensor_config_store_t *store, savvy_snapshot_handle_t *handle);

#ifdef __cplusplus
}
#endif

#endif
