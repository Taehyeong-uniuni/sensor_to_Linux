#define _POSIX_C_SOURCE 200809L

#include "sensor_platform/tcp_channel.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <stdatomic.h>
#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

#include "savvy/core/clock.h"
#include "savvy/core/lifecycle.h"
#include "savvy/core/queue.h"
#include "savvy/protocol/packet_codec.h"
#include "savvy/protocol/stream_parser.h"

/* Bounds worst-case latency for stop() to interrupt a blocked connect/send/
 * recv wait - see required behavior #26. Not a wire-protocol constant. */
#define SENSOR_TCP_STOP_CHECK_SLICE_MS 100u
/* Per-recv() chunk size, matching the pinned Android SOCK_BUFF_SIZE
 * convention (DEF.java) - not a wire-protocol constant either. */
#define SENSOR_TCP_RECV_CHUNK 10240u

typedef enum sensor_tcp_item_kind {
    SENSOR_TCP_ITEM_NORMAL, /* submit(): wait for a response */
    SENSOR_TCP_ITEM_FINAL,  /* submit_final(): fire-and-forget, close after send */
    SENSOR_TCP_ITEM_RELAY   /* try_relay(): fire-and-forget, no close, only if already connected */
} sensor_tcp_item_kind_t;

typedef struct sensor_tcp_queue_item {
    uint8_t *packet;      /* heap-owned deep copy; freed by the worker after processing,
                            * or by queue_item_destroy() if dropped while still queued */
    size_t   packet_len;
    uint32_t response_timeout_ms;
    sensor_tcp_item_kind_t kind;
    sensor_tcp_on_complete_fn on_complete;
    void *ctx;
} sensor_tcp_queue_item_t;

struct sensor_tcp_channel {
    char *host;
    uint16_t port;
    size_t max_packet_size;

    savvy_lifecycle_t lifecycle;
    pthread_t worker_thread;

    savvy_queue_t queue;

    /* Worker-thread-owned; never touched from any other thread except via
     * the documented worker-thread-only close_session() contract. */
    int sockfd;
    uint8_t *parse_buf;
    savvy_stream_parser_t parser;

    /* Cross-thread signals. */
    atomic_bool connected;
    atomic_bool stop_requested;
};

static char *sensor_tcp_dup_str(const char *s)
{
    size_t n = strlen(s) + 1;
    char *out = (char *)malloc(n);
    if (out != NULL) {
        memcpy(out, s, n);
    }
    return out;
}

static void queue_item_destroy(void *item_v)
{
    sensor_tcp_queue_item_t *item = (sensor_tcp_queue_item_t *)item_v;
    free(item->packet);
    /* No on_complete call here: this only runs for items still buffered
     * when cancel()/destroy() fires (channel shutting down) - there is no
     * outcome to report and no guarantee the caller's ctx is still valid. */
}

static void close_session_internal(sensor_tcp_channel_t *ch)
{
    if (ch->sockfd >= 0) {
        close(ch->sockfd);
        ch->sockfd = -1;
    }
    atomic_store(&ch->connected, false);
}

/* Non-blocking connect with a 1000ms deadline, sliced so stop_requested is
 * checked at least every SENSOR_TCP_STOP_CHECK_SLICE_MS. On success, sets
 * ch->sockfd and (re)initializes the stream parser for the new connection. */
static sensor_tcp_result_status_t try_connect(sensor_tcp_channel_t *ch)
{
    struct addrinfo hints;
    struct addrinfo *res = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    char port_str[8];
    snprintf(port_str, sizeof(port_str), "%u", (unsigned)ch->port);

    if (getaddrinfo(ch->host, port_str, &hints, &res) != 0 || res == NULL) {
        return SENSOR_TCP_ERR_IO;
    }

    savvy_deadline_t dl;
    savvy_deadline_arm(&dl, SENSOR_TCP_CONNECT_TIMEOUT_MS);
    sensor_tcp_result_status_t outcome = SENSOR_TCP_ERR_IO;

    for (struct addrinfo *rp = res; rp != NULL; rp = rp->ai_next) {
        int fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd < 0) {
            continue;
        }

        int flags = fcntl(fd, F_GETFL, 0);
        if (flags >= 0) {
            fcntl(fd, F_SETFL, flags | O_NONBLOCK);
        }
#ifdef SO_NOSIGPIPE
        {
            int one = 1;
            setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
        }
#endif

        int rc = connect(fd, rp->ai_addr, rp->ai_addrlen);
        if (rc == 0) {
            ch->sockfd = fd;
            outcome = SENSOR_TCP_OK;
            break;
        }
        if (errno != EINPROGRESS) {
            close(fd);
            continue;
        }

        bool connected = false;
        bool aborted = false;
        for (;;) {
            if (atomic_load(&ch->stop_requested)) {
                aborted = true;
                break;
            }
            uint32_t remaining = savvy_deadline_remaining_ms(&dl);
            if (remaining == 0) {
                break; /* timed out */
            }
            uint32_t slice = remaining < SENSOR_TCP_STOP_CHECK_SLICE_MS ? remaining : SENSOR_TCP_STOP_CHECK_SLICE_MS;

            struct pollfd pfd;
            pfd.fd = fd;
            pfd.events = POLLOUT;
            pfd.revents = 0;
            int pr = poll(&pfd, 1, (int)slice);
            if (pr < 0) {
                if (errno == EINTR) {
                    continue;
                }
                break; /* hard I/O error */
            }
            if (pr == 0) {
                continue; /* slice elapsed, recheck deadline/stop */
            }

            int so_err = 0;
            socklen_t slen = sizeof(so_err);
            if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &so_err, &slen) != 0 || so_err != 0) {
                break; /* connect failed */
            }
            connected = true;
            break;
        }

        if (connected) {
            ch->sockfd = fd;
            outcome = SENSOR_TCP_OK;
            break;
        }
        close(fd);
        if (aborted) {
            outcome = SENSOR_TCP_ERR_SHUTTING_DOWN;
            break;
        }
        outcome = savvy_deadline_expired(&dl) ? SENSOR_TCP_ERR_CONNECT_TIMEOUT : SENSOR_TCP_ERR_IO;
        if (savvy_deadline_expired(&dl)) {
            break; /* shared deadline exhausted; don't try further addresses */
        }
    }

    freeaddrinfo(res);

    if (outcome == SENSOR_TCP_OK) {
        atomic_store(&ch->connected, true);
        savvy_stream_parser_init(&ch->parser, ch->parse_buf, ch->max_packet_size);
    }
    return outcome;
}

static sensor_tcp_result_status_t send_all(sensor_tcp_channel_t *ch, const uint8_t *buf, size_t len)
{
    size_t sent = 0;
    while (sent < len) {
        if (atomic_load(&ch->stop_requested)) {
            return SENSOR_TCP_ERR_SHUTTING_DOWN;
        }
        struct pollfd pfd;
        pfd.fd = ch->sockfd;
        pfd.events = POLLOUT;
        pfd.revents = 0;
        int pr = poll(&pfd, 1, (int)SENSOR_TCP_STOP_CHECK_SLICE_MS);
        if (pr < 0) {
            if (errno == EINTR) {
                continue;
            }
            return SENSOR_TCP_ERR_IO;
        }
        if (pr == 0) {
            continue;
        }
        if (pfd.revents & (POLLERR | POLLHUP)) {
            return SENSOR_TCP_ERR_DISCONNECTED;
        }

        ssize_t n = send(ch->sockfd, buf + sent, len - sent, 0);
        if (n < 0) {
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;
            }
            if (errno == EPIPE || errno == ECONNRESET) {
                return SENSOR_TCP_ERR_DISCONNECTED;
            }
            return SENSOR_TCP_ERR_IO;
        }
        sent += (size_t)n;
    }
    return SENSOR_TCP_OK;
}

/* Waits up to timeout_ms for one full response packet, handling partial
 * header/body across multiple recv() calls and coalesced packets left
 * over from a previous cycle (try_extract is always attempted first,
 * before touching the socket). Does not verify CRC - callers apply that
 * themselves via savvy_crc32(), per savvy_stream_parser's own contract. */
static sensor_tcp_result_status_t wait_for_response(sensor_tcp_channel_t *ch, uint32_t timeout_ms,
                                                     savvy_packet_header_t *out_hdr,
                                                     const uint8_t **out_data, size_t *out_len)
{
    savvy_stream_result_t r = savvy_stream_parser_try_extract(&ch->parser, out_hdr, out_data, out_len);
    if (r == SAVVY_STREAM_PACKET_READY) {
        return SENSOR_TCP_OK;
    }
    if (r == SAVVY_STREAM_ERROR) {
        return SENSOR_TCP_ERR_PROTOCOL;
    }

    savvy_deadline_t dl;
    savvy_deadline_arm(&dl, timeout_ms);
    uint8_t recvbuf[SENSOR_TCP_RECV_CHUNK];

    for (;;) {
        if (atomic_load(&ch->stop_requested)) {
            return SENSOR_TCP_ERR_SHUTTING_DOWN;
        }
        uint32_t remaining = savvy_deadline_remaining_ms(&dl);
        if (remaining == 0) {
            return SENSOR_TCP_ERR_RESPONSE_TIMEOUT;
        }
        uint32_t slice = remaining < SENSOR_TCP_STOP_CHECK_SLICE_MS ? remaining : SENSOR_TCP_STOP_CHECK_SLICE_MS;

        struct pollfd pfd;
        pfd.fd = ch->sockfd;
        pfd.events = POLLIN;
        pfd.revents = 0;
        int pr = poll(&pfd, 1, (int)slice);
        if (pr < 0) {
            if (errno == EINTR) {
                continue;
            }
            return SENSOR_TCP_ERR_IO;
        }
        if (pr == 0) {
            continue;
        }

        ssize_t n = recv(ch->sockfd, recvbuf, sizeof(recvbuf), 0);
        if (n < 0) {
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;
            }
            return SENSOR_TCP_ERR_IO;
        }
        if (n == 0) {
            return SENSOR_TCP_ERR_DISCONNECTED; /* peer closed - recv()==0 */
        }

        if (savvy_stream_parser_push(&ch->parser, recvbuf, (size_t)n) != SAVVY_OK) {
            return SENSOR_TCP_ERR_PROTOCOL; /* overflow even after internal compaction */
        }
        r = savvy_stream_parser_try_extract(&ch->parser, out_hdr, out_data, out_len);
        if (r == SAVVY_STREAM_PACKET_READY) {
            return SENSOR_TCP_OK;
        }
        if (r == SAVVY_STREAM_ERROR) {
            return SENSOR_TCP_ERR_PROTOCOL;
        }
        /* SAVVY_STREAM_NEED_MORE_DATA: loop and read more */
    }
}

static void process_item(sensor_tcp_channel_t *ch, sensor_tcp_queue_item_t *item)
{
    sensor_tcp_result_t result;
    memset(&result, 0, sizeof(result));

    if (item->kind == SENSOR_TCP_ITEM_RELAY && ch->sockfd < 0) {
        /* Re-checked here (not just at submit time) because the queue may
         * have drained the prior item's failure/close before this one was
         * popped - try_relay() must never trigger a connect, ever. */
        result.status = SENSOR_TCP_ERR_NOT_CONNECTED;
        item->on_complete(&result, item->ctx);
        return;
    }

    if (ch->sockfd < 0) {
        sensor_tcp_result_status_t cs = try_connect(ch);
        if (cs != SENSOR_TCP_OK) {
            result.status = cs;
            item->on_complete(&result, item->ctx);
            return;
        }
    }

    sensor_tcp_result_status_t ss = send_all(ch, item->packet, item->packet_len);
    if (ss != SENSOR_TCP_OK) {
        close_session_internal(ch); /* required behavior #20: timeout/disconnect/IO error is a channel failure */
        result.status = ss;
        item->on_complete(&result, item->ctx);
        return;
    }

    if (item->kind == SENSOR_TCP_ITEM_FINAL) {
        close_session_internal(ch);
        result.status = SENSOR_TCP_SENT_NO_WAIT;
        item->on_complete(&result, item->ctx);
        return;
    }
    if (item->kind == SENSOR_TCP_ITEM_RELAY) {
        result.status = SENSOR_TCP_SENT_NO_WAIT;
        item->on_complete(&result, item->ctx);
        return;
    }

    savvy_packet_header_t hdr;
    const uint8_t *data = NULL;
    size_t data_len = 0;
    sensor_tcp_result_status_t rs = wait_for_response(ch, item->response_timeout_ms, &hdr, &data, &data_len);
    if (rs != SENSOR_TCP_OK) {
        /* Closing here is what prevents a late/stale response for THIS
         * request from being misread as the response to the NEXT request
         * (required behavior #28) - the next submit() lazily reconnects
         * on a fresh socket instead of continuing to read a desynced one. */
        close_session_internal(ch);
        result.status = rs;
        item->on_complete(&result, item->ctx);
        return;
    }

    bool crc_valid = (data_len == 0) || (hdr.crc32 == savvy_crc32(data, data_len));
    result.status = SENSOR_TCP_OK;
    result.start = hdr.start;
    result.command = hdr.command;
    result.crc_valid = crc_valid;
    result.data = data;
    result.data_len = data_len;
    item->on_complete(&result, item->ctx);
}

static void *worker_main(void *arg)
{
    sensor_tcp_channel_t *ch = (sensor_tcp_channel_t *)arg;
    for (;;) {
        sensor_tcp_queue_item_t item;
        savvy_status_t ps = savvy_queue_pop(&ch->queue, &item);
        if (ps != SAVVY_OK) {
            break; /* SAVVY_ERR_CLOSED: queue was cancelled/closed by stop() */
        }
        process_item(ch, &item);
        free(item.packet);
    }
    if (ch->sockfd >= 0) {
        close(ch->sockfd);
        ch->sockfd = -1;
        atomic_store(&ch->connected, false);
    }
    return NULL;
}

savvy_status_t sensor_tcp_channel_create(sensor_tcp_channel_t **out_channel,
                                          const char *host, uint16_t port,
                                          size_t max_packet_size, size_t queue_capacity)
{
    if (out_channel == NULL) {
        return SAVVY_ERR_INVALID_ARGUMENT;
    }
    *out_channel = NULL;
    if (host == NULL || host[0] == '\0' || port == 0 ||
        max_packet_size < SAVVY_PACKET_HEADER_LEN || queue_capacity == 0) {
        return SAVVY_ERR_INVALID_ARGUMENT;
    }

    /* Ignore SIGPIPE process-wide so a write to a peer-closed socket
     * returns EPIPE instead of terminating the daemon (required behavior
     * #25). Idempotent - safe if called again by another channel. */
    signal(SIGPIPE, SIG_IGN);

    sensor_tcp_channel_t *ch = (sensor_tcp_channel_t *)calloc(1, sizeof(*ch));
    if (ch == NULL) {
        return SAVVY_ERR_OUT_OF_MEMORY;
    }

    ch->host = sensor_tcp_dup_str(host);
    ch->parse_buf = (uint8_t *)malloc(max_packet_size);
    if (ch->host == NULL || ch->parse_buf == NULL) {
        free(ch->host);
        free(ch->parse_buf);
        free(ch);
        return SAVVY_ERR_OUT_OF_MEMORY;
    }

    ch->port = port;
    ch->max_packet_size = max_packet_size;
    ch->sockfd = -1;
    atomic_init(&ch->connected, false);
    atomic_init(&ch->stop_requested, false);

    savvy_status_t st = savvy_lifecycle_init(&ch->lifecycle);
    if (st != SAVVY_OK) {
        free(ch->host);
        free(ch->parse_buf);
        free(ch);
        return st;
    }

    st = savvy_queue_init(&ch->queue, queue_capacity, sizeof(sensor_tcp_queue_item_t), queue_item_destroy);
    if (st != SAVVY_OK) {
        savvy_lifecycle_destroy(&ch->lifecycle);
        free(ch->host);
        free(ch->parse_buf);
        free(ch);
        return st;
    }

    *out_channel = ch;
    return SAVVY_OK;
}

savvy_status_t sensor_tcp_channel_start(sensor_tcp_channel_t *channel)
{
    if (channel == NULL) {
        return SAVVY_ERR_INVALID_ARGUMENT;
    }
    bool transitioned = false;
    savvy_status_t st = savvy_lifecycle_start(&channel->lifecycle, &transitioned);
    if (st != SAVVY_OK) {
        return st;
    }
    if (!transitioned) {
        return SAVVY_OK; /* already running */
    }

    atomic_store(&channel->stop_requested, false);
    if (pthread_create(&channel->worker_thread, NULL, worker_main, channel) != 0) {
        savvy_lifecycle_stop(&channel->lifecycle, NULL);
        return SAVVY_ERR_UNKNOWN;
    }
    return SAVVY_OK;
}

static savvy_status_t enqueue_common(sensor_tcp_channel_t *ch, const uint8_t *packet, size_t packet_len,
                                      uint32_t response_timeout_ms, sensor_tcp_item_kind_t kind,
                                      sensor_tcp_on_complete_fn on_complete, void *ctx)
{
    if (ch == NULL || packet == NULL || on_complete == NULL) {
        return SAVVY_ERR_INVALID_ARGUMENT;
    }
    if (packet_len == 0 || packet_len > ch->max_packet_size) {
        return SAVVY_ERR_INVALID_ARGUMENT;
    }
    if (savvy_lifecycle_get(&ch->lifecycle) != SAVVY_LIFECYCLE_RUNNING) {
        return SAVVY_ERR_NOT_STARTED;
    }

    uint8_t *copy = (uint8_t *)malloc(packet_len);
    if (copy == NULL) {
        return SAVVY_ERR_OUT_OF_MEMORY;
    }
    memcpy(copy, packet, packet_len);

    sensor_tcp_queue_item_t item;
    item.packet = copy;
    item.packet_len = packet_len;
    item.response_timeout_ms = response_timeout_ms;
    item.kind = kind;
    item.on_complete = on_complete;
    item.ctx = ctx;

    savvy_status_t st = savvy_queue_try_push(&ch->queue, &item);
    if (st != SAVVY_OK) {
        /* Rejected (full, or racing a stop()): ownership never transferred
         * to the queue, so we free the copy here - the caller's own
         * buffer was never touched and remains theirs regardless. */
        free(copy);
        return st;
    }
    return SAVVY_OK;
}

savvy_status_t sensor_tcp_channel_submit(sensor_tcp_channel_t *channel,
                                          const uint8_t *packet, size_t packet_len,
                                          uint32_t response_timeout_ms,
                                          sensor_tcp_on_complete_fn on_complete, void *ctx)
{
    if (response_timeout_ms == 0) {
        return SAVVY_ERR_INVALID_ARGUMENT;
    }
    return enqueue_common(channel, packet, packet_len, response_timeout_ms, SENSOR_TCP_ITEM_NORMAL, on_complete, ctx);
}

savvy_status_t sensor_tcp_channel_submit_final(sensor_tcp_channel_t *channel,
                                                const uint8_t *packet, size_t packet_len,
                                                sensor_tcp_on_complete_fn on_complete, void *ctx)
{
    return enqueue_common(channel, packet, packet_len, 0, SENSOR_TCP_ITEM_FINAL, on_complete, ctx);
}

savvy_status_t sensor_tcp_channel_try_relay(sensor_tcp_channel_t *channel,
                                             const uint8_t *packet, size_t packet_len,
                                             sensor_tcp_on_complete_fn on_complete, void *ctx)
{
    if (channel == NULL) {
        return SAVVY_ERR_INVALID_ARGUMENT;
    }
    if (!atomic_load(&channel->connected)) {
        /* Fail fast without ever touching the queue or triggering a
         * connect - process_item() re-checks this authoritatively too. */
        return SAVVY_ERR_NOT_CONNECTED;
    }
    return enqueue_common(channel, packet, packet_len, 0, SENSOR_TCP_ITEM_RELAY, on_complete, ctx);
}

bool sensor_tcp_channel_is_connected(const sensor_tcp_channel_t *channel)
{
    if (channel == NULL) {
        return false;
    }
    return atomic_load(&channel->connected);
}

void sensor_tcp_channel_close_session(sensor_tcp_channel_t *channel)
{
    if (channel == NULL) {
        return;
    }
    close_session_internal(channel);
}

void sensor_tcp_channel_stop(sensor_tcp_channel_t *channel)
{
    if (channel == NULL) {
        return;
    }
    bool transitioned = false;
    savvy_lifecycle_stop(&channel->lifecycle, &transitioned);
    if (!transitioned) {
        return; /* already stopped */
    }

    atomic_store(&channel->stop_requested, true);
    savvy_queue_cancel(&channel->queue); /* wakes a blocked pop() immediately; drains+destroys buffered items */
    pthread_join(channel->worker_thread, NULL);
}

void sensor_tcp_channel_destroy(sensor_tcp_channel_t *channel)
{
    if (channel == NULL) {
        return;
    }
    sensor_tcp_channel_stop(channel);
    savvy_queue_destroy(&channel->queue);
    savvy_lifecycle_destroy(&channel->lifecycle);
    free(channel->parse_buf);
    free(channel->host);
    free(channel);
}
