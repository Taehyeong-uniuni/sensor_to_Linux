#ifndef SENSOR_PLATFORM_TCP_CHANNEL_H
#define SENSOR_PLATFORM_TCP_CHANNEL_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "savvy/core/error.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Generic request/response channel over a single TCP 8141 socket, built on
 * savvy_core (lifecycle/queue/clock) and savvy_protocol (packet_codec/
 * stream_parser). One instance == one worker thread + one socket + one
 * bounded queue + one connection state machine (CC-SENSOR-STREAM SNS-01).
 * The Stream and Voice logical channels are two independent instances of
 * this same type - nothing here is Stream/Voice-specific.
 *
 * Timeouts are fixed, not configurable, matching the pinned Android
 * ClientChannel contract (savvy_sensor DEF.java): connect 1000ms,
 * response-wait 3000ms. */
#define SENSOR_TCP_CONNECT_TIMEOUT_MS   1000u
#define SENSOR_TCP_RESPONSE_TIMEOUT_MS  3000u

typedef enum sensor_tcp_result_status {
    SENSOR_TCP_OK = 0,              /* a full response packet was received */
    SENSOR_TCP_SENT_NO_WAIT,        /* fire-and-forget send completed; no response was awaited */
    SENSOR_TCP_ERR_CONNECT_TIMEOUT, /* lazy connect did not finish within SENSOR_TCP_CONNECT_TIMEOUT_MS */
    SENSOR_TCP_ERR_RESPONSE_TIMEOUT,/* no full response arrived within the request's timeout */
    SENSOR_TCP_ERR_DISCONNECTED,    /* peer closed (recv()==0) while sending or awaiting a response */
    SENSOR_TCP_ERR_IO,              /* a hard socket error (connect/send/recv) other than timeout/disconnect */
    SENSOR_TCP_ERR_PROTOCOL,        /* savvy_stream_parser reported an unrecoverable framing error */
    SENSOR_TCP_ERR_NOT_CONNECTED,   /* rejected before send: channel not started, or (try_relay only) no live connection */
    SENSOR_TCP_ERR_QUEUE_FULL,      /* rejected before send: bounded queue was full */
    SENSOR_TCP_ERR_SHUTTING_DOWN    /* rejected or abandoned: channel is stopping/stopped */
} sensor_tcp_result_status_t;

typedef struct sensor_tcp_result {
    sensor_tcp_result_status_t status;
    /* Fields below are valid only when status == SENSOR_TCP_OK. */
    uint8_t start;     /* response packet's Start byte */
    uint8_t command;   /* response packet's Command byte - e.g. distinguishes
                         * the server's bare 'I' (PIRIN) echo from an 'R'
                         * (RESPONSE) carrying a DataResult body; callers
                         * MUST branch on this, not assume RESPONSE. */
    /* CRC32 outcome for this response's body, matching the pinned Android
     * inbound policy: true when data_len==0 (nothing to check) or the
     * body's CRC32 matched; false on a CRC mismatch. A false value is a
     * SOFT failure only - it is NOT folded into `status` (the response
     * still counts as "received" for timeout purposes; contracts/
     * json_field_policy.md §5.3 confirms the pinned Android app swallows
     * a CRC mismatch silently rather than surfacing a NAK). Callers must
     * treat crc_valid==false the same as a DataResult parse failure: no
     * counter change, no Alert, connection stays open. */
    bool crc_valid;
    /* Borrowed pointer into the channel's own reassembly buffer, valid
     * only for the duration of the on_complete call - copy out anything
     * you need before returning. */
    const uint8_t *data;
    size_t         data_len;
} sensor_tcp_result_t;

/* Invoked exactly once per accepted submit()/submit_final() call (never
 * for a request rejected up front, e.g. SAVVY_ERR_OVERFLOW from the
 * queue), synchronously on the channel's own worker thread, after the
 * request's outcome is known. Must be fast and must not block, and must
 * not call back into submit()/submit_final()/try_relay()/stop()/destroy()
 * on THIS SAME channel from within the callback (mirrors savvy_queue's
 * item_destroy_fn / savvy_snapshot's free_fn no-reentry convention - the
 * worker thread is still "inside" channel-owned state when this runs).
 * `result->data` is a borrowed pointer valid only until this call returns.
 *
 * Exception: a request dropped by stop()/destroy() while still queued
 * (never started) does NOT get an on_complete call at all - there is
 * nothing to report and no thread left to report it on. */
typedef void (*sensor_tcp_on_complete_fn)(const sensor_tcp_result_t *result, void *ctx);

typedef struct sensor_tcp_channel sensor_tcp_channel_t;

/* Allocates and initializes a channel bound to host:port. Does not start
 * any thread or connect any socket - call sensor_tcp_channel_start() next.
 *   - `max_packet_size` bounds both the largest packet this channel will
 *     ever send and the savvy_stream_parser reassembly buffer for inbound
 *     responses. Must be >= SAVVY_PACKET_HEADER_LEN + the largest expected
 *     response body for this channel's traffic (Stream: ToF-frame-sized;
 *     Voice: mic-buffer-sized - see the stream feature's sizing).
 *   - `queue_capacity` is the bounded pending-request queue depth
 *     (required behaviors #3/#4/#18-20).
 * Returns SAVVY_ERR_INVALID_ARGUMENT if host is NULL/empty, port is 0,
 * max_packet_size < SAVVY_PACKET_HEADER_LEN, or queue_capacity == 0.
 * Returns SAVVY_ERR_OUT_OF_MEMORY on allocation failure. */
savvy_status_t sensor_tcp_channel_create(sensor_tcp_channel_t **out_channel,
                                          const char *host, uint16_t port,
                                          size_t max_packet_size,
                                          size_t queue_capacity);

/* Starts the worker thread. Does NOT connect the socket - the first
 * submitted request lazily connects (required behaviors #5/#6).
 * Idempotent: start() while already running returns SAVVY_OK as a no-op
 * (savvy_lifecycle semantics). Returns SAVVY_ERR_UNKNOWN if the worker
 * thread cannot be created. */
savvy_status_t sensor_tcp_channel_start(sensor_tcp_channel_t *channel);

/* Deep-copies `packet` (packet_len bytes) into an internally-owned buffer
 * and enqueues a normal request/response item: the worker lazily connects
 * if needed, sends the packet (handling partial writes), then waits up to
 * `response_timeout_ms` (must be > 0) for one full response packet before
 * invoking `on_complete`. The caller's `packet` buffer is never retained -
 * it may be freed/reused immediately after this call returns, regardless
 * of the returned status.
 *   - Returns SAVVY_OK once accepted onto the bounded queue (on_complete
 *     will be invoked exactly once, later, from the worker thread).
 *   - Returns SAVVY_ERR_OVERFLOW if the bounded queue is currently full:
 *     the internal copy is freed before returning, the existing queue
 *     contents are left untouched, and on_complete is never invoked for
 *     this rejected request (required behaviors #5/#6/#18-20).
 *   - Returns SAVVY_ERR_NOT_STARTED if the worker thread isn't running.
 *   - Returns SAVVY_ERR_INVALID_ARGUMENT if packet_len > max_packet_size,
 *     response_timeout_ms == 0, or on_complete == NULL. */
savvy_status_t sensor_tcp_channel_submit(sensor_tcp_channel_t *channel,
                                          const uint8_t *packet, size_t packet_len,
                                          uint32_t response_timeout_ms,
                                          sensor_tcp_on_complete_fn on_complete, void *ctx);

/* Same enqueue/ownership contract as submit(), but for a terminal,
 * fire-and-forget send (Stream S/O, Voice V/O): the worker lazily
 * connects if needed, sends the packet, does NOT wait for or read any
 * response, then immediately closes the session (as if
 * sensor_tcp_channel_close_session() had been called), and finally
 * invokes on_complete with SENSOR_TCP_SENT_NO_WAIT (or an error status if
 * the connect/send itself failed). Matches the pinned Android contract of
 * PIROUT being sent with cmdTimeOut=0 and the channel being torn down
 * right after the send completes, without any handshake. */
savvy_status_t sensor_tcp_channel_submit_final(sensor_tcp_channel_t *channel,
                                                const uint8_t *packet, size_t packet_len,
                                                sensor_tcp_on_complete_fn on_complete, void *ctx);

/* Non-blocking relay send for traffic that must never create a connection
 * or wait (the T/S RKNN-result relay: "Stream session·socket이 없으면
 * 명시적 실패. 새 connect 금지, retry 금지, response 대기 금지"). If the
 * channel does not currently have a live connection, returns
 * SAVVY_ERR_NOT_CONNECTED immediately without touching the queue and
 * without calling on_complete at all - this is a synchronous rejection,
 * not an async outcome. If a connection is live, behaves like submit()
 * except it never waits for a response: on_complete is invoked with
 * SENSOR_TCP_SENT_NO_WAIT once the send completes (or an SENSOR_TCP_ERR_*
 * status if the send itself fails), and the session is left open
 * (no auto-close, unlike submit_final()). */
savvy_status_t sensor_tcp_channel_try_relay(sensor_tcp_channel_t *channel,
                                             const uint8_t *packet, size_t packet_len,
                                             sensor_tcp_on_complete_fn on_complete, void *ctx);

/* Thread-safe query of the current connection state (backed by an atomic
 * flag maintained by the worker thread - safe to call from any thread). */
bool sensor_tcp_channel_is_connected(const sensor_tcp_channel_t *channel);

/* Closes the current socket (if any), without stopping the worker thread
 * or discarding queued requests - the next submitted request lazily
 * reconnects. Idempotent (a no-op if already disconnected). Must only be
 * called from the channel's own worker thread (i.e. from within an
 * on_complete callback) or after sensor_tcp_channel_stop() has returned;
 * calling it concurrently from another thread while the worker may be
 * using the socket is a data race and is not supported. submit_final()
 * already calls this internally - most callers never need to call it
 * directly. */
void sensor_tcp_channel_close_session(sensor_tcp_channel_t *channel);

/* Stops the worker thread - waking it promptly even if it is blocked
 * waiting on the queue or waiting for a response (required behavior #26,
 * bounded by an internal short polling slice) - closes the socket if
 * open, and discards any requests still sitting in the queue (their
 * internal copies freed via the queue's item_destroy_fn; on_complete is
 * NOT invoked for requests that never started). Idempotent: calling
 * stop() again after the worker has already stopped is a no-op. Safe to
 * call from any thread except the channel's own worker thread (do not
 * call this from within an on_complete callback). Blocks until the
 * worker thread has fully exited. */
void sensor_tcp_channel_stop(sensor_tcp_channel_t *channel);

/* Calls sensor_tcp_channel_stop() if not already stopped, then frees the
 * channel. `channel` must not be used after this call. Safe to call with
 * NULL (no-op). */
void sensor_tcp_channel_destroy(sensor_tcp_channel_t *channel);

#ifdef __cplusplus
}
#endif

#endif
