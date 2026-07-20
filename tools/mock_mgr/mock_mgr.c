/* CC-SENSOR-CORE test tool: a minimal AF_UNIX SOCK_SEQPACKET SERVER
 * standing in for MGR (08_BLOCKERS.md B-006/DEC-20260714-02: MGR is the
 * server, Sensor is the client - Foundation only ships the client role in
 * this repo, savvy_ipc_client_connect[_cancelable](), so the server side
 * needed for integration-testing mgr_ipc has no Foundation equivalent to
 * wrap here). SOCK_SEQPACKET for AF_UNIX is Linux-only (unsupported on
 * Darwin) - this tool is only functionally testable on Ubuntu Docker,
 * matching src/features/mgr_ipc's own SENSOR_MGR_IPC_REAL_TRANSPORT gate.
 *
 * Not production code, not linked into any src/features feature target -
 * a standalone executable used only from tests/integration/sensor_core
 * and manual Docker verification.
 *
 * Usage: mock_mgr <socket_path> <cycles> <hold_open_ms>
 *   For <cycles> connection cycles: accept one client, drain and log
 *   whatever it sends (expects CONNECT_BROADCAST_IPC first), push one
 *   CONFIG then one DEVICE envelope, keep draining/logging for
 *   <hold_open_ms>, then close (simulating a server-initiated disconnect)
 *   and loop back to accept() again - so a driving test can exercise
 *   connect -> receive Config/Device -> disconnect -> reconnect -> receive
 *   latest Config/Device again end to end against the real transport. */

/* clock_gettime()/CLOCK_MONOTONIC are POSIX.1-2008; glibc hides them under
 * -std=c11 without this (see src/core/clock.c for Foundation's own
 * instance of the same fix). Must be defined before any system header. */
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#include "savvy/core/error.h"
#include "savvy/protocol/ipc_envelope.h"

static volatile sig_atomic_t g_stop = 0;
static void on_signal(int sig) {
    (void)sig;
    g_stop = 1;
}

static int64_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static int send_envelope(int fd, const char *action, const char *payload_json) {
    char *text = NULL;
    size_t len = 0;
    savvy_status_t st = savvy_ipc_envelope_build(action, payload_json, &text, &len);
    if (st != SAVVY_OK) {
        fprintf(stderr, "mock_mgr: envelope_build(%s) failed: %s\n", action, savvy_status_str(st));
        return -1;
    }
    ssize_t n = send(fd, text, len, 0);
    int rc = (n == (ssize_t)len) ? 0 : -1;
    if (rc != 0) {
        fprintf(stderr, "mock_mgr: send(%s) failed: %s\n", action, strerror(errno));
    } else {
        printf("mock_mgr: sent %s\n", action);
    }
    free(text);
    return rc;
}

static int expect_connect_handshake(int fd, int64_t deadline_ms) {
    char buf[65536 + 1];
    while (now_ms() < deadline_ms) {
        int64_t remaining = deadline_ms - now_ms();
        struct pollfd pfd = {fd, POLLIN, 0};
        int pr = poll(&pfd, 1, (int)remaining);
        if (pr < 0 && errno == EINTR) {
            continue;
        }
        if (pr <= 0) {
            break;
        }
        ssize_t n = recv(fd, buf, sizeof(buf) - 1, 0);
        if (n <= 0) {
            break;
        }
        buf[n] = '\0';
        savvy_ipc_envelope_t env;
        if (savvy_ipc_envelope_parse(buf, (size_t)n, &env) != SAVVY_OK) {
            fprintf(stderr, "mock_mgr: malformed first message\n");
            return -1;
        }
        bool valid = strcmp(env.action, "com.uniuni.savvymgr.ipc.connect") == 0 &&
                     strcmp(env.payload_json, "{}") == 0;
        savvy_ipc_envelope_free(&env);
        if (!valid) {
            fprintf(stderr, "mock_mgr: expected CONNECT_BROADCAST_IPC with {} payload\n");
            return -1;
        }
        printf("mock_mgr: CONNECT handshake verified\n");
        return 0;
    }
    fprintf(stderr, "mock_mgr: CONNECT handshake missing\n");
    return -1;
}

/* Config/Device are MGR->Sensor only. Any second Sensor->MGR record before
 * this mock closes the cycle proves cached replay or an unexpected outbound
 * action, so fail the child process for the integration test to observe. */
static int reject_unexpected_sensor_messages(int fd, int64_t deadline_ms) {
    char buf[65536 + 1];
    while (now_ms() < deadline_ms) {
        int64_t remaining = deadline_ms - now_ms();
        struct pollfd pfd = {fd, POLLIN, 0};
        int pr = poll(&pfd, 1, (int)remaining);
        if (pr < 0 && errno == EINTR) {
            continue;
        }
        if (pr == 0) {
            return 0;
        }
        if (pr < 0) {
            fprintf(stderr, "mock_mgr: poll error: %s\n", strerror(errno));
            return -1;
        }
        ssize_t n = recv(fd, buf, sizeof(buf) - 1, 0);
        if (n == 0) {
            return 0; /* driving test stopped the client */
        }
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            fprintf(stderr, "mock_mgr: recv error: %s\n", strerror(errno));
            return -1;
        }
        fprintf(stderr, "mock_mgr: unexpected Sensor message after CONNECT (%zd bytes)\n", n);
        return -1;
    }
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 4) {
        fprintf(stderr, "usage: %s <socket_path> <cycles> <hold_open_ms>\n", argv[0]);
        return 2;
    }
    const char *socket_path = argv[1];
    int cycles = atoi(argv[2]);
    int hold_open_ms = atoi(argv[3]);

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    int listen_fd = socket(AF_UNIX, SOCK_SEQPACKET, 0);
    if (listen_fd < 0) {
        fprintf(stderr, "mock_mgr: socket() failed: %s\n", strerror(errno));
        return 1;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    if (strlen(socket_path) >= sizeof(addr.sun_path)) {
        fprintf(stderr, "mock_mgr: socket path too long\n");
        close(listen_fd);
        return 1;
    }
    strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);

    unlink(socket_path); /* best-effort: a stale path from a prior run must not block bind() */

    if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        fprintf(stderr, "mock_mgr: bind(%s) failed: %s\n", socket_path, strerror(errno));
        close(listen_fd);
        return 1;
    }
    if (listen(listen_fd, 1) != 0) {
        fprintf(stderr, "mock_mgr: listen() failed: %s\n", strerror(errno));
        close(listen_fd);
        return 1;
    }

    printf("mock_mgr: listening on %s for %d cycle(s)\n", socket_path, cycles);

    int exit_code = 0;
    for (int cycle = 0; cycle < cycles && !g_stop; cycle++) {
        printf("mock_mgr: waiting for connection (cycle %d/%d)\n", cycle + 1, cycles);

        struct pollfd pfd;
        pfd.fd = listen_fd;
        pfd.events = POLLIN;
        pfd.revents = 0;
        int pr = poll(&pfd, 1, 10000);
        if (pr <= 0) {
            fprintf(stderr, "mock_mgr: no connection within 10s, aborting\n");
            exit_code = 1;
            break;
        }

        int client_fd = accept(listen_fd, NULL, NULL);
        if (client_fd < 0) {
            fprintf(stderr, "mock_mgr: accept() failed: %s\n", strerror(errno));
            exit_code = 1;
            break;
        }
        printf("mock_mgr: accepted connection\n");

        if (expect_connect_handshake(client_fd, now_ms() + 2000) != 0 ||
            send_envelope(client_fd, "com.uniuni.savvysensor.config", "{\"jsonConfigDto\":{}}") != 0 ||
            send_envelope(client_fd, "com.uniuni.savvysensor.device", "{\"jsonDeviceDto\":{}}") != 0 ||
            reject_unexpected_sensor_messages(client_fd, now_ms() + hold_open_ms) != 0) {
            exit_code = 1;
            close(client_fd);
            break;
        }

        close(client_fd);
        printf("mock_mgr: closed connection (cycle %d/%d)\n", cycle + 1, cycles);
    }

    close(listen_fd);
    unlink(socket_path);
    return exit_code;
}
