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
#include <pthread.h>
#include <signal.h>
#include <time.h>
#include <sys/wait.h>
#include "savvy/platform/ipc_client.h"
#include "savvy/platform/ipc_reconnect.h"
#include "savvy/platform/ipc_cancel.h"
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
    /* PwrLedState/AlertLedState/AlertTime/AlertSec/STATE/IFCOMM_START/
     * DELAY_SEC are Android Bundle Strings on the wire (V0R-H-01) - valid
     * payloads use JSON strings; the mismatch variant is a JSON number,
     * which is now the type that must be REJECTED (the inverse of what
     * these vectors tested before the fix). */
    { "com.uniuni.savvysensor.sensor.status.ledpwr", "{\"PwrLedState\":\"1\"}", "{\"PwrLedState\":1}" },
    { "com.uniuni.savvysensor.sensor.status.alert",
      "{\"AlertLedState\":\"1\",\"AlertTime\":\"2\",\"AlertSec\":\"3\"}",
      "{\"AlertLedState\":1,\"AlertTime\":\"2\",\"AlertSec\":\"3\"}" },
    { "com.uniuni.savvysensor.sensor.rknn.alert", "{}", NULL },
    { "com.uniuni.savvysensor.sensor.update.threash.hold", "{}", NULL },
    { "com.uniuni.savvysensor.sensor.rknn.anal.result", "{\"rknnAnalResult\":\"RET:level03\"}", "{\"rknnAnalResult\":42}" },
    { "com.uniuni.savvysensor.sensor.max.cpu.temp", "{}", NULL },
    { "com.uniuni.savvymgr.ipc.connect", "{}", NULL },
    { "com.uniuni.savvymgr.getstate.sensor", "{\"SENSOR\":\"MIC\",\"STATE\":\"1\"}", "{\"SENSOR\":\"MIC\",\"STATE\":1}" },
    { "com.uniuni.savvymgr.alert.sensor", "{\"IFCOMM_START\":\"1\"}", "{\"IFCOMM_START\":1}" },
    { "com.uniuni.savvymgr.upload.sensor",
      "{\"targetFilePath\":\"/base/x\",\"targetFileNm\":\"y\"}",
      "{\"targetFilePath\":1,\"targetFileNm\":\"y\"}" },
    { "com.uniuni.savvymgr.restart.sensor", "{\"DELAY_SEC\":\"5\"}", "{\"DELAY_SEC\":5}" },
    { "com.uniuni.savvymgr.fracture.sensor", "{}", NULL },
    { "com.uniuni.savvymgr.tof.property", "{}", NULL }, /* all 4 keys optional+nullable - see dedicated matrix below */
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

    /* PROPERTY_BROADCAST_TOF's four keys are both optional (may be
     * missing) AND nullable (may be present as JSON null) - V0R-H-01
     * requires these as four independently distinguishable cases, which
     * the generic valid/type-mismatch pair above can't express. Checked
     * directly against the catalog validator (no transport round-trip
     * needed - this is purely catalog/type-validation logic). */
    {
        const char *action = "com.uniuni.savvymgr.tof.property";
        savvy_status_t s;

        s = savvy_ipc_action_validate_payload(action, "{}");
        CHECK("001 TOF: optional key missing -> OK", s == SAVVY_OK);

        s = savvy_ipc_action_validate_payload(action,
            "{\"TofTemperature\":null,\"TofTemperDrv\":null,\"SmokeValue\":null,\"MicValue\":null}");
        CHECK("001 TOF: nullable key present as null -> OK", s == SAVVY_OK);

        s = savvy_ipc_action_validate_payload(action, "{\"TofTemperature\":42}");
        CHECK("001 TOF: optional key present with valid type -> OK", s == SAVVY_OK);

        s = savvy_ipc_action_validate_payload(action, "{\"TofTemperature\":\"42\"}");
        CHECK("001 TOF: optional key present with invalid (non-null) type -> reject",
              s == SAVVY_ERR_PROTOCOL);
    }

    /* A required, non-nullable key (PwrLedState) present as JSON null must
     * be rejected - distinct from a required key being absent entirely
     * (already covered: no catalog action has both required=true and
     * nullable=true, so this is the general "non-nullable key is null"
     * case using one representative field). */
    {
        savvy_status_t s = savvy_ipc_action_validate_payload(
            "com.uniuni.savvysensor.sensor.status.ledpwr", "{\"PwrLedState\":null}");
        CHECK("001 non-nullable required key (PwrLedState) present as null -> reject",
              s == SAVVY_ERR_PROTOCOL);
    }

    /* Payload root itself is not a JSON object at all (bare string/array),
     * as opposed to a nested key having the wrong type. */
    {
        savvy_status_t s = savvy_ipc_action_validate_payload(
            "com.uniuni.savvysensor.serverip", "\"not-an-object\"");
        CHECK("001 payload root not an object (bare string) -> reject", s == SAVVY_ERR_PROTOCOL);

        s = savvy_ipc_action_validate_payload("com.uniuni.savvysensor.serverip", "[1,2,3]");
        CHECK("001 payload root not an object (array) -> reject", s == SAVVY_ERR_PROTOCOL);
    }

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

/* F-07: mock replay recorder for savvy_ipc_reconnect_tracker_*. Records
 * call counts and relative order (via a shared monotonic index) rather
 * than actually resending anything - proving the hook itself fires
 * correctly is decoupled from proving the transport can carry the actual
 * replay messages (already covered by the RESYNC_ACTIONS loop below). */
typedef struct reconnect_recorder {
    int config_calls;
    int device_calls;
    int config_call_index;  /* 0 = never called this round */
    int device_call_index;
    int next_index;
    savvy_ipc_transport_t *already_closed_transport; /* used to simulate a callback-internal failure */
    savvy_status_t simulated_failure_status;
} reconnect_recorder_t;

static void recorder_request_config(void *user_data)
{
    reconnect_recorder_t *r = (reconnect_recorder_t *)user_data;
    r->config_calls++;
    r->config_call_index = ++r->next_index;
    /* Simulate a real, internal callback failure (e.g. the actual resend
     * racing a second disconnect) by sending through an already-closed
     * transport - this must return an error, not crash, and the tracker/
     * caller must keep going regardless (F-07: "콜백 실패 시 process crash
     * 없음"). */
    if (r->already_closed_transport != NULL) {
        r->simulated_failure_status = r->already_closed_transport->send(
            r->already_closed_transport, "x", 1, TEST_TIMEOUT_MS);
    }
}

static void recorder_request_device(void *user_data)
{
    reconnect_recorder_t *r = (reconnect_recorder_t *)user_data;
    r->device_calls++;
    r->device_call_index = ++r->next_index;
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

    /* F-07: the FIRST successful connect is normal startup, not a
     * reconnect - the replay hook must not fire. */
    savvy_ipc_reconnect_tracker_t reconnect_tracker;
    savvy_ipc_reconnect_tracker_init(&reconnect_tracker);
    reconnect_recorder_t recorder;
    memset(&recorder, 0, sizeof(recorder));
    savvy_ipc_reconnect_hooks_t hooks = { recorder_request_config, recorder_request_device, &recorder };
    savvy_ipc_reconnect_tracker_on_connected(&reconnect_tracker, &hooks);
    CHECK("002 F-07: first connect does not fire the replay hook",
          recorder.config_calls == 0 && recorder.device_calls == 0);

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

    /* F-07: reconnect must fire the replay hook exactly once, Config
     * before Device. The recorder's request_config callback also
     * exercises a real internal failure (a send() through the
     * already-closed first `transport`) to prove a callback failure
     * doesn't crash the process or block the hook from completing. */
    recorder.already_closed_transport = &transport;
    savvy_ipc_reconnect_tracker_on_connected(&reconnect_tracker, &hooks);
    CHECK("002 F-07: reconnect fires the replay hook exactly once",
          recorder.config_calls == 1 && recorder.device_calls == 1);
    CHECK("002 F-07: replay hook calls Config before Device",
          recorder.config_call_index > 0 && recorder.device_call_index > 0 &&
          recorder.config_call_index < recorder.device_call_index);
    CHECK("002 F-07: callback-internal failure (send on closed transport) reported, not crashed",
          recorder.simulated_failure_status != SAVVY_OK);

    static const char *const RESYNC_ACTIONS[] = {
        "com.uniuni.savvysensor.config",
        "com.uniuni.savvysensor.device",
        "com.uniuni.savvysensor.sensor.status.alert",
        "com.uniuni.savvysensor.sensor.status.ledpwr",
    };
    static const char *const RESYNC_PAYLOADS[] = {
        "{\"jsonConfigDto\":{}}",
        "{\"jsonDeviceDto\":{}}",
        "{\"AlertLedState\":\"0\",\"AlertTime\":\"0\",\"AlertSec\":\"0\"}",
        "{\"PwrLedState\":\"0\"}",
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

    /* F-07: a SECOND reconnect must fire the hook again (once each), not
     * just the first time - and the transport carrying the resync
     * messages above (after the first reconnect's hook fired, including
     * its simulated internal failure) must still be fully functional,
     * which the resync check just above already proved. */
    close(server_peer_fd2);
    char probe2[16];
    size_t probe2_len = 0;
    st = transport2.recv(&transport2, probe2, sizeof(probe2), &probe2_len, TEST_TIMEOUT_MS);
    CHECK("002 second disconnect detected as recv()==0", st == SAVVY_OK && probe2_len == 0);
    transport2.close(&transport2);

    savvy_ipc_transport_t transport3;
    st = savvy_ipc_client_connect(path, TEST_TIMEOUT_MS, &transport3);
    CHECK("002 client second reconnect ok", st == SAVVY_OK);
    int server_peer_fd3 = raw_server_accept(listen_fd, (int)TEST_TIMEOUT_MS);
    CHECK("002 raw server re-accepts after second reconnect", server_peer_fd3 >= 0);

    recorder.already_closed_transport = NULL;
    savvy_ipc_reconnect_tracker_on_connected(&reconnect_tracker, &hooks);
    CHECK("002 F-07: second reconnect fires the replay hook again, once each (not just the first time)",
          recorder.config_calls == 2 && recorder.device_calls == 2);

    close(server_peer_fd3);
    transport3.close(&transport3);
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

    /* F-08: independent of the client-side send() guard just above, the
     * RECEIVER's own 64KiB global cap must catch an oversized record even
     * when the caller's receive buffer is large enough that MSG_TRUNC
     * would NOT be what catches it - i.e. the cap is its own check, not
     * merely a side effect of a too-small caller buffer (this is the
     * F-06/M-05 fix). Raw send bypasses the client-side guard entirely
     * (same technique as the MSG_TRUNC case below), but this time with a
     * receiver buffer deliberately larger than the oversized record
     * itself. */
    {
        size_t oversized_len = SAVVY_IPC_MAX_MESSAGE + 1;
        uint8_t *big = (uint8_t *)malloc(oversized_len);
        memset(big, 'D', oversized_len);
        ssize_t sent = send(server_peer_fd, big, oversized_len, 0); /* raw send from the test server, bypassing the 64KiB app cap on purpose */
        uint8_t *big_cap = (uint8_t *)malloc(oversized_len + 1024); /* deliberately bigger than the record itself */
        size_t out_len = 999;
        st = transport.recv(&transport, big_cap, oversized_len + 1024, &out_len, TEST_TIMEOUT_MS);
        CHECK("003 65537B raw record rejected by the receiver's OWN global cap even with a large-enough buffer (not merely MSG_TRUNC)",
              sent == (ssize_t)oversized_len && st == SAVVY_ERR_OVERFLOW);
        free(big);
        free(big_cap);
    }

    /* And exactly like the MSG_TRUNC case below, a normal record sent
     * right after must still be received uncorrupted. */
    {
        const char *normal = "{\"action\":\"com.uniuni.savvymgr.fracture.sensor\",\"payload\":{}}";
        size_t normal_len = strlen(normal);
        ssize_t sent = send(server_peer_fd, normal, normal_len, 0);
        char rx[256]; size_t out_len = 0;
        st = transport.recv(&transport, rx, sizeof(rx), &out_len, TEST_TIMEOUT_MS);
        CHECK("003 normal record after the receiver-side-cap-rejected one is received uncorrupted",
              sent == (ssize_t)normal_len && st == SAVVY_OK && out_len == normal_len &&
              memcmp(rx, normal, normal_len) == 0);
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

/* ---- Supplementary (not one of the 8 required CT-* tests): V0B-H-02
 * strict cancellable I/O. Registered under CTest name CT-IPC-CANCEL (not
 * CT-IPC-004) specifically so it can never be mistaken for a renumbering
 * of, or replacement for, the required CT-IPC-001~003. ---- */

/* Blocks (via pthread_join with a bounded wait) instead of hanging the
 * whole suite forever if a fix regresses - a join that doesn't complete
 * within timeout_ms is itself the test failure, not a hang. */
static int join_with_timeout(pthread_t thread, int timeout_ms)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += timeout_ms / 1000;
    ts.tv_nsec += (long)(timeout_ms % 1000) * 1000000L;
    if (ts.tv_nsec >= 1000000000L) {
        ts.tv_sec += 1;
        ts.tv_nsec -= 1000000000L;
    }
    return pthread_timedjoin_np(thread, NULL, &ts);
}

static uint64_t wall_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL;
}

typedef struct blocked_call_result {
    savvy_ipc_transport_t *transport;
    savvy_status_t status;
    size_t out_len;
} blocked_call_result_t;

static void *blocked_recv_thread(void *arg)
{
    blocked_call_result_t *r = (blocked_call_result_t *)arg;
    char buf[64];
    /* 30s: must be woken by the main thread's close(), not by this
     * elapsing - if the fix regresses, join_with_timeout() below fails
     * long before this would. */
    r->status = r->transport->recv(r->transport, buf, sizeof(buf), &r->out_len, 30000u);
    return NULL;
}

static void *blocked_send_thread(void *arg)
{
    blocked_call_result_t *r = (blocked_call_result_t *)arg;
    char payload[512];
    memset(payload, 'S', sizeof(payload));
    /* Keep sending until send() itself reports a non-OK status (either
     * because the (small, deliberately-shrunk) send buffer is genuinely
     * full and this particular call is the one that ends up blocked when
     * the peer closes, or because an earlier call already failed) -
     * whichever happens, the LAST attempted status is what matters. */
    for (int i = 0; i < 100000; i++) {
        r->status = r->transport->send(r->transport, payload, sizeof(payload), 30000u);
        if (r->status != SAVVY_OK) {
            break;
        }
    }
    return NULL;
}

typedef struct blocked_connect_result {
    const char *path;
    const savvy_ipc_cancel_source_t *cancel;
    savvy_status_t status;
} blocked_connect_result_t;

static void *blocked_connect_thread(void *arg)
{
    blocked_connect_result_t *r = (blocked_connect_result_t *)arg;
    savvy_ipc_transport_t transport;
    r->status = savvy_ipc_client_connect_cancelable(r->path, 30000u, r->cancel, &transport);
    if (r->status == SAVVY_OK) {
        transport.close(&transport);
    }
    return NULL;
}

static void noop_signal_handler(int sig) { (void)sig; }

static void test_ipc_004(void)
{
    /* A: a thread blocked in recv() wakes promptly when another thread
     * closes the same transport (V0B-H-02 "read 대기 중 close/cancel"). */
    {
        char path[108];
        make_socket_path(path, sizeof(path));
        int listen_fd = raw_server_listen(path);
        savvy_ipc_transport_t transport;
        savvy_status_t st = savvy_ipc_client_connect(path, TEST_TIMEOUT_MS, &transport);
        int server_peer_fd = raw_server_accept(listen_fd, (int)TEST_TIMEOUT_MS);
        CHECK("004A setup: connected", st == SAVVY_OK && server_peer_fd >= 0);

        blocked_call_result_t result = { &transport, SAVVY_ERR_UNKNOWN, 999 };
        pthread_t th;
        pthread_create(&th, NULL, blocked_recv_thread, &result);
        struct timespec settle = { 0, 100000000L }; /* 100ms: let the thread actually enter recv() */
        nanosleep(&settle, NULL);

        transport.close(&transport);
        int joined = join_with_timeout(th, 2000);
        CHECK("004A blocked reader wakes promptly on another thread's close() (not after the full 30s timeout)",
              joined == 0);
        if (joined != 0) {
            pthread_cancel(th);
            pthread_join(th, NULL);
        }

        close(server_peer_fd);
        close(listen_fd);
        unlink(path);
    }

    /* B: a thread blocked in send() (send buffer deliberately shrunk and
     * filled, peer never draining) wakes promptly when another thread
     * closes the same transport (V0B-H-02 "write 대기 중 close/cancel"). */
    {
        char path[108];
        make_socket_path(path, sizeof(path));
        int listen_fd = raw_server_listen(path);
        savvy_ipc_transport_t transport;
        savvy_status_t st = savvy_ipc_client_connect(path, TEST_TIMEOUT_MS, &transport);
        int server_peer_fd = raw_server_accept(listen_fd, (int)TEST_TIMEOUT_MS);
        CHECK("004B setup: connected", st == SAVVY_OK && server_peer_fd >= 0);

        int fd = (int)(intptr_t)transport.impl;
        int small_buf = 1024;
        setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &small_buf, sizeof(small_buf));

        blocked_call_result_t result = { &transport, SAVVY_ERR_UNKNOWN, 0 };
        pthread_t th;
        pthread_create(&th, NULL, blocked_send_thread, &result);
        struct timespec settle = { 0, 200000000L }; /* 200ms: let the sender genuinely fill the buffer and block */
        nanosleep(&settle, NULL);

        transport.close(&transport);
        int joined = join_with_timeout(th, 2000);
        CHECK("004B blocked writer wakes promptly on another thread's close() (not after the full 30s timeout)",
              joined == 0);
        CHECK("004B blocked writer's send() reports a failure, not a stale success",
              joined == 0 && result.status != SAVVY_OK);
        if (joined != 0) {
            pthread_cancel(th);
            pthread_join(th, NULL);
        }

        close(server_peer_fd);
        close(listen_fd);
        unlink(path);
    }

    /* C: connect() cancellation (V0B-H-02 "connect 대기 중 cancel") - this
     * repo owns the CLIENT role, so savvy_ipc_client_connect_cancelable is
     * exactly the production function this exercises (mgr_to_Linux's
     * counterpart tests savvy_ipc_server_accept_cancelable instead, since
     * it owns the SERVER role, and proves the SAME shared
     * savvy_ipc_poll_with_deadline_cancelable() primitive genuinely
     * interrupts a peer-less, indefinitely-blocked wait).
     *
     * Two sub-cases, because AF_UNIX loopback connect() completes near-
     * instantaneously in practice - empirically, exhausting
     * raw_server_listen's backlog(1) with one un-accepted connection does
     * NOT reliably make a second connect() block long enough to race a
     * cancellation delivered ~200ms later (observed directly against this
     * Docker image: the second connect legitimately succeeds first). So:
     *   C1 (best-effort, not a hard failure either way): attempt the
     *       backlog-exhaustion scenario anyway and confirm it never hangs
     *       and always resolves to a definite status - documents the
     *       above finding rather than asserting a specific status.
     *   C2 (deterministic, the real assertion for this finding): cancel
     *       the source BEFORE calling connect_cancelable() at all, against
     *       a listener that WOULD otherwise accept it fine - this exactly
     *       exercises the shared poll_with_deadline_cancelable's
     *       cancel-fd check without depending on any OS-specific blocking
     *       timing, and must return SAVVY_ERR_CANCELLED, quickly, every
     *       time. */
    {
        char path[108];
        make_socket_path(path, sizeof(path));
        int listen_fd = raw_server_listen(path);
        CHECK("004C setup: raw server listening", listen_fd >= 0);

        savvy_ipc_transport_t occupying_transport;
        savvy_status_t occ_st = savvy_ipc_client_connect(path, TEST_TIMEOUT_MS, &occupying_transport);
        CHECK("004C setup: first connect occupies the only backlog slot", occ_st == SAVVY_OK);

        savvy_ipc_cancel_source_t cancel;
        savvy_status_t cs_st = savvy_ipc_cancel_source_init(&cancel);
        CHECK("004C cancel source created", cs_st == SAVVY_OK);

        blocked_connect_result_t result = { path, &cancel, SAVVY_ERR_UNKNOWN };
        pthread_t th;
        pthread_create(&th, NULL, blocked_connect_thread, &result);
        struct timespec settle = { 0, 200000000L };
        nanosleep(&settle, NULL);

        savvy_ipc_cancel_source_cancel(&cancel);
        int joined = join_with_timeout(th, 2000);
        CHECK("004C1 backlog-exhaustion connect attempt never hangs regardless of whether it wins the race "
              "(not after the full 30s timeout)", joined == 0);
        if (joined != 0) {
            pthread_cancel(th);
            pthread_join(th, NULL);
        } else if (result.status != SAVVY_OK) {
            /* If it didn't just succeed outright (this environment's
             * observed behavior), it must specifically be CANCELLED, not
             * some other unrelated failure. */
            CHECK("004C1 non-successful backlog-exhaustion connect is specifically SAVVY_ERR_CANCELLED",
                  result.status == SAVVY_ERR_CANCELLED);
        }

        savvy_ipc_cancel_source_destroy(&cancel);
        occupying_transport.close(&occupying_transport);
        close(listen_fd);
        unlink(path);
    }
    {
        char path[108];
        make_socket_path(path, sizeof(path));
        int listen_fd = raw_server_listen(path);
        CHECK("004C2 setup: raw server listening", listen_fd >= 0);

        savvy_ipc_cancel_source_t cancel;
        savvy_status_t cs_st = savvy_ipc_cancel_source_init(&cancel);
        savvy_ipc_cancel_source_cancel(&cancel); /* cancelled BEFORE the call even starts */
        CHECK("004C2 cancel source created and pre-cancelled", cs_st == SAVVY_OK);

        savvy_ipc_transport_t transport;
        uint64_t before = wall_ms();
        savvy_status_t st = savvy_ipc_client_connect_cancelable(path, 30000u, &cancel, &transport);
        uint64_t elapsed = wall_ms() - before;
        CHECK("004C2 already-cancelled source makes connect_cancelable return SAVVY_ERR_CANCELLED promptly "
              "(not after 30s, and not a successful connect)",
              st == SAVVY_ERR_CANCELLED && elapsed < 2000);

        savvy_ipc_cancel_source_destroy(&cancel);
        close(listen_fd);
        unlink(path);
    }

    /* D: with nothing cancelling and nothing sent, a bounded recv() still
     * times out at roughly the requested deadline (V0B-H-02 "timeout
     * 발생") - a regression that made everything wait for cancellation
     * only, and broke the plain timeout path, would show up here. */
    {
        char path[108];
        make_socket_path(path, sizeof(path));
        int listen_fd = raw_server_listen(path);
        savvy_ipc_transport_t transport;
        savvy_status_t st = savvy_ipc_client_connect(path, TEST_TIMEOUT_MS, &transport);
        int server_peer_fd = raw_server_accept(listen_fd, (int)TEST_TIMEOUT_MS);
        CHECK("004D setup: connected", st == SAVVY_OK && server_peer_fd >= 0);

        char buf[16]; size_t out_len = 999;
        uint64_t before = wall_ms();
        st = transport.recv(&transport, buf, sizeof(buf), &out_len, 400u);
        uint64_t elapsed = wall_ms() - before;
        CHECK("004D plain timeout still returns SAVVY_ERR_TIMEOUT at roughly the requested deadline",
              st == SAVVY_ERR_TIMEOUT && elapsed >= 350 && elapsed < 2000);

        close(server_peer_fd);
        transport.close(&transport);
        close(listen_fd);
        unlink(path);
    }

    /* E: repeated EINTR must not reset the deadline from scratch each
     * time (V0B-H-02 "반복 EINTR에도 absolute deadline 초과 금지"). A
     * no-op-handled signal delivered every ~50ms for ~2s would keep
     * pushing a naively-restarted wait far past the 500ms this call
     * actually asked for; the absolute-deadline design must still return
     * at roughly 500ms regardless. SA_RESTART is deliberately NOT set, so
     * every delivered signal is guaranteed to actually interrupt poll(). */
    {
        char path[108];
        make_socket_path(path, sizeof(path));
        int listen_fd = raw_server_listen(path);
        savvy_ipc_transport_t transport;
        savvy_status_t st = savvy_ipc_client_connect(path, TEST_TIMEOUT_MS, &transport);
        int server_peer_fd = raw_server_accept(listen_fd, (int)TEST_TIMEOUT_MS);
        CHECK("004E setup: connected", st == SAVVY_OK && server_peer_fd >= 0);

        struct sigaction sa;
        memset(&sa, 0, sizeof(sa));
        sa.sa_handler = noop_signal_handler;
        sa.sa_flags = 0; /* explicitly no SA_RESTART */
        sigaction(SIGUSR1, &sa, NULL);

        /* A forked child (not another pthread) sends the signals: at this
         * point in the test the parent process is single-threaded again
         * (previous sub-tests already joined their threads), so a
         * process-directed signal is unambiguously delivered to the one
         * thread blocked in recv() below - no thread-targeting needed. */
        pid_t child = fork();
        if (child == 0) {
            for (int i = 0; i < 40; i++) {
                kill(getppid(), SIGUSR1);
                struct timespec iv = { 0, 50000000L };
                nanosleep(&iv, NULL);
            }
            _exit(0);
        }

        char buf[16]; size_t out_len = 999;
        uint64_t before = wall_ms();
        st = transport.recv(&transport, buf, sizeof(buf), &out_len, 500u);
        uint64_t elapsed = wall_ms() - before;
        CHECK("004E repeated EINTR does not reset the absolute deadline (returns at ~500ms, not ~2s+)",
              st == SAVVY_ERR_TIMEOUT && elapsed >= 450 && elapsed < 1500);

        int wstatus = 0;
        waitpid(child, &wstatus, 0);

        close(server_peer_fd);
        transport.close(&transport);
        close(listen_fd);
        unlink(path);
    }

    /* F: close() is idempotent when called twice in a row. */
    {
        char path[108];
        make_socket_path(path, sizeof(path));
        int listen_fd = raw_server_listen(path);
        savvy_ipc_transport_t transport;
        savvy_status_t st = savvy_ipc_client_connect(path, TEST_TIMEOUT_MS, &transport);
        int server_peer_fd = raw_server_accept(listen_fd, (int)TEST_TIMEOUT_MS);
        CHECK("004F setup: connected", st == SAVVY_OK && server_peer_fd >= 0);

        transport.close(&transport);
        transport.close(&transport); /* must not crash / double-free / double-close */
        char buf[16]; size_t out_len = 0;
        st = transport.recv(&transport, buf, sizeof(buf), &out_len, TEST_TIMEOUT_MS);
        CHECK("004F double-close is idempotent and post-close recv() reports SAVVY_ERR_CLOSED",
              st == SAVVY_ERR_CLOSED);

        close(server_peer_fd);
        close(listen_fd);
        unlink(path);
    }

    /* G: after a cancelled/closed transport, BOTH (a) a brand new
     * transport instance can still be created normally, and (b) a
     * DIFFERENT, already-established transport instance is entirely
     * unaffected by the first one's failure. */
    {
        char path_a[108], path_b[108];
        snprintf(path_a, sizeof(path_a), "/tmp/savvy-test-ipc-%d-a.sock", (int)getpid());
        snprintf(path_b, sizeof(path_b), "/tmp/savvy-test-ipc-%d-b.sock", (int)getpid());

        int listen_fd_a = raw_server_listen(path_a);
        savvy_ipc_transport_t transport_a;
        savvy_status_t st_a = savvy_ipc_client_connect(path_a, TEST_TIMEOUT_MS, &transport_a);
        int server_peer_fd_a = raw_server_accept(listen_fd_a, (int)TEST_TIMEOUT_MS);

        int listen_fd_b = raw_server_listen(path_b);
        savvy_ipc_transport_t transport_b;
        savvy_status_t st_b = savvy_ipc_client_connect(path_b, TEST_TIMEOUT_MS, &transport_b);
        int server_peer_fd_b = raw_server_accept(listen_fd_b, (int)TEST_TIMEOUT_MS);

        CHECK("004G setup: two independent transports connected",
              st_a == SAVVY_OK && st_b == SAVVY_OK && server_peer_fd_a >= 0 && server_peer_fd_b >= 0);

        /* Kill transport_a hard. */
        transport_a.close(&transport_a);
        close(server_peer_fd_a);

        /* transport_b must still work perfectly. */
        const char *msg = "{\"action\":\"com.uniuni.savvymgr.fracture.sensor\",\"payload\":{}}";
        size_t msg_len = strlen(msg);
        st_b = transport_b.send(&transport_b, msg, msg_len, TEST_TIMEOUT_MS);
        char rx[256];
        ssize_t n = (st_b == SAVVY_OK) ? recv(server_peer_fd_b, rx, sizeof(rx) - 1, 0) : -1;
        CHECK("004G one transport's close/failure does not affect a different transport instance",
              st_b == SAVVY_OK && n == (ssize_t)msg_len && memcmp(rx, msg, msg_len) == 0);

        /* A brand new (third) transport can still be created normally
         * afterward, on yet another path. */
        char path_c[108];
        snprintf(path_c, sizeof(path_c), "/tmp/savvy-test-ipc-%d-c.sock", (int)getpid());
        int listen_fd_c = raw_server_listen(path_c);
        savvy_ipc_transport_t transport_c;
        savvy_status_t st_c = savvy_ipc_client_connect(path_c, TEST_TIMEOUT_MS, &transport_c);
        int server_peer_fd_c = raw_server_accept(listen_fd_c, (int)TEST_TIMEOUT_MS);
        CHECK("004G a brand new transport instance can still be created normally afterward",
              st_c == SAVVY_OK && server_peer_fd_c >= 0);

        close(server_peer_fd_b);
        transport_b.close(&transport_b);
        close(listen_fd_b);
        unlink(path_b);
        close(server_peer_fd_c);
        transport_c.close(&transport_c);
        close(listen_fd_c);
        unlink(path_c);
        close(listen_fd_a);
        unlink(path_a);
    }
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: %s <001|002|003|004>\n", argv[0]);
        return 2;
    }

    if (strcmp(argv[1], "001") == 0) {
        test_ipc_001();
    } else if (strcmp(argv[1], "002") == 0) {
        test_ipc_002();
    } else if (strcmp(argv[1], "003") == 0) {
        test_ipc_003();
    } else if (strcmp(argv[1], "004") == 0) {
        test_ipc_004();
    } else {
        fprintf(stderr, "unknown test id: %s\n", argv[1]);
        return 2;
    }

    printf("\n=== CT-IPC-%s: %d passed, %d failed ===\n", argv[1], g_pass, g_fail);
    return (g_fail == 0) ? 0 : 1;
}
