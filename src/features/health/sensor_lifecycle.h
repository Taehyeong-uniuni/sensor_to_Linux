#ifndef SENSOR_CORE_LIFECYCLE_H
#define SENSOR_CORE_LIFECYCLE_H

#include <pthread.h>
#include <stddef.h>

#include "savvy/core/error.h"
#include "savvy/core/lifecycle.h"

#ifdef __cplusplus
extern "C" {
#endif

/* SNC-01 daemon lifecycle: a boot-time module registry layered on top of
 * Foundation's savvy_lifecycle_t idempotent STOPPED/RUNNING primitive.
 *
 * Android evidence (pinned savvy_sensor@48e2d1442cd867cc60f8ff3186d53fce1
 * c08f308, MainActivity.java): onCreate() (lines 156-335) runs a fixed
 * sequence - cached Config/Device load (setJsonConfigDto/setJsonDeviceDto,
 * lines 214-215) strictly before any module init, then per-module setup,
 * then doBindIpcService() (MGR bind) last (line 320). onDestroy() (lines
 * 362-373) unregisters/unbinds but never blocks waiting for a module to
 * finish; onStop() (lines 382-394) hard-kills the process - there is no
 * "stopped but resumable" state to preserve, so this session models stop
 * as a single terminal notification fan-out, not a resumable pause.
 *
 * This module does NOT create or own any real Stream/Voice/RKNN/Mic/ToF
 * worker (those belong to CC-SENSOR-STREAM/INPUT/TOF) - it only provides
 * the typed hook shape (on_start/on_config_applied/on_shutdown) a future
 * integration session (CC-INTEGRATION, Stage B) registers those workers
 * against, matching the session_tasks/CC-SENSOR-CORE.md hint of
 * `lifecycle_hook_register(module_id, on_start, on_config_applied,
 * on_shutdown)`. Registration order is preserved as call order for all
 * three hook kinds, giving the integrator the same "config/device first,
 * MGR bind last" control Android hardcodes in onCreate(), without this
 * session hardcoding any particular module's identity.
 *
 * Shutdown fan-out is an explicit NONBLOCKING notification boundary: each
 * on_shutdown call is a single synchronous function call the callee must
 * return from promptly (e.g. flip a flag, signal a condvar) - this module
 * adds no timeout and does not join/wait on any other session's worker
 * threads, matching the instruction to never wait indefinitely on another
 * session's real module teardown. */

#define SENSOR_LIFECYCLE_MAX_MODULES 16

typedef void (*sensor_lifecycle_on_start_fn)(void *user_data);
typedef void (*sensor_lifecycle_on_config_applied_fn)(void *user_data);
typedef void (*sensor_lifecycle_on_shutdown_fn)(void *user_data);

typedef struct sensor_lifecycle_hooks {
    const char *module_id; /* borrowed, must outlive the registry */
    sensor_lifecycle_on_start_fn on_start;                   /* nullable */
    sensor_lifecycle_on_config_applied_fn on_config_applied; /* nullable */
    sensor_lifecycle_on_shutdown_fn on_shutdown;              /* nullable */
    void *user_data;
} sensor_lifecycle_hooks_t;

typedef struct sensor_lifecycle {
    savvy_lifecycle_t base;
    pthread_mutex_t registry_lock;
    sensor_lifecycle_hooks_t modules[SENSOR_LIFECYCLE_MAX_MODULES];
    size_t module_count;
} sensor_lifecycle_t;

savvy_status_t sensor_lifecycle_init(sensor_lifecycle_t *lc);
void sensor_lifecycle_destroy(sensor_lifecycle_t *lc);

/* Appends one module's hook set to the registry (registration order =
 * notification order for every hook kind). Meant to be called only while
 * STOPPED, before sensor_lifecycle_start(); this is a static boot-time
 * table, not a dynamic add/remove registry. Returns
 * SAVVY_ERR_OVERFLOW past SENSOR_LIFECYCLE_MAX_MODULES,
 * SAVVY_ERR_INVALID_ARGUMENT for a NULL lc/hooks/module_id. */
savvy_status_t sensor_lifecycle_register_module(sensor_lifecycle_t *lc,
                                                 const sensor_lifecycle_hooks_t *hooks);

/* Idempotent start (wraps savvy_lifecycle_start): on the real
 * STOPPED->RUNNING transition only, calls every registered module's
 * on_start in registration order. A redundant call while already RUNNING
 * fires no hooks and returns SAVVY_OK. */
savvy_status_t sensor_lifecycle_start(sensor_lifecycle_t *lc);

/* Notifies every registered module's on_config_applied, in registration
 * order, regardless of start/stop state. Does not itself parse or store
 * Config/Device - src/features/config owns that; this is purely the
 * fan-out boundary a future integration session drives after a config
 * feature's load_cached()/apply_runtime() call succeeds. */
void sensor_lifecycle_notify_config_applied(sensor_lifecycle_t *lc);

/* Idempotent stop (wraps savvy_lifecycle_stop): on the real
 * RUNNING->STOPPED transition only, calls every registered module's
 * on_shutdown in registration order, then returns - never blocks past
 * that. A redundant call while already STOPPED fires no hooks and
 * returns SAVVY_OK. */
savvy_status_t sensor_lifecycle_stop(sensor_lifecycle_t *lc);

savvy_lifecycle_state_t sensor_lifecycle_get_state(sensor_lifecycle_t *lc);

#ifdef __cplusplus
}
#endif

#endif
