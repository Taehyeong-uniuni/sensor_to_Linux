#ifndef SENSOR_CORE_HEALTH_H
#define SENSOR_CORE_HEALTH_H

#include <stdbool.h>

#include "savvy/core/lifecycle.h"
#include "sensor_lifecycle.h"

#ifdef __cplusplus
extern "C" {
#endif

/* SNC-04's "health interface": a minimal typed read-only status snapshot
 * combining the daemon lifecycle state with a caller-supplied guard flag.
 * update_guard owns the guard's true/false state itself (Stage A: no
 * cross-feature linkage) - a future integration session reads
 * sensor_update_guard_is_tripped() and passes it in here alongside the
 * lifecycle registrar it already holds. This module invents no new
 * command, timeout, or retry policy - it is a pure read/combine. */
typedef struct sensor_health_status {
    savvy_lifecycle_state_t lifecycle_state;
    bool update_guard_tripped;
} sensor_health_status_t;

sensor_health_status_t sensor_health_snapshot(sensor_lifecycle_t *lc, bool update_guard_tripped);

#ifdef __cplusplus
}
#endif

#endif
