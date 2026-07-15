#include "sensor_stream/result_policy.h"

#include <stdlib.h>
#include <string.h>

#include "savvy/protocol/data_result_codec.h"

struct sensor_result_policy {
    sensor_result_channel_role_t role;
    uint32_t danger_count_threshold;
    uint32_t danger_count;
    sensor_result_alert_fn on_alert;
    void *alert_ctx;
};

/*
 * DEC-20260715-SENSOR-STREAM-DATARESULT-WIRE-NORMALIZE
 *
 * Evidence (empirically probed, not theorized): the real, pinned
 * streaming_server_v2 (commit 39a6f49343e38ff8b62bb3d1ab7233065d593d4a)
 * builds its DataResult response bodies as raw Java string concatenation
 * with NO quotes around the key -
 *   DeviceHandler.java: ("{result:" + code + "}").getBytes()
 *   DeviceHandler.java: byte[] VOICE_RESULT_NORMAL = "{result:4}".getBytes();
 *   DeviceHandler.java: byte[] VOICE_RESULT_ALERT  = "{result:7}".getBytes();
 * (one fallback path even emits "{result: " + code + "}" - a space after
 * the colon). None of these are valid strict JSON (RFC 8259 requires a
 * quoted key). The pinned Android client tolerates this because Gson's
 * default JsonReader is lenient (accepts unquoted member names); this
 * repo's Foundation codec is built on cJSON, which is strict.
 *
 * Compiling and running savvy_data_result_parse() directly against both
 * forms confirms the gap:
 *   savvy_data_result_parse("{\"result\":4}", ...)  -> SAVVY_OK, result=4
 *   savvy_data_result_parse("{result:4}", ...)      -> SAVVY_ERR_PROTOCOL
 *   savvy_data_result_parse("{result: 4}", ...)     -> SAVVY_ERR_PROTOCOL
 * Left unaddressed, EVERY response from the real production server would
 * fail to parse, silently disabling Stream/Voice danger detection
 * entirely against real traffic (contracts/json_field_policy.md §4's
 * empirical Gson-parity matrix was tested only against quoted-key JSON -
 * this is a real, previously-undocumented wire-compatibility gap, not a
 * bug in that matrix).
 *
 * Foundation's data_result_codec.c/json_codec.c are frozen/forbidden
 * paths for this session (under src/protocol/) and are not modified here.
 * Per explicit user decision (2026-07-15, this session), this is bridged
 * with a narrow, mechanical, byte-level normalization confined entirely
 * to this session's own result_policy feature: insert quotes around an
 * EXACT bareword `result` key - and nothing else about the input - before
 * handing off to the unmodified savvy_data_result_parse(). This is not
 * "parsing DataResult ourselves": no value is inspected, coerced, or
 * decided here; every semantic outcome (missing key, null, int32 range,
 * numeric-string coercion, duplicate-key last-wins, non-object root,
 * UTF-8 validation, ...) is still decided entirely by Foundation's own
 * codec, unchanged. See sensor_result_policy_on_response()'s header
 * comment for the exact, narrow matching rule.
 */
static bool is_ws(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

static bool normalize_unquoted_result_key(const char *body, size_t body_len,
                                           char **out_rewritten, size_t *out_rewritten_len)
{
    size_t i = 0;
    while (i < body_len && is_ws(body[i])) {
        i++;
    }
    if (i >= body_len || body[i] != '{') {
        return false;
    }
    i++;
    while (i < body_len && is_ws(body[i])) {
        i++;
    }

    static const char KEY[] = "result";
    const size_t key_len = sizeof(KEY) - 1;
    if (i + key_len > body_len || strncmp(body + i, KEY, key_len) != 0) {
        return false;
    }
    size_t key_start = i;
    size_t key_end = i + key_len;
    i = key_end;
    while (i < body_len && is_ws(body[i])) {
        i++;
    }
    if (i >= body_len || body[i] != ':') {
        return false;
    }

    /* Matched exactly `<ws>{<ws>result<ws>:` - rewrite to
     * `<ws>{<ws>"result"<ws>:`, copying everything else (including the
     * value and closing brace, whatever they are) through untouched. */
    if (body_len > SIZE_MAX - 2) {
        return false; /* would overflow; let the unmodified body pass through and fail parsing normally */
    }
    size_t new_len = body_len + 2;
    char *out = (char *)malloc(new_len);
    if (out == NULL) {
        return false; /* OOM: fall through to parsing the unmodified body (will correctly fail) rather than crash */
    }

    memcpy(out, body, key_start);
    out[key_start] = '"';
    memcpy(out + key_start + 1, body + key_start, key_len);
    out[key_start + 1 + key_len] = '"';
    memcpy(out + key_start + 1 + key_len + 1, body + key_end, body_len - key_end);

    *out_rewritten = out;
    *out_rewritten_len = new_len;
    return true;
}

savvy_status_t sensor_result_policy_create(sensor_result_policy_t **out_policy,
                                            sensor_result_channel_role_t role,
                                            uint32_t danger_count_threshold,
                                            sensor_result_alert_fn on_alert, void *alert_ctx)
{
    if (out_policy == NULL) {
        return SAVVY_ERR_INVALID_ARGUMENT;
    }
    *out_policy = NULL;
    if (role == SENSOR_RESULT_ROLE_STREAM && danger_count_threshold == 0) {
        return SAVVY_ERR_INVALID_ARGUMENT;
    }

    sensor_result_policy_t *p = (sensor_result_policy_t *)calloc(1, sizeof(*p));
    if (p == NULL) {
        return SAVVY_ERR_OUT_OF_MEMORY;
    }
    p->role = role;
    p->danger_count_threshold = danger_count_threshold;
    p->danger_count = 0;
    p->on_alert = on_alert;
    p->alert_ctx = alert_ctx;

    *out_policy = p;
    return SAVVY_OK;
}

void sensor_result_policy_reset(sensor_result_policy_t *policy)
{
    if (policy == NULL) {
        return;
    }
    policy->danger_count = 0;
}

void sensor_result_policy_on_response(sensor_result_policy_t *policy, const char *body, size_t body_len)
{
    if (policy == NULL) {
        return;
    }
    if (body == NULL || body_len == 0) {
        return; /* no usable body (e.g. CRC-failed response passed through as empty): documented no-op */
    }

    char *rewritten = NULL;
    size_t rewritten_len = 0;
    bool did_rewrite = normalize_unquoted_result_key(body, body_len, &rewritten, &rewritten_len);

    savvy_data_result_t dr;
    savvy_status_t st = did_rewrite
        ? savvy_data_result_parse(rewritten, rewritten_len, &dr)
        : savvy_data_result_parse(body, body_len, &dr);
    free(rewritten);

    if (st != SAVVY_OK) {
        return; /* parse failure: no-op, matching the pinned Android dropped-Gson-exception contract */
    }

    bool normal = savvy_data_result_is_normal(&dr);
    if (policy->role == SENSOR_RESULT_ROLE_STREAM) {
        if (normal) {
            policy->danger_count = 0;
        } else {
            policy->danger_count++;
            if (policy->danger_count >= policy->danger_count_threshold && policy->on_alert != NULL) {
                policy->on_alert((uint8_t)'S', policy->alert_ctx);
            }
        }
    } else {
        if (!normal && policy->on_alert != NULL) {
            policy->on_alert((uint8_t)'V', policy->alert_ctx);
        }
    }
}

uint32_t sensor_result_policy_danger_count(const sensor_result_policy_t *policy)
{
    return policy == NULL ? 0 : policy->danger_count;
}

sensor_result_channel_role_t sensor_result_policy_role(const sensor_result_policy_t *policy)
{
    return policy == NULL ? SENSOR_RESULT_ROLE_STREAM : policy->role;
}

void sensor_result_policy_destroy(sensor_result_policy_t *policy)
{
    free(policy);
}
