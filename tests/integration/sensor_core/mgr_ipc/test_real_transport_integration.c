/* CT-IPC-002, real-transport variant: exercises src/features/mgr_ipc's
 * client against Foundation's REAL savvy_ipc_client_connect_cancelable()
 * (AF_UNIX SOCK_SEQPACKET, Linux-only) and a real tools/mock_mgr server
 * subprocess, rather than the fake in-process transport used by
 * tests/unit/sensor_core/mgr_ipc/test_mgr_ipc_client.c. Only buildable
 * when SENSOR_MGR_IPC_REAL_TRANSPORT is ON (see CMakeLists.txt) and a
 * pre-built tools/mock_mgr binary path is supplied via -DMOCK_MGR_BINARY.
 * Never runs on macOS (AF_UNIX SOCK_SEQPACKET is unsupported on Darwin) -
 * this is the Ubuntu 22.04 arm64 Docker verification path. */

/* clock_gettime()/nanosleep() are POSIX.1-2008; glibc hides them under
 * -std=c11 without this (see src/core/clock.c for Foundation's own
 * instance of the same fix). Must be defined before any system header. */
#define _POSIX_C_SOURCE 200809L

#include "mgr_ipc_client.h"
#include "real_connector.h"

#include <assert.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#ifndef MOCK_MGR_BINARY_PATH
#error "MOCK_MGR_BINARY_PATH must be defined - configure with -DMOCK_MGR_BINARY=<path to a pre-built tools/mock_mgr binary>"
#endif

typedef struct test_recorder {
    pthread_mutex_t lock;
    int connect_count;
    int reconnect_count;
    int disconnect_count;
    int action_count;
    char actions[8][128];
} test_recorder_t;

static void recorder_init(test_recorder_t *r) {
    pthread_mutex_init(&r->lock, NULL);
    r->connect_count = 0;
    r->reconnect_count = 0;
    r->disconnect_count = 0;
    r->action_count = 0;
}

static void on_connected_cb(bool was_reconnect, void *ud) {
    test_recorder_t *r = (test_recorder_t *)ud;
    pthread_mutex_lock(&r->lock);
    r->connect_count++;
    if (was_reconnect) {
        r->reconnect_count++;
    }
    pthread_mutex_unlock(&r->lock);
}
static void on_disconnected_cb(void *ud) {
    test_recorder_t *r = (test_recorder_t *)ud;
    pthread_mutex_lock(&r->lock);
    r->disconnect_count++;
    pthread_mutex_unlock(&r->lock);
}
static void on_envelope_cb(const char *action, const char *payload_json, void *ud) {
    (void)payload_json;
    test_recorder_t *r = (test_recorder_t *)ud;
    pthread_mutex_lock(&r->lock);
    if (r->action_count < 8) {
        snprintf(r->actions[r->action_count], sizeof(r->actions[0]), "%s", action);
        r->action_count++;
    }
    pthread_mutex_unlock(&r->lock);
}

static int get_connect_count(test_recorder_t *r) {
    pthread_mutex_lock(&r->lock);
    int v = r->connect_count;
    pthread_mutex_unlock(&r->lock);
    return v;
}
static int get_disconnect_count(test_recorder_t *r) {
    pthread_mutex_lock(&r->lock);
    int v = r->disconnect_count;
    pthread_mutex_unlock(&r->lock);
    return v;
}
static int get_action_count(test_recorder_t *r) {
    pthread_mutex_lock(&r->lock);
    int v = r->action_count;
    pthread_mutex_unlock(&r->lock);
    return v;
}

/* usleep() is XSI/legacy (obsoleted by POSIX.1-2008 in favor of
 * nanosleep()); using nanosleep() directly avoids needing a second,
 * broader feature-test macro alongside _POSIX_C_SOURCE 200809L. */
static void sleep_ms_test(long ms) {
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}

static bool wait_until_ge(int (*getter)(test_recorder_t *), test_recorder_t *r, int expected, int timeout_ms) {
    int elapsed = 0;
    while (elapsed < timeout_ms) {
        if (getter(r) >= expected) {
            return true;
        }
        sleep_ms_test(20);
        elapsed += 20;
    }
    return getter(r) >= expected;
}

int main(void) {
    const char *socket_path = "/tmp/cc-sensor-core-ct-ipc-002-real.sock";
    unlink(socket_path);

    pid_t mgr_pid = fork();
    assert(mgr_pid >= 0);
    if (mgr_pid == 0) {
        /* 2 cycles: first connection held open 1000ms then closed by
         * mock_mgr (forces a disconnect+reconnect), second cycle held
         * open long enough for this test to observe it and shut down
         * cleanly. */
        execl(MOCK_MGR_BINARY_PATH, MOCK_MGR_BINARY_PATH, socket_path, "2", "1000", (char *)NULL);
        _exit(127);
    }

    sleep_ms_test(300); /* let mock_mgr bind()+listen() before connecting */

    sensor_mgr_ipc_real_connector_ctx_t connector_ctx;
    connector_ctx.socket_path = socket_path;

    test_recorder_t recorder;
    recorder_init(&recorder);

    sensor_mgr_ipc_config_t config;
    memset(&config, 0, sizeof(config));
    config.connector = sensor_mgr_ipc_real_connector;
    config.connector_ctx = &connector_ctx;
    config.connect_timeout_ms = 2000;
    config.send_timeout_ms = 500;
    config.recv_poll_timeout_ms = 150;
    config.reconnect_backoff_ms = 50;
    config.callbacks.on_envelope = on_envelope_cb;
    config.callbacks.on_connected = on_connected_cb;
    config.callbacks.on_disconnected = on_disconnected_cb;
    config.callbacks.user_data = &recorder;

    sensor_mgr_ipc_client_t *client = NULL;
    assert(sensor_mgr_ipc_client_create(&client, &config) == SAVVY_OK);

    /* Prepare actual pre-connect drops. mock_mgr rejects every outbound
     * message after CONNECT, so its zero exit status directly proves none
     * of these four records (or cached Config/Device) was replayed. */
    assert(sensor_mgr_ipc_client_send(
               client, SENSOR_MGR_IPC_ACTION_GETSTATE,
               "{\"SENSOR\":\"PIR\",\"STATE\":\"1\"}") == SAVVY_ERR_NOT_CONNECTED);
    assert(sensor_mgr_ipc_client_send(
               client, SENSOR_MGR_IPC_ACTION_PROPERTY, "{}") == SAVVY_ERR_NOT_CONNECTED);
    assert(sensor_mgr_ipc_client_send(
               client, SENSOR_MGR_IPC_ACTION_ALERT,
               "{\"IFCOMM_START\":\"1\"}") == SAVVY_ERR_NOT_CONNECTED);
    assert(sensor_mgr_ipc_client_send(
               client, SENSOR_MGR_IPC_ACTION_UPLOAD,
               "{\"targetFilePath\":\"/tmp/preconnect\",\"targetFileNm\":\"preconnect\"}") ==
           SAVVY_ERR_NOT_CONNECTED);
    assert(sensor_mgr_ipc_client_start(client) == SAVVY_OK);

    assert(wait_until_ge(get_connect_count, &recorder, 1, 5000));
    assert(wait_until_ge(get_action_count, &recorder, 2, 5000)); /* CONFIG + DEVICE */
    assert(strcmp(recorder.actions[0], SENSOR_MGR_IPC_ACTION_CONFIG) == 0);
    assert(strcmp(recorder.actions[1], SENSOR_MGR_IPC_ACTION_DEVICE) == 0);

    /* mock_mgr's first cycle closes after ~1000ms -> Sensor must detect
     * recv()==0 and reconnect on its own. */
    assert(wait_until_ge(get_disconnect_count, &recorder, 1, 5000));
    assert(wait_until_ge(get_connect_count, &recorder, 2, 5000));
    assert(recorder.reconnect_count == 1);
    assert(wait_until_ge(get_action_count, &recorder, 4, 5000)); /* CONFIG + DEVICE again */
    assert(strcmp(recorder.actions[2], SENSOR_MGR_IPC_ACTION_CONFIG) == 0);
    assert(strcmp(recorder.actions[3], SENSOR_MGR_IPC_ACTION_DEVICE) == 0);

    assert(sensor_mgr_ipc_client_stop(client) == SAVVY_OK);
    sensor_mgr_ipc_client_destroy(client);

    int status = 0;
    waitpid(mgr_pid, &status, 0);
    assert(WIFEXITED(status));
    assert(WEXITSTATUS(status) == 0);

    printf("CT-IPC-002-real: OK\n");
    return 0;
}
