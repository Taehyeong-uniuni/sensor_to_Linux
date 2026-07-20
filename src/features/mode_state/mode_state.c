#include "mode_state.h"

bool sensor_mode_use_rknn_apply_startup(int32_t raw) {
    return raw != 0;
}

sensor_mode_transition_t sensor_mode_use_rknn_apply_live(int32_t old_raw, int32_t new_raw) {
    sensor_mode_transition_t result;
    result.changed = (old_raw != new_raw);
    result.runtime_value = (new_raw == 1);
    return result;
}

bool sensor_mode_data_collection_apply_startup(int32_t raw) {
    return raw == 1;
}

bool sensor_mode_data_collection_apply_live(int32_t raw) {
    return raw != 0;
}
