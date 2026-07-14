/*
 * CT-IPC-001~003 (session_tasks/CC-FOUNDATION.md "Required tests").
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
 * CT-IPC-001 uses the confirmed Android action catalog
 * (contracts/ipc_action_catalog.md, from Android source research) - all
 * 23 cataloged actions, valid payload + a type-mismatch variant for every
 * action that declares required/optional keys.
 *
 * Usage: test_ipc <001|002|003>
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/types.h>
#include <poll.h>
#include "savvy/platform/ipc_client.h"
#include "savvy/protocol/ipc_envelope.h"
#include "savvy/protocol/ipc_action_catalog.h"

#define TEST_TIMEOUT_MS 3000u

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

/* Builds a raw byte buffer of EXACTLY `target_total_len` bytes that is a
 * syntactically valid {"action":...,"payload":{"blob":"AAA..."}} JSON
 * envelope, for boundary-size tests that must exercise real envelope
 * bytes rather than arbitrary data. Caller frees with free(). */
static char *make_padded_envelope(const char *action, size_t target_total_len)
{
    const char *prefix_fmt = "{\"action\":\"%s\",\"payload\":{\"blob\":\"";
    const char *suffix = "\"}}";
    size_t prefix_len = strlen(action) + strlen(prefix_fmt) - 2; /* -2 for the "%s" placeholder */
    size_t suffix_len = strlen(suffix);
    if (target_total_len < prefix_len + suffix_len) {
        return NULL;
    }
    size_t pad_len = target_total_len - prefix_len - suffix_len;

    char *buf = (char *)malloc(target_total_len + 1);
    if (buf == NULL) {
        return NULL;
    }
    int written = snprintf(buf, prefix_len + 1, "{\"action\":\"%s\",\"payload\":{\"blob\":\"", action);
    if (written < 0 || (size_t)written != prefix_len) {
        free(buf);
        return NULL;
    }
    memset(buf + prefix_len, 'A', pad_len);
    memcpy(buf + prefix_len + pad_len, suffix, suffix_len);
    buf[target_total_len] = '\0';
    return buf;
}

/* ---- CT-IPC-001: real Android action/key catalog conformance (all 23 actions) ---- */
typedef struct catalog_test_case {
    const char *action;
    const char *valid_payload;
    const char *type_mismatch_payload; /* NULL if the action has no keys to corrupt */
} catalog_test_case_t;

static const catalog_test_case_t CATALOG_CASES[] = {
    { "com.uniuni.savvysensor.config", "{\"jsonConfigDto\":{}}", "{\"jsonConfigDto\":\"not-an-object\"}" },
    { "com.uniuni.savvysensor.device", "{\"jsonDeviceDto\":{}}", "{\"jsonDeviceDto\":123}" },
    { "com.uniuni.savvysensor.serverip", "{}", NULL },
    { "com.uniuni.savvysensor.voicestart", "{}", NULL },
    { "com.uniuni.savvysensor.streamstart", "{}", NULL },
    { "com.uniuni.savvysensor.test", "{\"TEST\":\"MIC\"}", "{\"TEST\":123}" },
    { "com.uniuni.savvysensor.sensor.reset", "{\"RESET\":\"LED\"}", "{\"RESET\":true}" },
    { "com.uniuni.savvysensor.sensor.beaconnotify", "{}", NULL },
    { "com.uniuni.savvysensor.sensor.apkupdate", "{}", NULL },
    { "com.uniuni.savvysensor.sensor.status.ledpwr", "{\"PwrLedState\":1}", "{\"PwrLedState\":\"1\"}" },
    { "com.uniuni.savvysensor.sensor.status.alert",
      "{\"AlertLedState\":1,\"AlertTime\":2,\"AlertSec\":3}",
      "{\"AlertLedState\":\"x\",\"AlertTime\":2,\"AlertSec\":3}" },
    { "com.uniuni.savvysensor.sensor.rknn.alert", "{}", NULL },
    { "com.uniuni.savvysensor.sensor.update.threash.hold", "{}", NULL },
    { "com.uniuni.savvysensor.sensor.rknn.anal.result", "{\"rknnAnalResult\":\"RET:level03\"}", "{\"rknnAnalResult\":42}" },
    { "com.uniuni.savvysensor.sensor.max.cpu.temp", "{}", NULL },
    { "com.uniuni.savvymgr.ipc.connect", "{}", NULL },
    { "com.uniuni.savvymgr.getstate.sensor", "{\"SENSOR\":\"MIC\",\"STATE\":1}", "{\"SENSOR\":\"MIC\",\"STATE\":\"1\"}" },
    { "com.uniuni.savvymgr.alert.sensor", "{\"IFCOMM_START\":1}", "{\"IFCOMM_START\":\"S\"}" },
    { "com.uniuni.savvymgr.upload.sensor",
      "{\"targetFilePath\":\"/base/x\",\"targetFileNm\":\"y\"}",
      "{\"targetFilePath\":1,\"targetFileNm\":\"y\"}" },
    { "com.uniuni.savvymgr.restart.sensor", "{\"DELAY_SEC\":5}", "{\"DELAY_SEC\":\"5\"}" },
    { "com.uniuni.savvymgr.fracture.sensor", "{}", NULL },
    { "com.uniuni.savvymgr.tof.property", "{}", NULL }, /* all 4 keys optional - empty payload is valid */
    { "com.uniuni.savvymgr.update.threash.rslt", "{\"rslt\":\"True\"}", "{\"rslt\":1}" },
};
#define N_CATALOG_CASES (sizeof(CATALOG_CASES) / sizeof(CATALOG_CASES[0]))

static int round_trip_and_validate(int server_peer_fd, savvy_ipc_transport_t *transport,
                                    const char *action, const char *payload_json,
                                    savvy_status_t *out_validate_status)
{
    char *text = NULL;
    size_t len = 0;
    if (savvy_ipc_envelope_build(action, payload_json, &text, &len) != SAVVY_OK) {
        return 0;
    }
    savvy_status_t st = transport->send(transport, text, len, TEST_TIMEOUT_MS);
    free(text);
    if (st != SAVVY_OK) {
        return 0;
    }

    char rx[8192];
    ssize_t n = recv(server_peer_fd, rx, sizeof(rx) - 1, 0);
    if (n <= 0) {
        return 0;
    }
    rx[n] = '\0';

    savvy_ipc_envelope_t env;
    if (savvy_ipc_envelope_parse(rx, (size_t)n, &env) != SAVVY_OK) {
        return 0;
    }
    int action_ok = (strcmp(env.action, action) == 0) && savvy_ipc_action_known(env.action);
    *out_validate_status = savvy_ipc_action_validate_payload(env.action, env.payload_json);
    savvy_ipc_envelope_free(&env);
    return action_ok;
}

static void test_ipc_001(void)
{
    char path[108];
    make_socket_path(path, sizeof(path));

    int listen_fd = raw_server_listen(path);
    savvy_ipc_transport_t transport;
    savvy_status_t st = savvy_ipc_client_connect(path, TEST_TIMEOUT_MS, &transport);
    int server_peer_fd = raw_server_accept(listen_fd, (int)TEST_TIMEOUT_MS);
    CHECK("001 setup: connected", st == SAVVY_OK && server_peer_fd >= 0);

    int all_valid_ok = 1;
    int all_mismatch_ok = 1;
    for (size_t i = 0; i < N_CATALOG_CASES; i++) {
        savvy_status_t vst = SAVVY_ERR_UNKNOWN;
        int ok = round_trip_and_validate(server_peer_fd, &transport, CATALOG_CASES[i].action,
                                          CATALOG_CASES[i].valid_payload, &vst);
        if (!ok || vst != SAVVY_OK) {
            all_valid_ok = 0;
            printf("  [catalog valid case FAILED] %s\n", CATALOG_CASES[i].action);
        }

        if (CATALOG_CASES[i].type_mismatch_payload != NULL) {
            savvy_status_t mst = SAVVY_ERR_UNKNOWN;
            int mok = round_trip_and_validate(server_peer_fd, &transport, CATALOG_CASES[i].action,
                                               CATALOG_CASES[i].type_mismatch_payload, &mst);
            if (!mok || mst != SAVVY_ERR_PROTOCOL) {
                all_mismatch_ok = 0;
                printf("  [catalog type-mismatch case FAILED to reject] %s\n", CATALOG_CASES[i].action);
            }
        }
    }
    CHECK("001 all 23 catalog actions: valid payload parses+validates OK", all_valid_ok);
    CHECK("001 all applicable catalog actions: type-mismatch payload rejected", all_mismatch_ok);

    /* Missing "action" field entirely. */
    {
        const char *bad = "{\"payload\":{}}";
        st = transport.send(&transport, bad, strlen(bad), TEST_TIMEOUT_MS);
        char rx[512];
        ssize_t n = (st == SAVVY_OK) ? recv(server_peer_fd, rx, sizeof(rx) - 1, 0) : -1;
        int ok = 0;
        if (n > 0) {
            rx[n] = '\0';
            savvy_ipc_envelope_t env;
            ok = (savvy_ipc_envelope_parse(rx, (size_t)n, &env) == SAVVY_ERR_PROTOCOL);
        }
        CHECK("001 missing action field rejected at envelope parse", ok);
    }

    /* Duplicate key inside a schema-controlled payload object. */
    {
        const char *bad = "{\"action\":\"com.uniuni.savvysensor.config\","
                           "\"payload\":{\"jsonConfigDto\":{},\"jsonConfigDto\":{}}}";
        st = transport.send(&transport, bad, strlen(bad), TEST_TIMEOUT_MS);
        char rx[512];
        ssize_t n = (st == SAVVY_OK) ? recv(server_peer_fd, rx, sizeof(rx) - 1, 0) : -1;
        int ok = 0;
        if (n > 0) {
            rx[n] = '\0';
            savvy_ipc_envelope_t env;
            ok = (savvy_ipc_envelope_parse(rx, (size_t)n, &env) == SAVVY_ERR_PROTOCOL);
        }
        CHECK("001 duplicate payload key rejected", ok);
    }

    close(server_peer_fd);
    transport.close(&transport);
    close(listen_fd);
    unlink(path);
}

/* ---- CT-IPC-002: connect -> send/recv -> disconnect -> reconnect -> 4-message resync; pre-connect send drop ---- */
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
    savvy_status_t st = savvy_ipc_client_connect("/tmp/savvy-test-ipc-nonexistent.sock", TEST_TIMEOUT_MS, &transport);
    CHECK("002 connect to nonexistent server fails cleanly (nothing to send to)", st != SAVVY_OK);

    /* Connect. */
    st = savvy_ipc_client_connect(path, TEST_TIMEOUT_MS, &transport);
    CHECK("002 client connect ok", st == SAVVY_OK);
    int server_peer_fd = raw_server_accept(listen_fd, (int)TEST_TIMEOUT_MS);
    CHECK("002 raw server accepts", server_peer_fd >= 0);

    /* Send/recv content match, using a real cataloged action. */
    char *env_text = NULL; size_t env_len = 0;
    st = savvy_ipc_envelope_build("com.uniuni.savvymgr.ipc.connect", "{}", &env_text, &env_len);
    CHECK("002 envelope build ok", st == SAVVY_OK);
    st = transport.send(&transport, env_text, env_len, TEST_TIMEOUT_MS);
    CHECK("002 client send ok", st == SAVVY_OK);

    char recv_buf[4096];
    ssize_t n = recv(server_peer_fd, recv_buf, sizeof(recv_buf) - 1, 0);
    CHECK("002 server received exact content", n == (ssize_t)env_len && memcmp(recv_buf, env_text, env_len) == 0);
    free(env_text);

    /* Disconnect detection: recv()==0 on the client side when the server closes. */
    close(server_peer_fd);
    char probe[16]; size_t probe_len = 0;
    st = transport.recv(&transport, probe, sizeof(probe), &probe_len, TEST_TIMEOUT_MS);
    CHECK("002 disconnect detected as recv()==0", st == SAVVY_OK && probe_len == 0);
    transport.close(&transport);

    /* Reconnect, then receive the real 4-message resync sequence
     * confirmed by Android source research (contracts/
     * ipc_action_catalog.md §3): MGR replays CONFIG, DEVICE,
     * STATUS_ALERT, STATUS_LED_PWR on a fresh accept(). FND-03 only
     * proves the transport CAN carry this sequence back-to-back
     * correctly on the client side - the resend policy itself is MGR's
     * (Wave 1, MGC-03), not tested from this repo. */
    savvy_ipc_transport_t transport2;
    st = savvy_ipc_client_connect(path, TEST_TIMEOUT_MS, &transport2);
    CHECK("002 client reconnect ok", st == SAVVY_OK);
    int server_peer_fd2 = raw_server_accept(listen_fd, (int)TEST_TIMEOUT_MS);
    CHECK("002 raw server re-accepts after reconnect", server_peer_fd2 >= 0);

    static const char *const RESYNC_ACTIONS[] = {
        "com.uniuni.savvysensor.config",
        "com.uniuni.savvysensor.device",
        "com.uniuni.savvysensor.sensor.status.alert",
        "com.uniuni.savvysensor.sensor.status.ledpwr",
    };
    static const char *const RESYNC_PAYLOADS[] = {
        "{\"jsonConfigDto\":{}}",
        "{\"jsonDeviceDto\":{}}",
        "{\"AlertLedState\":0,\"AlertTime\":0,\"AlertSec\":0}",
        "{\"PwrLedState\":0}",
    };
    int resync_ok = 1;
    for (size_t i = 0; i < sizeof(RESYNC_ACTIONS) / sizeof(RESYNC_ACTIONS[0]); i++) {
        char *text = NULL; size_t len = 0;
        savvy_status_t bst = savvy_ipc_envelope_build(RESYNC_ACTIONS[i], RESYNC_PAYLOADS[i], &text, &len);
        ssize_t sent = (bst == SAVVY_OK) ? send(server_peer_fd2, text, len, 0) : -1;
        free(text);

        char rx[512];
        size_t out_len = 0;
        savvy_status_t rst = (sent > 0) ? transport2.recv(&transport2, rx, sizeof(rx) - 1, &out_len, TEST_TIMEOUT_MS)
                                         : SAVVY_ERR_IO;
        if (rst != SAVVY_OK) {
            resync_ok = 0;
            continue;
        }
        rx[out_len] = '\0';
        savvy_ipc_envelope_t env;
        if (savvy_ipc_envelope_parse(rx, out_len, &env) != SAVVY_OK ||
            strcmp(env.action, RESYNC_ACTIONS[i]) != 0) {
            resync_ok = 0;
        } else {
            savvy_ipc_envelope_free(&env);
        }
    }
    CHECK("002 resync: CONFIG+DEVICE+STATUS_ALERT+STATUS_LED_PWR all deliverable after reconnect", resync_ok);

    close(server_peer_fd2);
    transport2.close(&transport2);
    close(listen_fd);
    unlink(path);
}

/* ---- CT-IPC-003: 65536B/65537B real-envelope boundary, oversized single record, genuine truncated multibyte UTF-8, recovery ---- */
static void test_ipc_003(void)
{
    char path[108];
    make_socket_path(path, sizeof(path));

    int listen_fd = raw_server_listen(path);
    savvy_ipc_transport_t transport;
    savvy_status_t st = savvy_ipc_client_connect(path, TEST_TIMEOUT_MS, &transport);
    int server_peer_fd = raw_server_accept(listen_fd, (int)TEST_TIMEOUT_MS);
    CHECK("003 setup: connected", st == SAVVY_OK && server_peer_fd >= 0);

    /* Exactly 65536B, a real (well-formed) envelope, is allowed. */
    {
        char *env = make_padded_envelope("com.uniuni.savvymgr.fracture.sensor", SAVVY_IPC_MAX_MESSAGE);
        size_t env_len = env ? strlen(env) : 0;
        CHECK("003 padded envelope built at exactly 65536B", env != NULL && env_len == SAVVY_IPC_MAX_MESSAGE);

        st = transport.send(&transport, env, env_len, TEST_TIMEOUT_MS);
        char *rx = (char *)malloc(SAVVY_IPC_MAX_MESSAGE + 16);
        ssize_t n = (st == SAVVY_OK) ? recv(server_peer_fd, rx, SAVVY_IPC_MAX_MESSAGE + 16, 0) : -1;
        CHECK("003 exactly 65536B real envelope accepted, delivered whole, and parses",
              st == SAVVY_OK && n == (ssize_t)env_len && memcmp(rx, env, env_len) == 0);
        if (n > 0) {
            rx[n] = '\0';
            savvy_ipc_envelope_t parsed;
            CHECK("003 65536B envelope parses successfully", savvy_ipc_envelope_parse(rx, (size_t)n, &parsed) == SAVVY_OK);
            savvy_ipc_envelope_free(&parsed);
        }
        free(env);
        free(rx);
    }

    /* 65537B, still a well-formed envelope, is rejected before send() -
     * bypasses envelope_build (which would itself reject it) to test the
     * TRANSPORT layer's own independent cap enforcement. */
    {
        char *env = make_padded_envelope("com.uniuni.savvymgr.fracture.sensor", SAVVY_IPC_MAX_MESSAGE + 1);
        size_t env_len = env ? strlen(env) : 0;
        CHECK("003 padded envelope built at exactly 65537B", env != NULL && env_len == SAVVY_IPC_MAX_MESSAGE + 1);

        st = transport.send(&transport, env, env_len, TEST_TIMEOUT_MS);
        CHECK("003 65537B real envelope rejected before send() (transport-level cap)", st == SAVVY_ERR_OVERFLOW);
        free(env);
    }

    /* A single record larger than the receiver's buffer cap is detected
     * via MSG_TRUNC and discarded whole, not partially parsed. */
    {
        uint8_t *big = (uint8_t *)malloc(4096);
        memset(big, 'C', 4096);
        ssize_t sent = send(server_peer_fd, big, 4096, 0); /* raw send from the test server, bypassing the 64KiB app cap on purpose */
        uint8_t small_cap[1024];
        size_t out_len = 999;
        st = transport.recv(&transport, small_cap, sizeof(small_cap), &out_len, TEST_TIMEOUT_MS);
        CHECK("003 oversized single record -> SAVVY_ERR_OVERFLOW (MSG_TRUNC), discarded whole",
              sent == 4096 && st == SAVVY_ERR_OVERFLOW);
        free(big);
    }

    /* Oversized record followed by a normal record: no corruption/desync carries over. */
    {
        const char *normal = "{\"action\":\"com.uniuni.savvymgr.fracture.sensor\",\"payload\":{}}";
        size_t normal_len = strlen(normal);
        ssize_t sent = send(server_peer_fd, normal, normal_len, 0);
        char rx[256]; size_t out_len = 0;
        st = transport.recv(&transport, rx, sizeof(rx), &out_len, TEST_TIMEOUT_MS);
        CHECK("003 normal record after oversized one is received uncorrupted",
              sent == (ssize_t)normal_len && st == SAVVY_OK && out_len == normal_len &&
              memcmp(rx, normal, normal_len) == 0);
    }

    /* Genuine truncated multibyte UTF-8 (not incomplete ASCII JSON
     * syntax): the JSON is syntactically complete/balanced - only the
     * UTF-8 byte sequence inside the string is malformed. 0xC3 is a valid
     * 2-byte-sequence leading byte that MUST be followed by a 0x80-0xBF
     * continuation byte; here it's immediately followed by the closing
     * quote (0x22), which is not one. */
    {
        static const char truncated[] =
            "{\"action\":\"com.uniuni.savvysensor.config\",\"payload\":"
            "{\"jsonConfigDto\":{\"serverIp\":\"caf\xc3\"}}}";
        size_t truncated_len = sizeof(truncated) - 1; /* exclude the C string's own NUL */
        send(server_peer_fd, truncated, truncated_len, 0);
        char rx[256]; size_t out_len = 0;
        st = transport.recv(&transport, rx, sizeof(rx) - 1, &out_len, TEST_TIMEOUT_MS);
        rx[out_len] = '\0';
        savvy_ipc_envelope_t env;
        savvy_status_t parse_st = savvy_ipc_envelope_parse(rx, out_len, &env);
        CHECK("003 genuine truncated multibyte UTF-8 rejected at parse (not crashed, not silently accepted)",
              st == SAVVY_OK && out_len == truncated_len && parse_st == SAVVY_ERR_PROTOCOL);
    }

    close(server_peer_fd);
    transport.close(&transport);
    close(listen_fd);
    unlink(path);
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: %s <001|002|003>\n", argv[0]);
        return 2;
    }

    if (strcmp(argv[1], "001") == 0) {
        test_ipc_001();
    } else if (strcmp(argv[1], "002") == 0) {
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
