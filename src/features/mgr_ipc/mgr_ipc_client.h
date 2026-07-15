#ifndef SENSOR_CORE_MGR_IPC_CLIENT_H
#define SENSOR_CORE_MGR_IPC_CLIENT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "savvy/core/error.h"
#include "savvy/platform/ipc_cancel.h"
#include "savvy/platform/ipc_transport.h"

#ifdef __cplusplus
extern "C" {
#endif

/* SNC-03: Sensor-side MGR IPC client (savvy_sensor@48e2d1442cd867cc60f8ff
 * 3186d53fce1c08f308's MainActivity.doBindIpcService()/IpcHandler/
 * sendIpcMessage(), lines 2339-2453, plus 08_BLOCKERS.md B-006/
 * DEC-20260714-02: MGR is the AF_UNIX SOCK_SEQPACKET server, Sensor is
 * the client).
 *
 * Foundation's savvy_ipc_client_connect_cancelable() only lives in
 * savvy_platform_ipc, which is gated behind SAVVY_IPC_REAL_TRANSPORT and
 * only builds on Linux (this repo's own CMakePresets.json sets it OFF for
 * host-mac - AF_UNIX SOCK_SEQPACKET is not available on Darwin). To keep
 * this client's connect/recv/reconnect/cancel-destroy state machine fully
 * portable and unit-testable on both macOS and Linux, "how a transport is
 * obtained" is injected as a connector function rather than hardcoded -
 * production integration code supplies sensor_mgr_ipc_real_connector()
 * (declared in real_connector.h, only built when this feature's own
 * SENSOR_MGR_IPC_REAL_TRANSPORT CMake option is ON), and tests supply a
 * fake one. This is a plain dependency-injection seam, not a production
 * mock: nothing here behaves differently based on who supplies the
 * connector, and no test-only code path exists inside this client. */

typedef savvy_status_t (*sensor_mgr_ipc_connector_fn)(void *connector_ctx,
                                                       uint32_t timeout_ms,
                                                       const savvy_ipc_cancel_source_t *cancel,
                                                       savvy_ipc_transport_t *out_transport);

/* Fired once per successfully parsed, catalog-known, MGR->Sensor envelope
 * (action/payload_json are only valid for the duration of this call -
 * copy anything you need to keep). Malformed/unknown/wrong-direction/
 * invalid-payload envelopes are dropped silently at this layer (S-003:
 * reject, never crash) and never reach this callback. */
typedef void (*sensor_mgr_ipc_on_envelope_fn)(const char *action, const char *payload_json, void *user_data);

/* Fired after a successful (re)connect and after CONNECT_BROADCAST_IPC
 * has already been sent. was_reconnect is false only for the very first
 * successful connect of this client's lifetime; true for every one after
 * that (savvy_ipc_reconnect_tracker_t's own contract) - CC-SENSOR-CORE
 * does not replay cached Config/Device to MGR on reconnect (that would
 * violate the Sensor-is-client contract), so was_reconnect exists purely
 * as an informational signal, e.g. for logging or a future integration's
 * own bookkeeping - it triggers no automatic resend of anything here. */
typedef void (*sensor_mgr_ipc_on_connected_fn)(bool was_reconnect, void *user_data);

/* Fired once a disconnect (peer EOF or transport error) is detected,
 * before this client starts trying to reconnect. */
typedef void (*sensor_mgr_ipc_on_disconnected_fn)(void *user_data);

typedef struct sensor_mgr_ipc_callbacks {
    sensor_mgr_ipc_on_envelope_fn on_envelope;
    sensor_mgr_ipc_on_connected_fn on_connected;
    sensor_mgr_ipc_on_disconnected_fn on_disconnected;
    void *user_data;
} sensor_mgr_ipc_callbacks_t;

typedef struct sensor_mgr_ipc_config {
    sensor_mgr_ipc_connector_fn connector; /* required, not owned */
    void *connector_ctx;                   /* passed through verbatim, not owned */
    uint32_t connect_timeout_ms;
    uint32_t send_timeout_ms;
    uint32_t recv_poll_timeout_ms;   /* bounded recv() slice - the only mechanism available to notice
                                       shutdown promptly while blocked receiving, since Foundation's
                                       transport recv() takes no cancel source, only a timeout */
    uint32_t reconnect_backoff_ms;
    sensor_mgr_ipc_callbacks_t callbacks;
} sensor_mgr_ipc_config_t;

typedef struct sensor_mgr_ipc_client sensor_mgr_ipc_client_t; /* opaque */

savvy_status_t sensor_mgr_ipc_client_create(sensor_mgr_ipc_client_t **out_client,
                                             const sensor_mgr_ipc_config_t *config);

/* Idempotent: starts the background connect/recv worker thread. A call
   while already started is a safe no-op. Safe to call again after
   stop() to restart the same client (a fresh cancel source is created
   each time). */
savvy_status_t sensor_mgr_ipc_client_start(sensor_mgr_ipc_client_t *client);

/* Pre-connect drop: if not currently connected, returns
   SAVVY_ERR_NOT_CONNECTED WITHOUT ever calling the transport's send() -
   no queueing, no retry, no durable store. `action` must be a
   Foundation-catalog-known Sensor->MGR action (SAVVY_ERR_INVALID_ARGUMENT
   otherwise); `payload_json` may be NULL (treated as "{}"). Rejects
   (SAVVY_ERR_OVERFLOW, before ever touching the transport) any payload
   whose built envelope would exceed SAVVY_IPC_MAX_MESSAGE - mirrors
   savvy_ipc_envelope_build()'s own contract. This one function is the
   complete State/Property/Alert/Upload/Threshold-result "send interface"
   - each report type is just a (known action, JSON payload) pair; there
   is no bespoke per-report-type function because Foundation's action
   catalog already fully describes and validates each shape. */
savvy_status_t sensor_mgr_ipc_client_send(sensor_mgr_ipc_client_t *client,
                                          const char *action, const char *payload_json);

bool sensor_mgr_ipc_client_is_connected(sensor_mgr_ipc_client_t *client);

/* Thread-safe and idempotent. Concurrent stop callers serialize one
 * cancellation and worker join. The worker closes transport and destroys
 * its cancel source before terminal completion is published. A callback
 * running on the worker may call stop() without self-joining; that worker
 * remains joinable and the next external stop/start/destroy completes its
 * terminal handoff before returning or restarting. */
savvy_status_t sensor_mgr_ipc_client_stop(sensor_mgr_ipc_client_t *client);

/* Thread-safe with stop() and itself. API calls pin a registered client
 * before dereferencing it; the owning destroy waits existing calls and the
 * worker before freeing resources. A destroy called from an IPC callback is
 * deferred to that worker's terminal path, so callback return never touches
 * freed storage. If an external stop already owns the join when callback
 * destroy races it, the void callback destroy is safely reduced to stop and
 * a later external destroy owns final storage release. Calls made after an
 * accepted destroy has claimed the client are safely rejected/no-op. */
void sensor_mgr_ipc_client_destroy(sensor_mgr_ipc_client_t *client);

/* Sensor->MGR action strings (contracts/ipc_action_catalog.md; MSG_CMD.
 * java constants of the same wire names) - named here so callers never
 * need a magic string literal. */
#define SENSOR_MGR_IPC_ACTION_CONNECT "com.uniuni.savvymgr.ipc.connect"
#define SENSOR_MGR_IPC_ACTION_GETSTATE "com.uniuni.savvymgr.getstate.sensor"
#define SENSOR_MGR_IPC_ACTION_ALERT "com.uniuni.savvymgr.alert.sensor"
#define SENSOR_MGR_IPC_ACTION_UPLOAD "com.uniuni.savvymgr.upload.sensor"
#define SENSOR_MGR_IPC_ACTION_PROPERTY "com.uniuni.savvymgr.tof.property"
#define SENSOR_MGR_IPC_ACTION_THRESHOLD_RESULT "com.uniuni.savvymgr.update.threash.rslt"

/* MGR->Sensor action strings relevant to this session's own scope (the
 * full catalog has more; these are the ones CC-SENSOR-CORE's own modules
 * react to - config/device apply and the update guard). */
#define SENSOR_MGR_IPC_ACTION_CONFIG "com.uniuni.savvysensor.config"
#define SENSOR_MGR_IPC_ACTION_DEVICE "com.uniuni.savvysensor.device"
#define SENSOR_MGR_IPC_ACTION_APK_UPDATE "com.uniuni.savvysensor.sensor.apkupdate"

#ifdef __cplusplus
}
#endif

#endif
