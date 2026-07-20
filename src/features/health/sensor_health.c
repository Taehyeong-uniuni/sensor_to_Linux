#include "sensor_health.h"

sensor_health_status_t sensor_health_snapshot(sensor_lifecycle_t *lc, bool update_guard_tripped) {
    sensor_health_status_t status;
    status.lifecycle_state = sensor_lifecycle_get_state(lc);
    status.update_guard_tripped = update_guard_tripped;
    return status;
}
