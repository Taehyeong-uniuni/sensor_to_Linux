#ifndef SENSOR_CORE_MODE_STATE_H
#define SENSOR_CORE_MODE_STATE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Startup rule: no previous value to diff against - this IS the initial
 * evaluation at daemon boot from the cached Config. */
bool sensor_mode_use_rknn_apply_startup(int32_t raw);

typedef struct sensor_mode_transition {
    bool changed;       /* old_raw != new_raw */
    bool runtime_value; /* meaningful regardless of changed */
} sensor_mode_transition_t;

/* useRknn's live rule is diff-gated in Android (only reacts inside
 * `if(preUseRknn != mJsonConfigDto.useRknn)`) - callers must ignore
 * runtime_value when changed is false, mirroring that skipped branch.
 * dataCollection's live rule below has no such gate; the two-arg vs.
 * one-arg signatures make this asymmetry impossible to mix up. */
sensor_mode_transition_t sensor_mode_use_rknn_apply_live(int32_t old_raw, int32_t new_raw);

bool sensor_mode_data_collection_apply_startup(int32_t raw);

bool sensor_mode_data_collection_apply_live(int32_t raw);

#ifdef __cplusplus
}
#endif

#endif
