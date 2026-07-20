#ifndef SENSOR_CORE_UPDATE_GUARD_H
#define SENSOR_CORE_UPDATE_GUARD_H

#include <pthread.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Mirrors Android's mIsApkUpdate flag (pinned savvy_sensor@48e2d1442cd86
 * 7cc60f8ff3186d53fce1c08f308, MainActivity.java): actionApkUpdate() sets
 * it true on an APK_UPDATE broadcast, callToF_PirIn() checks it first.
 * No reset exists anywhere in that file - do not add one here either. */
typedef struct sensor_update_guard {
    pthread_mutex_t lock;
    bool tripped;
} sensor_update_guard_t;

void sensor_update_guard_init(sensor_update_guard_t *guard);
void sensor_update_guard_destroy(sensor_update_guard_t *guard);

/* Mirrors actionApkUpdate(): unconditionally sets tripped = true.
 * Idempotent - safe across repeated broadcasts. */
void sensor_update_guard_on_apk_update(sensor_update_guard_t *guard);

/* Mirrors callToF_PirIn()'s `if(mIsApkUpdate){ return; }` guard. */
bool sensor_update_guard_should_allow_pir_in(sensor_update_guard_t *guard);

bool sensor_update_guard_is_tripped(sensor_update_guard_t *guard);

#ifdef __cplusplus
}
#endif

#endif
