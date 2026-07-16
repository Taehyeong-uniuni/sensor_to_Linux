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

static bool find_unquoted_result_key(const char *body, size_t body_len,
                                     size_t *out_key_start, size_t *out_key_end)
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
    if (key_len > body_len - i || strncmp(body + i, KEY, key_len) != 0) {
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

    *out_key_start = key_start;
    *out_key_end = key_end;
    return true;
}

/* Foundation's JSON parser requires text[len] to be an accessible NUL
 * byte. Always make an owned parser input, including for already-valid
 * quoted JSON; the response body is a counted packet field and is not a
 * C string. When the one supported bareword key is present, reserve two
 * additional bytes for the quotes as well as the final terminator. */
static savvy_status_t make_parser_input(const char *body, size_t body_len,
                                         char **out_text, size_t *out_len)
{
    size_t key_start = 0;
    size_t key_end = 0;
    bool rewrite = find_unquoted_result_key(body, body_len, &key_start, &key_end);
    size_t extra = rewrite ? 2u : 0u;

    if (body_len > SIZE_MAX - extra - 1u) {
        return SAVVY_ERR_OVERFLOW;
    }
    size_t parser_len = body_len + extra;
    char *text = (char *)malloc(parser_len + 1u);
    if (text == NULL) {
        return SAVVY_ERR_OUT_OF_MEMORY;
    }

    if (rewrite) {
        /* Matched exactly `<ws>{<ws>result<ws>:` - rewrite to
         * `<ws>{<ws>"result"<ws>:`, copying everything else (including
         * the value and closing brace, whatever they are) untouched. */
        memcpy(text, body, key_start);
        text[key_start] = '"';
        memcpy(text + key_start + 1u, body + key_start, key_end - key_start);
        text[key_end + 1u] = '"';
        memcpy(text + key_end + 2u, body + key_end, body_len - key_end);
    } else {
        memcpy(text, body, body_len);
    }
    text[parser_len] = '\0';

    *out_text = text;
    *out_len = parser_len;
    return SAVVY_OK;
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

    char *parser_text = NULL;
    size_t parser_len = 0;
    savvy_status_t st = make_parser_input(body, body_len, &parser_text, &parser_len);
    if (st != SAVVY_OK) {
        return;
    }

    savvy_data_result_t dr;
    st = savvy_data_result_parse(parser_text, parser_len, &dr);
    free(parser_text);

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
