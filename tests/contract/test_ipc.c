/*
 * CT-IPC-002~003 (session_tasks/CC-FOUNDATION.md "Required tests").
 * Real AF_UNIX SOCK_SEQPACKET transport - Docker Ubuntu 22.04 only
 * (macOS/Darwin does not support SOCK_SEQPACKET for AF_UNIX), built only
 * when SAVVY_IPC_REAL_TRANSPORT=ON.
 *
 * This repo (sensor_to_Linux) owns the CLIENT role in production
 * (savvy_ipc_client_connect, DEC-20260714-02). The MGR/server role has no
 * production code in this repo, so the test drives a minimal, test-only
 * raw AF_UNIX SOCK_SEQPACKET counterpart to exercise the real
 * savvy_ipc_client_connect implementation end to end - not a production
 * mock/dummy, purely test harness scaffolding (00_SCOPE_LOCK.md 5.2
 * explicitly allows "unit test와 mock 추가").
 *
 * CT-IPC-001 (full Android action/key catalog conformance) is NOT in this
 * file yet - it is pending the Android source action-catalog research and
 * will be added once that is available; it is intentionally not
 * registered as a CTest test until then (no stub/fake pass).
 *
 * Usage: test_ipc <002|003>
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/types.h>
#include <poll.h>
#include "savvy/platform/ipc_client.h"
#include "savvy/protocol/ipc_envelope.h"

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(name, cond) do { \
        if (cond) { g_pass++; printf("[PASS] %s\n", (name)); } \
        else      { g_fail++; printf("[FAIL] %s\n", (name)); } \
    } while (0)

static void make_socket_path(char *out, size_t cap)
{
    snprintf(out, cap, "/tmp/savvy-test-ipc-%d.sock", (int)getpid());
}

/* Minimal test-only raw server counterpart (simulates the MGR role's wire
 * behavior without depending on any MGR production code, which does not
 * exist in this repo). */
static int raw_server_listen(const char *path)
{
    int fd = socket(AF_UNIX, SOCK_SEQPACKET, 0);
    if (fd < 0) return -1;
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);
    unlink(path);
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) { close(fd); return -1; }
    if (listen(fd, 1) != 0) { close(fd); return -1; }
    return fd;
}

static int raw_server_accept(int listen_fd, int timeout_ms)
{
    struct pollfd pfd = { .fd = listen_fd, .events = POLLIN, .revents = 0 };
    if (poll(&pfd, 1, timeout_ms) <= 0) return -1;
    return accept(listen_fd, NULL, NULL);
}

/* ---- CT-IPC-002: connect -> send/recv -> disconnect -> reconnect -> resync; pre-connect send drop ---- */
static void test_ipc_002(void)
{
    char path[108];
    make_socket_path(path, sizeof(path));

    int listen_fd = raw_server_listen(path);
    CHECK("002 raw server listen ok", listen_fd >= 0);

    /* Pre-connect send: no connection exists yet, so there is structurally
     * no savvy_ipc_transport_t to send() through (the API only hands one
     * out after connect() succeeds) - connecting to a socket path with no
     * listener must fail cleanly, not hang or fabricate a peer. */
    savvy_ipc_transport_t transport;
    savvy_status_t st = savvy_ipc_client_connect("/tmp/savvy-test-ipc-nonexistent.sock", &transport);
    CHECK("002 connect to nonexistent server fails cleanly (nothing to send to)", st != SAVVY_OK);

    /* Connect. */
    st = savvy_ipc_client_connect(path, &transport);
    CHECK("002 client connect ok", st == SAVVY_OK);
    int server_peer_fd = raw_server_accept(listen_fd, 3000);
    CHECK("002 raw server accepts", server_peer_fd >= 0);

    /* Send/recv content match. */
    char *env_text = NULL; size_t env_len = 0;
    st = savvy_ipc_envelope_build("ACTION_STATE_REPORT", "{\"danger_count\":0}", &env_text, &env_len);
    CHECK("002 envelope build ok", st == SAVVY_OK);
    st = transport.send(&transport, env_text, env_len);
    CHECK("002 client send ok", st == SAVVY_OK);

    char recv_buf[4096];
    ssize_t n = recv(server_peer_fd, recv_buf, sizeof(recv_buf) - 1, 0);
    CHECK("002 server received exact content", n == (ssize_t)env_len && memcmp(recv_buf, env_text, env_len) == 0);
    free(env_text);

    /* Disconnect detection: recv()==0 on the client side when the server closes. */
    close(server_peer_fd);
    char probe[16]; size_t probe_len = 0;
    st = transport.recv(&transport, probe, sizeof(probe), &probe_len);
    CHECK("002 disconnect detected as recv()==0", st == SAVVY_OK && probe_len == 0);
    transport.close(&transport);

    /* Reconnect + resync: after reconnecting, the client can immediately
     * send/receive again - the mechanical capability CC-SENSOR-CORE
     * (SNC-03) builds its actual resync-expectation policy on top of. */
    savvy_ipc_transport_t transport2;
    st = savvy_ipc_client_connect(path, &transport2);
    CHECK("002 client reconnect ok", st == SAVVY_OK);
    int server_peer_fd2 = raw_server_accept(listen_fd, 3000);
    CHECK("002 raw server re-accepts after reconnect", server_peer_fd2 >= 0);

    char *resync_text = NULL; size_t resync_len = 0;
    savvy_ipc_envelope_build("ACTION_STATE_REPORT", "{\"danger_count\":0}", &resync_text, &resync_len);
    st = transport2.send(&transport2, resync_text, resync_len);
    ssize_t n2 = recv(server_peer_fd2, recv_buf, sizeof(recv_buf) - 1, 0);
    CHECK("002 message delivered after reconnect", st == SAVVY_OK &&
          n2 == (ssize_t)resync_len && memcmp(recv_buf, resync_text, resync_len) == 0);
    free(resync_text);

    close(server_peer_fd2);
    transport2.close(&transport2);
    close(listen_fd);
    unlink(path);
}

/* ---- CT-IPC-003: 65536B/65537B boundary, oversized single record, truncated JSON, recovery ---- */
static void test_ipc_003(void)
{
    char path[108];
    make_socket_path(path, sizeof(path));

    int listen_fd = raw_server_listen(path);
    savvy_ipc_transport_t transport;
    savvy_status_t st = savvy_ipc_client_connect(path, &transport);
    int server_peer_fd = raw_server_accept(listen_fd, 3000);
    CHECK("003 setup: connected", st == SAVVY_OK && server_peer_fd >= 0);

    /* Exactly 65536B is allowed. */
    {
        uint8_t *buf = (uint8_t *)malloc(SAVVY_IPC_MAX_MESSAGE);
        memset(buf, 'A', SAVVY_IPC_MAX_MESSAGE);
        st = transport.send(&transport, buf, SAVVY_IPC_MAX_MESSAGE);
        uint8_t *rx = (uint8_t *)malloc(SAVVY_IPC_MAX_MESSAGE + 16);
        ssize_t n = recv(server_peer_fd, rx, SAVVY_IPC_MAX_MESSAGE + 16, 0);
        CHECK("003 exactly 65536B accepted and delivered whole", st == SAVVY_OK && n == (ssize_t)SAVVY_IPC_MAX_MESSAGE);
        free(buf); free(rx);
    }

    /* 65537B is rejected before send() is ever called. */
    {
        uint8_t *buf = (uint8_t *)malloc(SAVVY_IPC_MAX_MESSAGE + 1);
        memset(buf, 'B', SAVVY_IPC_MAX_MESSAGE + 1);
        st = transport.send(&transport, buf, SAVVY_IPC_MAX_MESSAGE + 1);
        CHECK("003 65537B rejected before send()", st == SAVVY_ERR_OVERFLOW);
        free(buf);
    }

    /* A single record larger than the receiver's buffer cap is detected
     * via MSG_TRUNC and discarded whole, not partially parsed. */
    {
        uint8_t *big = (uint8_t *)malloc(4096);
        memset(big, 'C', 4096);
        ssize_t sent = send(server_peer_fd, big, 4096, 0); /* raw send from the test server, bypassing the 64KiB app cap on purpose */
        uint8_t small_cap[1024];
        size_t out_len = 999;
        st = transport.recv(&transport, small_cap, sizeof(small_cap), &out_len);
        CHECK("003 oversized single record -> SAVVY_ERR_OVERFLOW (MSG_TRUNC), discarded whole",
              sent == 4096 && st == SAVVY_ERR_OVERFLOW);
        free(big);
    }

    /* Oversized record followed by a normal record: no corruption/desync carries over. */
    {
        const char *normal = "{\"action\":\"ACTION_STATE_REPORT\",\"payload\":{}}";
        size_t normal_len = strlen(normal);
        ssize_t sent = send(server_peer_fd, normal, normal_len, 0);
        char rx[256]; size_t out_len = 0;
        st = transport.recv(&transport, rx, sizeof(rx), &out_len);
        CHECK("003 normal record after oversized one is received uncorrupted",
              sent == (ssize_t)normal_len && st == SAVVY_OK && out_len == normal_len &&
              memcmp(rx, normal, normal_len) == 0);
    }

    /* Truncated/incomplete JSON text (normal-sized record, malformed content):
     * the envelope/JSON layer must reject it explicitly, not crash or
     * silently accept partial data. */
    {
        const char *truncated = "{\"action\":\"ACTION_STATE_REPORT\",\"payload\":{\"danger_count\":0";
        size_t truncated_len = strlen(truncated); /* no closing braces */
        send(server_peer_fd, truncated, truncated_len, 0);
        char rx[256]; size_t out_len = 0;
        st = transport.recv(&transport, rx, sizeof(rx), &out_len);
        savvy_ipc_envelope_t env;
        savvy_status_t parse_st = savvy_ipc_envelope_parse(rx, out_len, &env);
        CHECK("003 truncated UTF-8 JSON explicitly rejected at parse (not crashed, not silently accepted)",
              st == SAVVY_OK && parse_st == SAVVY_ERR_PROTOCOL);
    }

    close(server_peer_fd);
    transport.close(&transport);
    close(listen_fd);
    unlink(path);
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: %s <002|003>\n", argv[0]);
        return 2;
    }

    if (strcmp(argv[1], "002") == 0) {
        test_ipc_002();
    } else if (strcmp(argv[1], "003") == 0) {
        test_ipc_003();
    } else {
        fprintf(stderr, "unknown test id: %s\n", argv[1]);
        return 2;
    }

    printf("\n=== CT-IPC-%s: %d passed, %d failed ===\n", argv[1], g_pass, g_fail);
    return (g_fail == 0) ? 0 : 1;
}
