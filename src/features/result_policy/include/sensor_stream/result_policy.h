#ifndef SENSOR_STREAM_RESULT_POLICY_H
#define SENSOR_STREAM_RESULT_POLICY_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "savvy/core/error.h"

#ifdef __cplusplus
extern "C" {
#endif

/* SNS-03: wraps Foundation's savvy_data_result_parse()/is_normal() with
 * the Stream/Voice business policy pinned from savvy_sensor MainActivity.java:
 *   - Stream: pre-increment-then-threshold-compare danger counter
 *     (`if (++counter >= dangerCount)`), reset to 0 on any normal result.
 *   - Voice: no counter at all - alert fires immediately on the first
 *     non-normal result.
 *   - A DataResult parse failure (malformed JSON, or a CRC-failed
 *     response passed through as an empty body) is a silent no-op for
 *     both roles: no counter change, no alert. This mirrors the pinned
 *     Android contract exactly - a dropped Gson exception in
 *     PacketIfCommData's constructor leaves mDataResult null, and the
 *     caller's `if (mDataResult != null)` guard skips the entire
 *     counter/alert block. */

typedef enum sensor_result_channel_role {
    SENSOR_RESULT_ROLE_STREAM,
    SENSOR_RESULT_ROLE_VOICE
} sensor_result_channel_role_t;

/* Invoked when the Stream threshold is reached, or immediately for any
 * Voice non-normal result. `channel_start` is the wire Start byte of the
 * channel that raised it ('S' or 'V'), matching the pinned Android
 * callBroadcastAlert(DEF.IFCOMM_START) contract. Must be fast and
 * non-blocking - Stage A: caller-supplied function pointer (no shared
 * cross-session IPC/event-bus API exists yet; see
 * CROSS_SESSION_DEPENDENCY in session_results/wave1/CC-SENSOR-STREAM.md).
 * Invoked synchronously from within sensor_result_policy_on_response(). */
typedef void (*sensor_result_alert_fn)(uint8_t channel_start, void *ctx);

typedef struct sensor_result_policy sensor_result_policy_t;

/* `danger_count_threshold` mirrors the pinned Android jsonConfigDto.dangerCount
 * config field (upstream default 4) - Stage A: caller-injected at create
 * time, not read from a shared config store (no such Foundation API
 * exists yet). Ignored entirely for the Voice role (Voice never counts).
 * Returns SAVVY_ERR_INVALID_ARGUMENT if out_policy is NULL, or
 * danger_count_threshold == 0 for the Stream role (a zero threshold would
 * fire on the very first ++counter, which is never what the pinned
 * default-4 contract intends - callers must pass a real threshold).
 * Returns SAVVY_ERR_OUT_OF_MEMORY on allocation failure. */
savvy_status_t sensor_result_policy_create(sensor_result_policy_t **out_policy,
                                            sensor_result_channel_role_t role,
                                            uint32_t danger_count_threshold,
                                            sensor_result_alert_fn on_alert, void *alert_ctx);

/* Resets the Stream danger counter to 0 - call this when a fresh session
 * starts (the pinned Android setInitSecInfo() contract, triggered by
 * receiving the PIRIN echo response; see the stream feature's session
 * lifecycle). No-op for the Voice role. */
void sensor_result_policy_reset(sensor_result_policy_t *policy);

/* Feeds one response body (the raw bytes of a Command=RESPONSE packet's
 * Data field) into the policy.
 *   - `body`/`body_len` may be NULL/0 (e.g. the caller already determined
 *     the response failed CRC validation and is deliberately passing
 *     "nothing usable") or may be non-JSON garbage - either way this is a
 *     documented, silent no-op: no counter change, no alert call.
 *   - Internally normalizes ONE specific, narrowly-scoped real-server
 *     wire quirk before calling the unmodified savvy_data_result_parse():
 *     the actual production streaming_server_v2 emits an unquoted bareword
 *     key - `{result:4}` / `{result: 4}` - which Gson's default lenient
 *     JsonReader accepts but Foundation's strict-JSON cJSON-backed parser
 *     rejects (empirically confirmed by compiling and probing the real
 *     codec - see result_policy.c for the full evidence and exact
 *     matching rule). The normalization ONLY inserts quotes around an
 *     exact bareword `result` key immediately after `{` (optional
 *     whitespace) and immediately before `:` (optional whitespace) -
 *     every other input (already-quoted, a different key, genuinely
 *     malformed some other way) is passed through to
 *     savvy_data_result_parse() completely unchanged, so every case in
 *     contracts/json_field_policy.md §4's empirically-verified matrix
 *     keeps behaving exactly as it did before this normalization existed. */
void sensor_result_policy_on_response(sensor_result_policy_t *policy,
                                      const char *body, size_t body_len);

/* Read-only inspection (tests). */
uint32_t sensor_result_policy_danger_count(const sensor_result_policy_t *policy);
sensor_result_channel_role_t sensor_result_policy_role(const sensor_result_policy_t *policy);

void sensor_result_policy_destroy(sensor_result_policy_t *policy);

#ifdef __cplusplus
}
#endif

#endif
