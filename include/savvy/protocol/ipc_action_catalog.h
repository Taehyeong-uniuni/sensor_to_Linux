#ifndef SAVVY_PROTOCOL_IPC_ACTION_CATALOG_H
#define SAVVY_PROTOCOL_IPC_ACTION_CATALOG_H

#include <stddef.h>
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

/* Validates `payload_json` (a JSON object's serialized text, e.g. from a
 * parsed savvy_ipc_envelope_t.payload_json) against the catalog entry for
 * `action`: every required key must be present, and every key that IS
 * present (required or optional) must have the catalog-declared JSON
 * type (object/string/number) - e.g. CONFIG's {"jsonConfigDto":"not-an-
 * object"} is rejected even though the key exists, because the catalog
 * declares jsonConfigDto as an object. A present key whose value is JSON
 * null is accepted only if the catalog marks that key nullable (e.g.
 * PROPERTY_BROADCAST_TOF's four optional fields); otherwise a present
 * null is rejected just like any other type mismatch - missing entirely
 * is a separate, independently-allowed case for a non-required key.
 * Returns SAVVY_ERR_PROTOCOL if `action` is unknown/excluded, if
 * `payload_json` fails to parse or is not an object, if a required key is
 * absent, or if any present key's type (or null-ability) mismatches. */
savvy_status_t savvy_ipc_action_validate_payload(const char *action, const char *payload_json);

#ifdef __cplusplus
}
#endif

#endif
