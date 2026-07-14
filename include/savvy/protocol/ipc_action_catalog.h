#ifndef SAVVY_PROTOCOL_IPC_ACTION_CATALOG_H
#define SAVVY_PROTOCOL_IPC_ACTION_CATALOG_H

#include <stdbool.h>
#include "savvy/core/error.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Confirmed action catalog from contracts/ipc_action_catalog.md
 * (Android source research, both savvy_mgr and savvy_sensor). */
typedef enum savvy_ipc_direction {
    SAVVY_IPC_MGR_TO_SENSOR,
    SAVVY_IPC_SENSOR_TO_MGR
} savvy_ipc_direction_t;

/* True iff `action` is a known, cataloged action. Deliberately-excluded
 * dead actions (e.g. TEST_BROADCAST_1/2 - confirmed unreachable in
 * Android source, see contracts/ipc_action_catalog.md) return false. */
bool savvy_ipc_action_known(const char *action);

/* Confirmed direction for a known action. Behavior is undefined if
 * savvy_ipc_action_known(action) is false - check that first. */
savvy_ipc_direction_t savvy_ipc_action_direction(const char *action);

/* Validates that `payload_json` (a JSON object's serialized text, e.g.
 * from a parsed savvy_ipc_envelope_t.payload_json) contains every
 * payload key the catalog requires for `action`. Returns
 * SAVVY_ERR_PROTOCOL if `action` is unknown/excluded, if `payload_json`
 * fails to parse or is not an object, or if any required key is absent.
 * Per-key JSON *type* conformance is not checked here - this repo's
 * envelope nests payload as JSON objects rather than Android's
 * double-string-encoding (contracts/json_field_policy.md §0), so
 * per-field typing belongs to the specific DTO codec (config_codec.h
 * etc.) or to the Wave 1 session owning that action. */
savvy_status_t savvy_ipc_action_validate_payload(const char *action, const char *payload_json);

#ifdef __cplusplus
}
#endif

#endif
