#include "state_report.h"

#include <string.h>

void sensor_state_report_tracker_init(sensor_state_report_tracker_t *tracker) {
    memset(tracker, 0, sizeof(*tracker));
}

bool sensor_state_report_should_send(sensor_state_report_tracker_t *tracker,
                                      sensor_report_sensor_type_t type,
                                      int32_t new_value) {
    if (tracker->has_value[type] && tracker->last_value[type] == new_value) {
        return false;
    }

    /* Update before send is attempted, unconditionally - matches Android's
     * cache write, which is not conditioned on send success. */
    tracker->has_value[type] = true;
    tracker->last_value[type] = new_value;
    return true;
}
