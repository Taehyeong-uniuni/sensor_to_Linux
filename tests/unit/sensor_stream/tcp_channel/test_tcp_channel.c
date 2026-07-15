/* SNS-STR-006 (queue overflow), SNS-STR-007 (lifecycle wake/idempotency),
 * SNS-STR-008 (repeated connect/disconnect fd leak), SNS-STR-009 (channel
 * isolation - two independent instances don't interfere), SNS-STR-010
 * (a timed-out request's late response is never consumed as the next
 * request's response, because a timeout closes the session and the next
 * request lazily reconnects on a fresh socket + fresh parser buffer). */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "sensor_platform/tcp_channel.h"
#include "savvy/protocol/packet_codec.h"

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond, msg) do { \
    if (cond) { g_pass++; } \
    else { g_fail++; fprintf(stderr, "FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__); } \
} while (0)

static const uint8_t SERIAL[SAVVY_PACKET_SERIAL_LEN] = {'0','0','0','0','0','0','0','0','0','0','0','0','0','1'};

/* ---- minimal loopback test-server helpers ---- */

typedef struct {
    int listen_fd;
    uint16_t port;
} test_listener_t;

static bool make_listener(test_listener_t *l)
{
    l->listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (l->listen_fd < 0) {
        return false;
    }
    int one = 1;
    setsockopt(l->listen_fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0; /* OS-assigned ephemeral port */
    if (bind(l->listen_fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        close(l->listen_fd);
        return false;
    }
    if (listen(l->listen_fd, 8) != 0) {
        close(l->listen_fd);
        return false;
    }
    socklen_t alen = sizeof(addr);
    getsockname(l->listen_fd, (struct sockaddr *)&addr, &alen);
    l->port = ntohs(addr.sin_port);
    return true;
}

static void encode_request(uint8_t cmd, const uint8_t *data, size_t data_len, uint8_t *out, size_t out_cap, size_t *out_len)
{
    savvy_status_t st = savvy_packet_encode((uint8_t)'S', cmd, 0, 0, SERIAL, sizeof(SERIAL), data, data_len, out, out_cap, out_len);
    if (st != SAVVY_OK) {
        fprintf(stderr, "test bug: savvy_packet_encode failed: %d\n", (int)st);
        abort();
    }
}

typedef struct { sensor_tcp_result_status_t status; uint8_t command; size_t data_len; int calls; } completion_capture_t;

static void capture_complete(const sensor_tcp_result_t *result, void *ctx)
{
    completion_capture_t *cap = (completion_capture_t *)ctx;
    cap->status = result->status;
    cap->command = result->command;
    cap->data_len = result->data_len;
    cap->calls++;
}

/* ---- SNS-STR-006: bounded queue overflow ---- */
typedef struct { test_listener_t l; volatile bool stop; } never_accept_ctx_t;

static void *never_accept_thread(void *arg)
{
    never_accept_ctx_t *c = (never_accept_ctx_t *)arg;
    /* Deliberately never accept() - the OS listen backlog absorbs the
     * connect(), so our client-side connect() succeeds immediately, but
     * nothing ever reads what we send: this keeps the worker "busy"
     * (blocked in the response-wait) for the whole test, which is exactly
     * what's needed to force the bounded queue to fill up behind it. */
    while (!c->stop) {
        usleep(20 * 1000);
    }
    return NULL;
}

static void test_006(void)
{
    never_accept_ctx_t ctx = {0};
    CHECK(make_listener(&ctx.l), "create never-accept listener");
    ctx.stop = false;
    pthread_t th;
    pthread_create(&th, NULL, never_accept_thread, &ctx);

    sensor_tcp_channel_t *ch = NULL;
    savvy_status_t st = sensor_tcp_channel_create(&ch, "127.0.0.1", ctx.l.port, 4096, 2 /* small bounded capacity */);
    CHECK(st == SAVVY_OK, "create channel with capacity 2");
    CHECK(sensor_tcp_channel_start(ch) == SAVVY_OK, "start channel");

    uint8_t pkt[64];
    size_t pkt_len;
    encode_request((uint8_t)'I', NULL, 0, pkt, sizeof(pkt), &pkt_len);

    completion_capture_t caps[8];
    memset(caps, 0, sizeof(caps));

    /* First submit occupies the worker (it will be sent, then block
     * waiting up to 500ms for a response that never comes - long enough
     * for the next two enqueue attempts to observe a full queue). Give
     * the worker a moment to actually dequeue it before filling the
     * queue below, so the capacity accounting isn't racy against
     * scheduling: once popped, this item no longer counts against the
     * bounded queue's own capacity. */
    savvy_status_t s0 = sensor_tcp_channel_submit(ch, pkt, pkt_len, 500, capture_complete, &caps[0]);
    CHECK(s0 == SAVVY_OK, "first submit accepted (becomes the in-flight item)");
    usleep(50 * 1000);

    /* These two should fill the capacity-2 queue while the first is being processed. */
    savvy_status_t s1 = sensor_tcp_channel_submit(ch, pkt, pkt_len, 500, capture_complete, &caps[1]);
    savvy_status_t s2 = sensor_tcp_channel_submit(ch, pkt, pkt_len, 500, capture_complete, &caps[2]);
    CHECK(s1 == SAVVY_OK, "second submit accepted (fills queue slot 1)");
    CHECK(s2 == SAVVY_OK, "third submit accepted (fills queue slot 2)");

    /* Queue is now full (capacity 2, both slots occupied) - a further
     * enqueue must be rejected explicitly, not block, not evict slot 1/2. */
    savvy_status_t s3 = sensor_tcp_channel_submit(ch, pkt, pkt_len, 500, capture_complete, &caps[3]);
    CHECK(s3 == SAVVY_ERR_OVERFLOW, "fourth submit rejected with SAVVY_ERR_OVERFLOW when queue is full");
    CHECK(caps[3].calls == 0, "rejected submit never invokes on_complete");

    /* Let everything drain (all three accepted items will eventually time
     * out, ~500ms each, processed serially). */
    usleep(2200 * 1000);
    CHECK(caps[0].calls == 1 && caps[0].status == SENSOR_TCP_ERR_RESPONSE_TIMEOUT, "item 1 times out (never-accept server)");
    CHECK(caps[1].calls == 1 && caps[1].status == SENSOR_TCP_ERR_RESPONSE_TIMEOUT, "item 2 (queued) was not evicted - it still ran and timed out");
    CHECK(caps[2].calls == 1 && caps[2].status == SENSOR_TCP_ERR_RESPONSE_TIMEOUT, "item 3 (queued) was not evicted - it still ran and timed out");

    sensor_tcp_channel_destroy(ch);
    ctx.stop = true;
    pthread_join(th, NULL);
    close(ctx.l.listen_fd);
}

/* ---- SNS-STR-007: lifecycle - wakes promptly from queue-wait AND response-wait, idempotent start/stop ---- */
static void test_007(void)
{
    never_accept_ctx_t ctx = {0};
    CHECK(make_listener(&ctx.l), "create never-accept listener for 007");
    ctx.stop = false;
    pthread_t th;
    pthread_create(&th, NULL, never_accept_thread, &ctx);

    sensor_tcp_channel_t *ch = NULL;
    sensor_tcp_channel_create(&ch, "127.0.0.1", ctx.l.port, 4096, 4);

    /* Idempotent start: calling twice must not error or create a second thread. */
    CHECK(sensor_tcp_channel_start(ch) == SAVVY_OK, "first start");
    CHECK(sensor_tcp_channel_start(ch) == SAVVY_OK, "second start is a no-op, not an error");

    /* Idempotent stop with NOTHING in flight (worker blocked in queue-pop). */
    CHECK(sensor_tcp_channel_is_connected(ch) == false, "not connected before any submit");

    /* Put one long-response-timeout item in flight so the worker is
     * blocked in the response-wait when we call stop(), proving stop()
     * interrupts that wait promptly rather than waiting out the full
     * timeout. */
    uint8_t pkt[64];
    size_t pkt_len;
    encode_request((uint8_t)'I', NULL, 0, pkt, sizeof(pkt), &pkt_len);
    completion_capture_t cap = {0};
    sensor_tcp_channel_submit(ch, pkt, pkt_len, 30000 /* deliberately much longer than the stop should take */, capture_complete, &cap);
    usleep(50 * 1000); /* give the worker a moment to pick it up and enter the response-wait */

    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    sensor_tcp_channel_stop(ch); /* must return quickly, not after 30s */
    clock_gettime(CLOCK_MONOTONIC, &t1);
    double elapsed_ms = (t1.tv_sec - t0.tv_sec) * 1000.0 + (t1.tv_nsec - t0.tv_nsec) / 1e6;
    CHECK(elapsed_ms < 2000.0, "stop() interrupts a blocked response-wait within ~2s, not 30s");

    /* Idempotent stop again, after already stopped. */
    sensor_tcp_channel_stop(ch); /* must not hang or crash */
    CHECK(true, "second stop() after already-stopped does not hang/crash");

    sensor_tcp_channel_destroy(ch);
    ctx.stop = true;
    pthread_join(th, NULL);
    close(ctx.l.listen_fd);
}

/* ---- SNS-STR-008: repeated connect/disconnect leaves no fd leak ---- */
/* NOTE: accept() does not honor SO_RCVTIMEO (that only bounds recv()/read()
 * timeouts) - an indefinite "while(!stop) accept()" can block forever once
 * the last expected connection has already arrived, hanging pthread_join()
 * at cleanup. Bounding the loop by the exact expected connection count
 * (known by each test) avoids this entirely. */
typedef struct { test_listener_t l; int expected_connections; } echo_ctx_t;

static void *echo_accept_thread(void *arg)
{
    echo_ctx_t *c = (echo_ctx_t *)arg;
    for (int i = 0; i < c->expected_connections; i++) {
        struct sockaddr_in peer;
        socklen_t plen = sizeof(peer);
        int fd = accept(c->l.listen_fd, (struct sockaddr *)&peer, &plen);
        if (fd < 0) {
            continue;
        }
        uint8_t buf[4096];
        ssize_t n = recv(fd, buf, sizeof(buf), 0);
        if (n > 0) {
            uint8_t resp[64];
            size_t resp_len;
            savvy_packet_encode((uint8_t)'S', (uint8_t)'I', 0, 0, SERIAL, sizeof(SERIAL), NULL, 0, resp, sizeof(resp), &resp_len);
            send(fd, resp, resp_len, 0);
        }
        close(fd);
    }
    return NULL;
}

static void test_008(void)
{
    echo_ctx_t ctx = {0};
    CHECK(make_listener(&ctx.l), "create echo listener for 008");
    ctx.expected_connections = 50;
    pthread_t th;
    pthread_create(&th, NULL, echo_accept_thread, &ctx);

    int fd_probe_before = dup(0);
    close(fd_probe_before);

    for (int i = 0; i < 50; i++) {
        sensor_tcp_channel_t *ch = NULL;
        sensor_tcp_channel_create(&ch, "127.0.0.1", ctx.l.port, 4096, 4);
        sensor_tcp_channel_start(ch);

        uint8_t pkt[64];
        size_t pkt_len;
        encode_request((uint8_t)'I', NULL, 0, pkt, sizeof(pkt), &pkt_len);
        completion_capture_t cap = {0};
        sensor_tcp_channel_submit(ch, pkt, pkt_len, 1000, capture_complete, &cap);
        usleep(15 * 1000);

        sensor_tcp_channel_destroy(ch); /* stop() + close socket + join thread + free */
    }

    int fd_probe_after = dup(0);
    close(fd_probe_after);
    CHECK(fd_probe_after == fd_probe_before, "no fd leak across 50 create/start/submit/destroy cycles");

    pthread_join(th, NULL);
    close(ctx.l.listen_fd);
}

/* ---- SNS-STR-009: two independent channel instances don't interfere - forcing one to fail leaves the other unaffected ---- */
static void test_009(void)
{
    echo_ctx_t ctx_a = {0}, ctx_b = {0};
    CHECK(make_listener(&ctx_a.l), "listener A (stream-role stand-in)");
    CHECK(make_listener(&ctx_b.l), "listener B (voice-role stand-in)");
    pthread_t th_b;
    ctx_b.expected_connections = 1;
    pthread_create(&th_b, NULL, echo_accept_thread, &ctx_b);
    /* Listener A intentionally has no accept-loop thread: any connect to
     * it will succeed (backlog), but reads will simply hang -> forced
     * into a response timeout/failure, in isolation from channel B. */

    sensor_tcp_channel_t *chan_a = NULL, *chan_b = NULL;
    sensor_tcp_channel_create(&chan_a, "127.0.0.1", ctx_a.l.port, 4096, 4);
    sensor_tcp_channel_create(&chan_b, "127.0.0.1", ctx_b.l.port, 4096, 4);
    sensor_tcp_channel_start(chan_a);
    sensor_tcp_channel_start(chan_b);

    uint8_t pkt[64];
    size_t pkt_len;
    encode_request((uint8_t)'I', NULL, 0, pkt, sizeof(pkt), &pkt_len);

    completion_capture_t cap_a = {0}, cap_b = {0};
    sensor_tcp_channel_submit(chan_a, pkt, pkt_len, 300, capture_complete, &cap_a); /* will time out */
    usleep(500 * 1000);
    CHECK(cap_a.calls == 1 && cap_a.status == SENSOR_TCP_ERR_RESPONSE_TIMEOUT, "channel A fails/times out on its own");

    /* Channel B must be completely unaffected - it still connects and gets a real response. */
    sensor_tcp_channel_submit(chan_b, pkt, pkt_len, 1000, capture_complete, &cap_b);
    usleep(300 * 1000);
    CHECK(cap_b.calls == 1 && cap_b.status == SENSOR_TCP_OK, "channel B is unaffected by channel A's failure and still works");

    sensor_tcp_channel_destroy(chan_a);
    sensor_tcp_channel_destroy(chan_b);
    pthread_join(th_b, NULL);
    close(ctx_a.l.listen_fd);
    close(ctx_b.l.listen_fd);
}

/* ---- SNS-STR-010: a request that times out must not have its late
 * response consumed by the NEXT request - verified by confirming the
 * next request gets a connection that only ever saw ITS OWN bytes
 * (a fresh accept() on the server side proves the client reconnected on
 * a brand new socket rather than continuing to read the old, timed-out
 * one). ---- */
typedef struct {
    test_listener_t l;
    volatile int accept_count;
    volatile bool first_conn_should_stay_silent;
} stale_ctx_t;

static void *stale_accept_thread(void *arg)
{
    stale_ctx_t *c = (stale_ctx_t *)arg;
    for (int i = 0; i < 2; i++) {
        struct sockaddr_in peer;
        socklen_t plen = sizeof(peer);
        int fd = accept(c->l.listen_fd, (struct sockaddr *)&peer, &plen);
        if (fd < 0) {
            continue;
        }
        c->accept_count++;
        uint8_t buf[4096];
        ssize_t n = recv(fd, buf, sizeof(buf), 0);
        (void)n;
        if (i == 0) {
            /* First connection: stay silent well past the client's
             * timeout (simulating a very late response), then finally
             * send a response to a socket the client has ALREADY closed
             * and moved on from - this response must be unobservable to
             * the client's second request. */
            usleep(600 * 1000);
            uint8_t resp[64];
            size_t resp_len;
            savvy_packet_encode((uint8_t)'S', (uint8_t)'I', 0, 0, SERIAL, sizeof(SERIAL), NULL, 0, resp, sizeof(resp), &resp_len);
            send(fd, resp, resp_len, 0); /* likely fails/ignored - client already closed its end */
            close(fd);
        } else {
            uint8_t resp[64];
            size_t resp_len;
            savvy_packet_encode((uint8_t)'S', (uint8_t)'O', 0, 0, SERIAL, sizeof(SERIAL), NULL, 0, resp, sizeof(resp), &resp_len);
            send(fd, resp, resp_len, 0);
            close(fd);
        }
    }
    return NULL;
}

static void test_010(void)
{
    stale_ctx_t ctx = {0};
    CHECK(make_listener(&ctx.l), "listener for 010");
    pthread_t th;
    pthread_create(&th, NULL, stale_accept_thread, &ctx);

    sensor_tcp_channel_t *ch = NULL;
    sensor_tcp_channel_create(&ch, "127.0.0.1", ctx.l.port, 4096, 4);
    sensor_tcp_channel_start(ch);

    uint8_t pkt1[64], pkt2[64];
    size_t pkt1_len, pkt2_len;
    encode_request((uint8_t)'I', NULL, 0, pkt1, sizeof(pkt1), &pkt1_len);
    encode_request((uint8_t)'O', NULL, 0, pkt2, sizeof(pkt2), &pkt2_len);

    completion_capture_t cap1 = {0}, cap2 = {0};
    sensor_tcp_channel_submit(ch, pkt1, pkt1_len, 300, capture_complete, &cap1);
    usleep(500 * 1000); /* request 1 times out at 300ms; server stays silent until 600ms */
    CHECK(cap1.calls == 1 && cap1.status == SENSOR_TCP_ERR_RESPONSE_TIMEOUT, "request 1 times out");

    /* Request 2 must reconnect (fresh socket) and get its OWN response
     * (Command='O' echoed back), never the stale Command='I' bytes the
     * server sends late on the first (now-closed) connection. */
    sensor_tcp_channel_submit(ch, pkt2, pkt2_len, 1000, capture_complete, &cap2);
    usleep(300 * 1000);
    CHECK(cap2.calls == 1 && cap2.status == SENSOR_TCP_OK, "request 2 gets a real response on a fresh connection");
    CHECK(cap2.command == (uint8_t)'O', "request 2's response is its OWN (Command='O'), not request 1's stale late Command='I'");

    /* give the late/stale write from the server's first connection time
     * to arrive and be ignored (already-closed local fd) before teardown */
    usleep(300 * 1000);

    sensor_tcp_channel_destroy(ch);
    pthread_join(th, NULL);
    close(ctx.l.listen_fd);
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: %s <006|007|008|009|010>\n", argv[0]);
        return 2;
    }
    if (strcmp(argv[1], "006") == 0) {
        test_006();
    } else if (strcmp(argv[1], "007") == 0) {
        test_007();
    } else if (strcmp(argv[1], "008") == 0) {
        test_008();
    } else if (strcmp(argv[1], "009") == 0) {
        test_009();
    } else if (strcmp(argv[1], "010") == 0) {
        test_010();
    } else {
        fprintf(stderr, "unknown test id: %s\n", argv[1]);
        return 2;
    }
    printf("\n=== SNS-STR-%s: %d passed, %d failed ===\n", argv[1], g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
