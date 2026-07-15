#include "sensor_stream/session.h"

#include <stdlib.h>
#include <string.h>

#include "savvy/protocol/packet_codec.h"
#include "sensor_platform/tcp_channel.h"
#include "sensor_stream/wav.h"
#include "sensor_stream/bzip.h"

/* Pinned Android IFCOMM command bytes (DEF.java IFCOMM_COMMAND /
 * IFCOMM_START) - literal ASCII values, not re-derived from any enum
 * here, since this session owns no shared header with the Android app. */
#define WIRE_START_STREAM      ((uint8_t)'S')
#define WIRE_START_VOICE       ((uint8_t)'V')
#define WIRE_START_RKNN_RESULT ((uint8_t)'T')

#define WIRE_CMD_PIRIN       ((uint8_t)'I')
#define WIRE_CMD_STREAM      ((uint8_t)'S')
#define WIRE_CMD_STREAM_BZIP ((uint8_t)'Z')
#define WIRE_CMD_VOICE       ((uint8_t)'V')
#define WIRE_CMD_PIROUT      ((uint8_t)'O')
#define WIRE_CMD_RESPONSE    ((uint8_t)'R')

/* Max encoded packet the transport ever has to hold: SAVVY_PACKET_HEADER_LEN
 * plus the largest possible body. Chosen per-role by the caller via
 * sensor_stream_config_t.max_payload_size, which sizes the tcp_channel's
 * reassembly buffer for INBOUND responses too - responses are always tiny
 * ({result:N} bodies), so this is dominated by our own largest OUTGOING
 * payload, but the transport buffer is shared for both directions. */

struct sensor_stream_session {
    sensor_stream_role_t role;
    sensor_tcp_channel_t *channel;
    sensor_result_policy_t *policy; /* owned by this session (created here, destroyed here) */
    int compress;
    uint8_t device_serial[SAVVY_PACKET_SERIAL_LEN];
    uint8_t *encode_buf; /* scratch buffer for outgoing packet encoding, sized to max_payload_size + header */
    size_t encode_buf_cap;
};

typedef struct pending_send {
    sensor_stream_session_t *session;
    sensor_stream_on_sent_fn on_sent;
    void *ctx;
} pending_send_t;

static uint8_t session_start_byte(const sensor_stream_session_t *s)
{
    return s->role == SENSOR_STREAM_ROLE_STREAM ? WIRE_START_STREAM : WIRE_START_VOICE;
}

static void on_pirin_complete(const sensor_tcp_result_t *result, void *ctx_v)
{
    pending_send_t *pend = (pending_send_t *)ctx_v;
    bool ok = (result->status == SENSOR_TCP_OK) && (result->command == WIRE_CMD_PIRIN);
    if (ok) {
        /* Pinned Android setInitSecInfo(), triggered by receiving this
         * same PIRIN echo - resets the danger counter for a fresh session. */
        sensor_result_policy_reset(pend->session->policy);
    }
    if (pend->on_sent != NULL) {
        pend->on_sent(ok, result->status == SENSOR_TCP_OK ? result->command : 0, pend->ctx);
    }
    free(pend);
}

savvy_status_t sensor_stream_session_send_pirin(sensor_stream_session_t *session,
                                                 sensor_stream_on_sent_fn on_sent, void *ctx)
{
    if (session == NULL) {
        return SAVVY_ERR_INVALID_ARGUMENT;
    }
    size_t written = 0;
    savvy_status_t st = savvy_packet_encode(session_start_byte(session), WIRE_CMD_PIRIN, 0, 0,
                                             session->device_serial, sizeof(session->device_serial),
                                             NULL, 0,
                                             session->encode_buf, session->encode_buf_cap, &written);
    if (st != SAVVY_OK) {
        return st;
    }

    pending_send_t *pend = (pending_send_t *)malloc(sizeof(*pend));
    if (pend == NULL) {
        return SAVVY_ERR_OUT_OF_MEMORY;
    }
    pend->session = session;
    pend->on_sent = on_sent;
    pend->ctx = ctx;

    st = sensor_tcp_channel_submit(session->channel, session->encode_buf, written,
                                   SENSOR_TCP_RESPONSE_TIMEOUT_MS, on_pirin_complete, pend);
    if (st != SAVVY_OK) {
        free(pend);
    }
    return st;
}

static void on_data_complete(const sensor_tcp_result_t *result, void *ctx_v)
{
    pending_send_t *pend = (pending_send_t *)ctx_v;
    bool ok = (result->status == SENSOR_TCP_OK) && (result->command == WIRE_CMD_RESPONSE) && result->crc_valid;

    if (result->status == SENSOR_TCP_OK && result->command == WIRE_CMD_RESPONSE) {
        /* Feed the result policy exactly once per RESPONSE received,
         * regardless of CRC outcome - an invalid CRC is passed through as
         * "no usable body" (NULL/0), which is a documented no-op there,
         * identical to a JSON parse failure (pinned Android contract). */
        if (result->crc_valid) {
            sensor_result_policy_on_response(pend->session->policy, (const char *)result->data, result->data_len);
        } else {
            sensor_result_policy_on_response(pend->session->policy, NULL, 0);
        }
    }

    if (pend->on_sent != NULL) {
        pend->on_sent(ok, result->status == SENSOR_TCP_OK ? result->command : 0, pend->ctx);
    }
    free(pend);
}

savvy_status_t sensor_stream_session_send_data(sensor_stream_session_t *session,
                                                const uint8_t *payload, size_t payload_len,
                                                int16_t mic_value,
                                                sensor_stream_on_sent_fn on_sent, void *ctx)
{
    if (session == NULL || (payload == NULL && payload_len > 0)) {
        return SAVVY_ERR_INVALID_ARGUMENT;
    }

    uint8_t device = 0, config = 0;
    uint8_t *wire_body = NULL;      /* heap buffer we may allocate below; must be freed before returning */
    size_t wire_body_len = 0;
    const uint8_t *final_body = payload;
    size_t final_body_len = payload_len;
    savvy_status_t st = SAVVY_OK;

    if (session->role == SENSOR_STREAM_ROLE_VOICE) {
        uint16_t uv = (uint16_t)mic_value;
        device = (uint8_t)(uv >> 8);
        config = (uint8_t)(uv & 0xFFu);

        uint8_t *wav_buf = NULL;
        size_t wav_len = 0;
        st = sensor_wav_wrap(payload, payload_len, &wav_buf, &wav_len);
        if (st != SAVVY_OK) {
            return st;
        }
        wire_body = wav_buf;
        wire_body_len = wav_len;
        final_body = wire_body;
        final_body_len = wire_body_len;
    }

    uint8_t command = (session->role == SENSOR_STREAM_ROLE_STREAM) ? WIRE_CMD_STREAM : WIRE_CMD_VOICE;
    if (session->compress == 1) {
        uint8_t *compressed = NULL;
        size_t compressed_len = 0;
        st = sensor_bzip_compress(final_body, final_body_len, &compressed, &compressed_len);
        free(wire_body); /* the pre-compression WAV buffer (if any) is no longer needed either way */
        wire_body = NULL;
        if (st != SAVVY_OK) {
            return st;
        }
        wire_body = compressed;
        wire_body_len = compressed_len;
        final_body = wire_body;
        final_body_len = wire_body_len;
        command = WIRE_CMD_STREAM_BZIP;
    }

    size_t written = 0;
    st = savvy_packet_encode(session_start_byte(session), command, device, config,
                              session->device_serial, sizeof(session->device_serial),
                              final_body, final_body_len,
                              session->encode_buf, session->encode_buf_cap, &written);
    free(wire_body);
    if (st != SAVVY_OK) {
        return st;
    }

    pending_send_t *pend = (pending_send_t *)malloc(sizeof(*pend));
    if (pend == NULL) {
        return SAVVY_ERR_OUT_OF_MEMORY;
    }
    pend->session = session;
    pend->on_sent = on_sent;
    pend->ctx = ctx;

    st = sensor_tcp_channel_submit(session->channel, session->encode_buf, written,
                                   SENSOR_TCP_RESPONSE_TIMEOUT_MS, on_data_complete, pend);
    if (st != SAVVY_OK) {
        free(pend);
    }
    return st;
}

static void on_pirout_complete(const sensor_tcp_result_t *result, void *ctx_v)
{
    pending_send_t *pend = (pending_send_t *)ctx_v;
    bool ok = (result->status == SENSOR_TCP_SENT_NO_WAIT);
    if (pend->on_sent != NULL) {
        pend->on_sent(ok, 0, pend->ctx);
    }
    free(pend);
}

savvy_status_t sensor_stream_session_send_pirout(sensor_stream_session_t *session,
                                                  sensor_stream_on_sent_fn on_sent, void *ctx)
{
    if (session == NULL) {
        return SAVVY_ERR_INVALID_ARGUMENT;
    }
    size_t written = 0;
    savvy_status_t st = savvy_packet_encode(session_start_byte(session), WIRE_CMD_PIROUT, 0, 0,
                                             session->device_serial, sizeof(session->device_serial),
                                             NULL, 0,
                                             session->encode_buf, session->encode_buf_cap, &written);
    if (st != SAVVY_OK) {
        return st;
    }

    pending_send_t *pend = (pending_send_t *)malloc(sizeof(*pend));
    if (pend == NULL) {
        return SAVVY_ERR_OUT_OF_MEMORY;
    }
    pend->session = session;
    pend->on_sent = on_sent;
    pend->ctx = ctx;

    st = sensor_tcp_channel_submit_final(session->channel, session->encode_buf, written, on_pirout_complete, pend);
    if (st != SAVVY_OK) {
        free(pend);
    }
    return st;
}

/* try_relay() requires a non-NULL on_complete; the relay's caller has no
 * way to observe the outcome (sensor_stream_session_relay_rknn_result()'s
 * only observable result is its own synchronous return status - whether
 * it was accepted onto the queue at all, e.g. SAVVY_ERR_NOT_CONNECTED),
 * so the eventual async completion (send succeeded / failed once
 * actually attempted) is intentionally discarded here. */
static void relay_complete_noop(const sensor_tcp_result_t *result, void *ctx_v)
{
    (void)result;
    (void)ctx_v;
}

savvy_status_t sensor_stream_session_relay_rknn_result(sensor_stream_session_t *session,
                                                        const uint8_t *payload, size_t payload_len)
{
    if (session == NULL || session->role != SENSOR_STREAM_ROLE_STREAM) {
        return SAVVY_ERR_INVALID_ARGUMENT;
    }
    size_t written = 0;
    savvy_status_t st = savvy_packet_encode(WIRE_START_RKNN_RESULT, WIRE_CMD_STREAM, 0, 0,
                                             session->device_serial, sizeof(session->device_serial),
                                             payload, payload_len,
                                             session->encode_buf, session->encode_buf_cap, &written);
    if (st != SAVVY_OK) {
        return st;
    }
    return sensor_tcp_channel_try_relay(session->channel, session->encode_buf, written, relay_complete_noop, NULL);
}

savvy_status_t sensor_stream_session_create(sensor_stream_session_t **out_session,
                                             sensor_stream_role_t role,
                                             const sensor_stream_config_t *config,
                                             sensor_result_alert_fn on_alert, void *alert_ctx)
{
    if (out_session == NULL) {
        return SAVVY_ERR_INVALID_ARGUMENT;
    }
    *out_session = NULL;
    if (config == NULL || config->server_ip == NULL || config->device_serial == NULL ||
        config->max_payload_size < SAVVY_PACKET_HEADER_LEN) {
        return SAVVY_ERR_INVALID_ARGUMENT;
    }

    sensor_stream_session_t *s = (sensor_stream_session_t *)calloc(1, sizeof(*s));
    if (s == NULL) {
        return SAVVY_ERR_OUT_OF_MEMORY;
    }
    s->role = role;
    s->compress = config->compress;
    memcpy(s->device_serial, config->device_serial, sizeof(s->device_serial));

    s->encode_buf_cap = config->max_payload_size;
    s->encode_buf = (uint8_t *)malloc(s->encode_buf_cap);
    if (s->encode_buf == NULL) {
        free(s);
        return SAVVY_ERR_OUT_OF_MEMORY;
    }

    savvy_status_t st = sensor_tcp_channel_create(&s->channel, config->server_ip, config->server_port,
                                                   config->max_payload_size, 4 /* bounded queue depth */);
    if (st != SAVVY_OK) {
        free(s->encode_buf);
        free(s);
        return st;
    }

    sensor_result_channel_role_t policy_role = (role == SENSOR_STREAM_ROLE_STREAM)
        ? SENSOR_RESULT_ROLE_STREAM : SENSOR_RESULT_ROLE_VOICE;
    st = sensor_result_policy_create(&s->policy, policy_role, config->danger_count_threshold, on_alert, alert_ctx);
    if (st != SAVVY_OK) {
        sensor_tcp_channel_destroy(s->channel);
        free(s->encode_buf);
        free(s);
        return st;
    }

    *out_session = s;
    return SAVVY_OK;
}

savvy_status_t sensor_stream_session_start(sensor_stream_session_t *session)
{
    if (session == NULL) {
        return SAVVY_ERR_INVALID_ARGUMENT;
    }
    return sensor_tcp_channel_start(session->channel);
}

bool sensor_stream_session_is_connected(const sensor_stream_session_t *session)
{
    if (session == NULL) {
        return false;
    }
    return sensor_tcp_channel_is_connected(session->channel);
}

void sensor_stream_session_stop(sensor_stream_session_t *session)
{
    if (session == NULL) {
        return;
    }
    sensor_tcp_channel_stop(session->channel);
}

void sensor_stream_session_destroy(sensor_stream_session_t *session)
{
    if (session == NULL) {
        return;
    }
    sensor_tcp_channel_destroy(session->channel);
    sensor_result_policy_destroy(session->policy);
    free(session->encode_buf);
    free(session);
}
