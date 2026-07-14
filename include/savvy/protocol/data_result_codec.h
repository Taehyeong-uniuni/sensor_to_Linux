#ifndef SAVVY_PROTOCOL_DATA_RESULT_CODEC_H
#define SAVVY_PROTOCOL_DATA_RESULT_CODEC_H

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

/* Parses `json` (NUL-terminated) as {"result": <int>}.
 *
 * Unlike config_codec/device_codec, a MISSING "result" key is a parse
 * error here (SAVVY_ERR_PROTOCOL) - not defaulted to 4. This is a
 * deliberate exception to the general "missing key -> apply Android
 * default" policy: Android's DataResult has no no-arg constructor (only
 * DataResult(int)), so Gson must use unsafe/no-constructor allocation to
 * deserialize it, which bypasses the `= 4` field initializer - a real
 * `DataResult` missing its "result" key could plausibly parse to `0`
 * (danger) rather than `4` (normal) on the Android side. This is
 * safety-relevant (Alert suppression vs. false Alert) and unverified by
 * execution (see contracts/json_field_policy.md §4/§5), so this codec
 * rejects rather than guesses - matching CT-JSON-002's explicit spec.
 *
 * Also rejects: JSON null, non-integer type, duplicate key (both via
 * savvy_json_parse), and a non-object root. */
savvy_status_t savvy_data_result_parse(const char *json, savvy_data_result_t *out);

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
