#ifndef SAVVY_PROTOCOL_DATA_RESULT_CODEC_H
#define SAVVY_PROTOCOL_DATA_RESULT_CODEC_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "savvy/core/error.h"

#ifdef __cplusplus
extern "C" {
#endif

/* DataResult (contracts/json_field_policy.md §4) - Sensor-side only;
 * decoded from a packet's Data field on the Stream/Voice TCP 8141
 * channel (FND-01), not part of the MGR-Sensor IPC envelope. */
typedef struct savvy_data_result {
    int32_t result;
} savvy_data_result_t;

/* Parses `json` (`len` bytes, NUL-terminated at json[len]) as
 * {"result": <int>}.
 *
 * DEC-20260715-DATARESULT-GSON-PARITY: unlike config_codec/device_codec
 * (and unlike this codec's own pre-2026-07-15 policy), this is a
 * DataResult-ONLY empirical parity contract with real Gson 2.8.2
 * (verified by executing the actual gson-2.8.2.jar outside this repo -
 * see contracts/json_field_policy.md §4 for the full matrix), not the
 * general "reject anything uncertain" schema policy:
 *
 *   - Missing "result" key -> SAVVY_OK, result=0 (Gson's unsafe/no-
 *     constructor allocation for a class with no no-arg constructor
 *     bypasses the `= 4` field initializer - empirically confirmed, not
 *     merely theorized as before).
 *   - JSON null -> SAVVY_OK, result=0 (same mechanism, empirically
 *     confirmed).
 *   - JSON integer, in [INT32_MIN, INT32_MAX] -> SAVVY_OK, that value.
 *   - JSON integer, fractional (e.g. 4.9), non-finite, or outside int32
 *     range -> SAVVY_ERR_PROTOCOL (never silently truncated/clamped).
 *   - JSON string in integer format (optional leading '-', then one or
 *     more ASCII digits only) whose value fits int32 -> SAVVY_OK, parsed
 *     as that int (Gson coerces a quoted numeral through the same
 *     numeric-token path as an unquoted number - empirically confirmed
 *     for "-1", "4", "2147483647"). Out of int32 range (e.g.
 *     "2147483648") -> SAVVY_ERR_PROTOCOL, empirically confirmed. A
 *     fractional-format string (e.g. "4.9") or non-numeric string (e.g.
 *     "abc") -> SAVVY_ERR_PROTOCOL, empirically confirmed. Any other
 *     lexical form not covered above (leading '+', whitespace, exponent
 *     notation, ...) was not exercised against the real JAR and is
 *     therefore also SAVVY_ERR_PROTOCOL - this codec does not extend the
 *     accepted grammar beyond what was actually observed.
 *   - JSON bool/array/object for "result" -> SAVVY_ERR_PROTOCOL (Gson
 *     throws for a type-incompatible target field; not empirically
 *     re-verified since no coercion path exists for these types).
 *   - Duplicate "result" key -> SAVVY_OK, LAST occurrence wins
 *     (empirically confirmed: {"result":4,"result":7} -> 7). This is the
 *     one schema-managed object in this codebase where duplicate keys are
 *     NOT rejected - see savvy_json_parse_allow_duplicate_keys().
 *   - Invalid UTF-8 anywhere in the JSON text -> SAVVY_ERR_PROTOCOL
 *     (unchanged - orthogonal to the Gson-parity exception above).
 *   - Non-object root -> SAVVY_ERR_PROTOCOL (unchanged).
 *
 * This exception is scoped to DataResult ONLY: Config, Device, the IPC
 * envelope root/payload, jsonConfigDto, and jsonDeviceDto all keep their
 * existing strict policy (duplicate-key reject, required-null reject)
 * completely unchanged, and savvy_json_parse() (the shared/common parser
 * every other codec uses) is itself untouched by this exception. */
savvy_status_t savvy_data_result_parse(const char *json, size_t len, savvy_data_result_t *out);

/* Builds {"result": dr->result} into a newly malloc'd, NUL-terminated
 * JSON string (*out_json; caller frees with free()). */
savvy_status_t savvy_data_result_build(const savvy_data_result_t *dr, char **out_json);

/* result == 4 is normal; any other integer value (5, 6, 7, 8, 99, ...)
 * is equally non-normal/danger - K-001, current-code semantics,
 * independent of any protocol document's wording. */
bool savvy_data_result_is_normal(const savvy_data_result_t *dr);

#ifdef __cplusplus
}
#endif

#endif
