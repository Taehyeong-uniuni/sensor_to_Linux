#ifndef SENSOR_STREAM_SESSION_H
#define SENSOR_STREAM_SESSION_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "savvy/core/error.h"
#include "sensor_stream/result_policy.h"

#ifdef __cplusplus
extern "C" {
#endif

/* SNS-01 (channel lifecycle) + SNS-02 (wire commands) combined: a
 * sensor_stream_session_t is one Stream ('S') or Voice ('V') logical
 * channel, owning its own sensor_tcp_channel_t (SNS-01 transport, TCP
 * 8141) and its own sensor_result_policy_t (SNS-03 - injected from the
 * result_policy feature, not owned/duplicated here). Two independent
 * instances of this type - one per role - are what the pinned Android
 * ClientChannel-pair contract maps onto; nothing here is shared between
 * a Stream session and a Voice session. */

typedef enum sensor_stream_role {
    SENSOR_STREAM_ROLE_STREAM, /* wire Start byte 'S' - ToF/frame data */
    SENSOR_STREAM_ROLE_VOICE   /* wire Start byte 'V' - audio data */
} sensor_stream_role_t;

typedef struct sensor_stream_session sensor_stream_session_t;

/* Stage A cross-session dependency ports: CC-SENSOR-CORE's public
 * Config/Device snapshot API does not exist yet on this branch (see
 * CROSS_SESSION_DEPENDENCY, session_results/wave1/CC-SENSOR-STREAM.md).
 * `server_ip`/`server_port`/`compress`/`danger_count_threshold` mirror
 * the pinned jsonConfigDto fields and are caller-injected at create time
 * rather than read from a shared config store; `device_serial` mirrors
 * the pinned jsonDeviceDto.deviceSerial, already normalized by the caller
 * to exactly SAVVY_PACKET_SERIAL_LEN (14) bytes. Production wiring to a
 * real Config/Device snapshot is deferred to integration/Stage B. */
typedef struct sensor_stream_config {
    const char *server_ip;
    uint16_t server_port;              /* 8141 in production */
    int compress;                      /* mirrors pinned jsonConfigDto.compress: 0=raw, 1=bzip2 */
    uint32_t danger_count_threshold;   /* mirrors pinned jsonConfigDto.dangerCount - Stream role only, ignored for Voice */
    const uint8_t *device_serial;      /* exactly SAVVY_PACKET_SERIAL_LEN bytes */
    size_t max_payload_size;           /* maximum raw input accepted by send_data/relay before any packet header, Voice WAV header, or optional BZip expansion; the session derives a larger overflow-checked encoded packet/transport capacity internally */
} sensor_stream_config_t;

/* Invoked once per send call's outcome, from the channel's own worker
 * thread (same non-reentrancy constraints as sensor_tcp_on_complete_fn -
 * must be fast, must not call back into this session synchronously).
 * `ok` is true only for the expected, successful outcome for that
 * specific send kind (see each function's doc below for what "expected"
 * means); `response_command` is the response packet's Command byte when
 * a response was actually received (0 otherwise, e.g. on a timeout or a
 * fire-and-forget send). */
typedef void (*sensor_stream_on_sent_fn)(bool ok, uint8_t response_command, void *ctx);

savvy_status_t sensor_stream_session_create(sensor_stream_session_t **out_session,
                                             sensor_stream_role_t role,
                                             const sensor_stream_config_t *config,
                                             sensor_result_alert_fn on_alert, void *alert_ctx);

savvy_status_t sensor_stream_session_start(sensor_stream_session_t *session);

/* Sends PIRIN (S/I or V/I, empty body - matching the pinned
 * getMakeSendMsg(..., null) contract) and waits for the response. Per
 * the confirmed real-server behavior, the expected response is an ECHO
 * of Command='I' (NOT a Command='R' RESPONSE) with an empty body - `ok`
 * is true only for exactly that shape. On that expected echo, resets the
 * result policy's danger counter (pinned Android setInitSecInfo(),
 * triggered by receiving this same echo) BEFORE invoking on_sent. */
savvy_status_t sensor_stream_session_send_pirin(sensor_stream_session_t *session,
                                                 sensor_stream_on_sent_fn on_sent, void *ctx);

/* Sends the data payload and waits for the Command='R' RESPONSE,
 * forwarding its body to the result policy (dangerCount/Alert - SNS-03)
 * before invoking on_sent. `ok` is true only for a received, CRC-valid,
 * Command='R' response (a CRC-invalid or wrong-command response, or any
 * sensor_tcp_result_status_t error, is `ok=false` - the result policy is
 * still fed exactly per its documented no-op contract in either case, so
 * callers never need to call the result policy themselves).
 *   - Stream role: `payload`/`payload_len` are sent as-is (optionally
 *     BZip-compressed per config.compress) with Command=STREAM('S') or
 *     STREAM_BZIP('Z'); `mic_value` is ignored (pass 0).
 *   - Voice role: `payload`/`payload_len` (raw PCM) are first wrapped in
 *     a 44-byte WAV header (sensor_wav_wrap), then optionally
 *     BZip-compressed per config.compress, sent with Command=VOICE('V')
 *     or STREAM_BZIP('Z'); `mic_value` is packed into the packet's
 *     Device/Config bytes as the pinned big-endian-split 16-bit decibel
 *     value (device=mic_value>>8, config=mic_value&0xFF), matching both
 *     the pinned Android sender and the confirmed real-server decoder. */
savvy_status_t sensor_stream_session_send_data(sensor_stream_session_t *session,
                                                const uint8_t *payload, size_t payload_len,
                                                int16_t mic_value,
                                                sensor_stream_on_sent_fn on_sent, void *ctx);

/* Sends PIROUT (S/O or V/O, empty body) fire-and-forget and closes the
 * session immediately once the send completes - matching the pinned
 * Android cmdTimeOut=0 + immediate closeChannel() contract. Never waits
 * for or interprets a response (`response_command` is always 0 in the
 * on_sent callback); `ok` is true iff the send itself completed. */
savvy_status_t sensor_stream_session_send_pirout(sensor_stream_session_t *session,
                                                  sensor_stream_on_sent_fn on_sent, void *ctx);

/* Stream role only (returns SAVVY_ERR_INVALID_ARGUMENT for a Voice-role
 * session): relays a pre-formed RKNN result payload as Start='T'
 * (RKNN_RESULT), Command='S' (STREAMING) onto the EXISTING Stream
 * connection - no wait for a response (the real server never sends one
 * for this path), no retry, and critically no new connect: fails
 * explicitly with SAVVY_ERR_NOT_CONNECTED if there is no live Stream
 * session right now, exactly matching "Stream session·socket이 없으면
 * 명시적 실패. 새 connect 금지, retry 금지, response 대기 금지". */
savvy_status_t sensor_stream_session_relay_rknn_result(sensor_stream_session_t *session,
                                                        const uint8_t *payload, size_t payload_len);

bool sensor_stream_session_is_connected(const sensor_stream_session_t *session);

void sensor_stream_session_stop(sensor_stream_session_t *session);
void sensor_stream_session_destroy(sensor_stream_session_t *session);

#ifdef __cplusplus
}
#endif

#endif
