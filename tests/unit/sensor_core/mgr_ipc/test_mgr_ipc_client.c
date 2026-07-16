/* SNS-CORE-003a, SNS-CORE-003b (mgr_ipc angle), CT-IPC-002, SNS-CORE-006,
 * SNS-CORE-007 (mgr_ipc angle). Dispatches on argv[1]. */

/* clock_gettime()/CLOCK_MONOTONIC are POSIX.1-2008; glibc hides them under
 * -std=c11 without this (see src/core/clock.c for Foundation's own
 * instance of the same fix). Must be defined before any system header. */
#define _POSIX_C_SOURCE 200809L

#include "fake_transport.h"
#include "mgr_ipc_client.h"
#ifdef SENSOR_MGR_IPC_TESTING
#include "mgr_ipc_test_hooks.h"
#endif
#include "savvy/protocol/ipc_envelope.h"
#include "state_report.h"

#include <assert.h>
#include <dirent.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#if defined(__has_feature)
#if __has_feature(thread_sanitizer)
#define SENSOR_MGR_IPC_TEST_WITH_TSAN 1
#endif
#endif
#if defined(__SANITIZE_THREAD__)
#define SENSOR_MGR_IPC_TEST_WITH_TSAN 1
#endif
#ifndef SENSOR_MGR_IPC_TEST_WITH_TSAN
#define SENSOR_MGR_IPC_TEST_WITH_TSAN 0
#endif

typedef struct callback_barrier {
    pthread_mutex_t lock;
    pthread_cond_t cond;
    bool action_done;
    bool release_callback;
} callback_barrier_t;

#ifdef SENSOR_MGR_IPC_TESTING
static struct timespec realtime_deadline(uint32_t timeout_ms) {
    struct timespec deadline;
    clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_sec += (time_t)(timeout_ms / 1000u);
    deadline.tv_nsec += (long)(timeout_ms % 1000u) * 1000000L;
    if (deadline.tv_nsec >= 1000000000L) {
        deadline.tv_sec += 1;
        deadline.tv_nsec -= 1000000000L;
    }
    return deadline;
}

typedef struct lifecycle_probe {
    pthread_mutex_t lock;
    pthread_cond_t cond;
    sensor_mgr_ipc_client_t *client;
    unsigned int counts[SENSOR_MGR_IPC_TEST_EVENT_COUNT];
    bool blocked[SENSOR_MGR_IPC_TEST_EVENT_COUNT];
    bool released[SENSOR_MGR_IPC_TEST_EVENT_COUNT];
} lifecycle_probe_t;

static void lifecycle_probe_hook(sensor_mgr_ipc_client_t *client,
                                 sensor_mgr_ipc_test_event_t event,
                                 void *user_data) {
    lifecycle_probe_t *probe = (lifecycle_probe_t *)user_data;
    assert(client == probe->client);
    assert(event >= 0 && event < SENSOR_MGR_IPC_TEST_EVENT_COUNT);
    pthread_mutex_lock(&probe->lock);
    probe->counts[event] += 1;
    pthread_cond_broadcast(&probe->cond);
    while (probe->blocked[event] && !probe->released[event]) {
        pthread_cond_wait(&probe->cond, &probe->lock);
    }
    pthread_mutex_unlock(&probe->lock);
}

static void lifecycle_probe_init(lifecycle_probe_t *probe,
                                 sensor_mgr_ipc_client_t *client) {
    assert(pthread_mutex_init(&probe->lock, NULL) == 0);
    assert(pthread_cond_init(&probe->cond, NULL) == 0);
    probe->client = client;
    memset(probe->counts, 0, sizeof(probe->counts));
    memset(probe->blocked, 0, sizeof(probe->blocked));
    memset(probe->released, 0, sizeof(probe->released));
    sensor_mgr_ipc_test_set_event_hook(lifecycle_probe_hook, probe);
}

static void lifecycle_probe_block(lifecycle_probe_t *probe,
                                  sensor_mgr_ipc_test_event_t event) {
    pthread_mutex_lock(&probe->lock);
    probe->blocked[event] = true;
    probe->released[event] = false;
    pthread_mutex_unlock(&probe->lock);
}

static bool lifecycle_probe_wait(lifecycle_probe_t *probe,
                                 sensor_mgr_ipc_test_event_t event,
                                 unsigned int expected, uint32_t timeout_ms) {
    struct timespec deadline = realtime_deadline(timeout_ms);
    pthread_mutex_lock(&probe->lock);
    while (probe->counts[event] < expected) {
        if (pthread_cond_timedwait(&probe->cond, &probe->lock, &deadline) != 0) {
            pthread_mutex_unlock(&probe->lock);
            return false;
        }
    }
    pthread_mutex_unlock(&probe->lock);
    return true;
}

static unsigned int lifecycle_probe_count(lifecycle_probe_t *probe,
                                          sensor_mgr_ipc_test_event_t event) {
    pthread_mutex_lock(&probe->lock);
    unsigned int count = probe->counts[event];
    pthread_mutex_unlock(&probe->lock);
    return count;
}

static void lifecycle_probe_release(lifecycle_probe_t *probe,
                                    sensor_mgr_ipc_test_event_t event) {
    pthread_mutex_lock(&probe->lock);
    probe->released[event] = true;
    pthread_cond_broadcast(&probe->cond);
    pthread_mutex_unlock(&probe->lock);
}

static void lifecycle_probe_destroy(lifecycle_probe_t *probe) {
    sensor_mgr_ipc_test_set_event_hook(NULL, NULL);
    pthread_cond_destroy(&probe->cond);
    pthread_mutex_destroy(&probe->lock);
}
#endif

typedef struct test_recorder {
    pthread_mutex_t lock;
    int connect_count;
    int reconnect_count;
    int disconnect_count;
    char actions[32][128];
    int action_count;
    sensor_mgr_ipc_client_t *callback_client;
    int callback_lifecycle_action; /* 0 none, 1 stop, 2 destroy */
    callback_barrier_t *callback_barrier;
} test_recorder_t;

static void recorder_init(test_recorder_t *r) {
    pthread_mutex_init(&r->lock, NULL);
    r->connect_count = 0;
    r->reconnect_count = 0;
    r->disconnect_count = 0;
    r->action_count = 0;
    r->callback_client = NULL;
    r->callback_lifecycle_action = 0;
    r->callback_barrier = NULL;
}

static void callback_barrier_init(callback_barrier_t *barrier) {
    assert(pthread_mutex_init(&barrier->lock, NULL) == 0);
    assert(pthread_cond_init(&barrier->cond, NULL) == 0);
    barrier->action_done = false;
    barrier->release_callback = false;
}

static void callback_barrier_wait_action(callback_barrier_t *barrier) {
    pthread_mutex_lock(&barrier->lock);
    while (!barrier->action_done) {
        pthread_cond_wait(&barrier->cond, &barrier->lock);
    }
    pthread_mutex_unlock(&barrier->lock);
}

static void callback_barrier_release(callback_barrier_t *barrier) {
    pthread_mutex_lock(&barrier->lock);
    barrier->release_callback = true;
    pthread_cond_broadcast(&barrier->cond);
    pthread_mutex_unlock(&barrier->lock);
}

static void callback_barrier_destroy(callback_barrier_t *barrier) {
    pthread_cond_destroy(&barrier->cond);
    pthread_mutex_destroy(&barrier->lock);
}

static void on_connected_cb(bool was_reconnect, void *ud) {
    test_recorder_t *r = (test_recorder_t *)ud;
    pthread_mutex_lock(&r->lock);
    r->connect_count++;
    if (was_reconnect) {
        r->reconnect_count++;
    }
    pthread_mutex_unlock(&r->lock);

    /* The client deliberately invokes callbacks without its state/I/O
     * locks. Exercise both worker-self lifecycle paths here. */
    if (r->callback_lifecycle_action == 1) {
        assert(sensor_mgr_ipc_client_stop(r->callback_client) == SAVVY_OK);
    } else if (r->callback_lifecycle_action == 2) {
        sensor_mgr_ipc_client_destroy(r->callback_client);
    }
    if (r->callback_barrier != NULL) {
        pthread_mutex_lock(&r->callback_barrier->lock);
        r->callback_barrier->action_done = true;
        pthread_cond_broadcast(&r->callback_barrier->cond);
        while (!r->callback_barrier->release_callback) {
            pthread_cond_wait(&r->callback_barrier->cond,
                              &r->callback_barrier->lock);
        }
        pthread_mutex_unlock(&r->callback_barrier->lock);
    }
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
    if (r->action_count < 32) {
        snprintf(r->actions[r->action_count], sizeof(r->actions[0]), "%s", action);
        r->action_count++;
    }
    pthread_mutex_unlock(&r->lock);
}

static int64_t now_ms_test(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
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
        sleep_ms_test(10);
        elapsed += 10;
    }
    return getter(r) >= expected;
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

static sensor_mgr_ipc_config_t make_default_config(fake_connector_ctx_t *connector_ctx, test_recorder_t *recorder) {
    sensor_mgr_ipc_config_t config;
    memset(&config, 0, sizeof(config));
    config.connector = fake_connector_connect;
    config.connector_ctx = connector_ctx;
    config.connect_timeout_ms = 300;
    config.send_timeout_ms = 300;
    config.recv_poll_timeout_ms = 150;
    config.reconnect_backoff_ms = 20;
    config.callbacks.on_envelope = on_envelope_cb;
    config.callbacks.on_connected = on_connected_cb;
    config.callbacks.on_disconnected = on_disconnected_cb;
    config.callbacks.user_data = recorder;
    return config;
}

typedef struct boundary_send_arg {
    sensor_mgr_ipc_client_t *client;
    const char *payload;
    savvy_status_t status;
} boundary_send_arg_t;

static void *boundary_send_thread_main(void *arg) {
    boundary_send_arg_t *send_arg = (boundary_send_arg_t *)arg;
    send_arg->status = sensor_mgr_ipc_client_send(
        send_arg->client, SENSOR_MGR_IPC_ACTION_UPLOAD, send_arg->payload);
    return NULL;
}

static void test_003a_pre_connect_drop(void) {
    fake_connector_ctx_t connector_ctx;
    assert(fake_connector_init(&connector_ctx, 4) == SAVVY_OK);

    test_recorder_t recorder;
    recorder_init(&recorder);
    sensor_mgr_ipc_config_t config = make_default_config(&connector_ctx, &recorder);

    sensor_mgr_ipc_client_t *client = NULL;
    assert(sensor_mgr_ipc_client_create(&client, &config) == SAVVY_OK);
    assert(sensor_mgr_ipc_client_is_connected(client) == false);

    /* Client never started -> never connected. Send must be dropped
     * before ever touching a transport (there is none to touch). */
    savvy_status_t st = sensor_mgr_ipc_client_send(client, SENSOR_MGR_IPC_ACTION_GETSTATE,
                                                    "{\"SENSOR\":\"PIR\",\"STATE\":\"1\"}");
    assert(st == SAVVY_ERR_NOT_CONNECTED);

    sensor_mgr_ipc_client_destroy(client);
    fake_connector_destroy(&connector_ctx);
    printf("SNS-CORE-003a: OK\n");
}

static void test_003b_repeated_drop_is_safe(void) {
    fake_connector_ctx_t connector_ctx;
    assert(fake_connector_init(&connector_ctx, 4) == SAVVY_OK);

    test_recorder_t recorder;
    recorder_init(&recorder);
    sensor_mgr_ipc_config_t config = make_default_config(&connector_ctx, &recorder);

    sensor_mgr_ipc_client_t *client = NULL;
    assert(sensor_mgr_ipc_client_create(&client, &config) == SAVVY_OK);

    sensor_state_report_tracker_t tracker;
    sensor_state_report_tracker_init(&tracker);
    const char *state_payload = "{\"SENSOR\":\"PIR\",\"STATE\":\"1\"}";
    assert(sensor_state_report_should_send(&tracker, SENSOR_REPORT_TYPE_PIR, 1));
    assert(sensor_mgr_ipc_client_send(client, SENSOR_MGR_IPC_ACTION_GETSTATE,
                                      state_payload) == SAVVY_ERR_NOT_CONNECTED);
    assert(!sensor_state_report_should_send(&tracker, SENSOR_REPORT_TYPE_PIR, 1));
    assert(sensor_mgr_ipc_client_send(client, SENSOR_MGR_IPC_ACTION_PROPERTY,
                                      "{}") == SAVVY_ERR_NOT_CONNECTED);
    assert(sensor_mgr_ipc_client_send(client, SENSOR_MGR_IPC_ACTION_ALERT,
                                      "{\"IFCOMM_START\":\"1\"}") == SAVVY_ERR_NOT_CONNECTED);
    assert(sensor_mgr_ipc_client_send(client, SENSOR_MGR_IPC_ACTION_UPLOAD,
                                      "{\"targetFilePath\":\"/tmp/a\",\"targetFileNm\":\"a\"}") ==
           SAVVY_ERR_NOT_CONNECTED);
    assert(fake_connector_send_count(&connector_ctx) == 0);

    assert(sensor_mgr_ipc_client_start(client) == SAVVY_OK);
    const char *config_env =
        "{\"action\":\"com.uniuni.savvysensor.config\",\"payload\":{\"jsonConfigDto\":{}}}";
    const char *device_env =
        "{\"action\":\"com.uniuni.savvysensor.device\",\"payload\":{\"jsonDeviceDto\":{}}}";
    char buf[512];
    size_t len = 0;
    int mgr_fd1 = fake_mgr_dequeue_fd(&connector_ctx);
    assert(mgr_fd1 >= 0);
    assert(fake_mgr_recv(mgr_fd1, buf, sizeof(buf), &len, 2000));
    assert(fake_connector_send_count(&connector_ctx) == 1); /* CONNECT only */
    assert(fake_mgr_send(mgr_fd1, config_env, strlen(config_env)));
    assert(fake_mgr_send(mgr_fd1, device_env, strlen(device_env)));
    assert(wait_until_ge(get_action_count, &recorder, 2, 2000));
    fake_mgr_close(mgr_fd1);
    assert(wait_until_ge(get_disconnect_count, &recorder, 1, 2000));

    int mgr_fd2 = fake_mgr_dequeue_fd(&connector_ctx);
    assert(mgr_fd2 >= 0);
    assert(fake_mgr_recv(mgr_fd2, buf, sizeof(buf), &len, 2000));
    assert(fake_connector_send_count(&connector_ctx) == 2); /* reconnect CONNECT only */
    assert(fake_mgr_send(mgr_fd2, config_env, strlen(config_env)));
    assert(fake_mgr_send(mgr_fd2, device_env, strlen(device_env)));
    assert(wait_until_ge(get_action_count, &recorder, 4, 2000));
    assert(strcmp(recorder.actions[0], SENSOR_MGR_IPC_ACTION_CONFIG) == 0);
    assert(strcmp(recorder.actions[1], SENSOR_MGR_IPC_ACTION_DEVICE) == 0);
    assert(strcmp(recorder.actions[2], SENSOR_MGR_IPC_ACTION_CONFIG) == 0);
    assert(strcmp(recorder.actions[3], SENSOR_MGR_IPC_ACTION_DEVICE) == 0);
    assert(fake_connector_send_count(&connector_ctx) == 2);

    fake_mgr_close(mgr_fd2);
    assert(sensor_mgr_ipc_client_stop(client) == SAVVY_OK);
    sensor_mgr_ipc_client_destroy(client);
    fake_connector_destroy(&connector_ctx);
    printf("SNS-CORE-003b(mgr_ipc): OK\n");
}

static void test_ct_ipc_002_reconnect(void) {
    fake_connector_ctx_t connector_ctx;
    assert(fake_connector_init(&connector_ctx, 4) == SAVVY_OK);

    test_recorder_t recorder;
    recorder_init(&recorder);
    sensor_mgr_ipc_config_t config = make_default_config(&connector_ctx, &recorder);
    config.recv_poll_timeout_ms = 1000;

    sensor_mgr_ipc_client_t *client = NULL;
    assert(sensor_mgr_ipc_client_create(&client, &config) == SAVVY_OK);
    assert(sensor_mgr_ipc_client_start(client) == SAVVY_OK);

    char buf[4096];
    size_t len = 0;

    /* --- first connection --- */
    int mgr_fd1 = fake_mgr_dequeue_fd(&connector_ctx);
    assert(mgr_fd1 >= 0);
    assert(fake_mgr_recv(mgr_fd1, buf, sizeof(buf) - 1, &len, 2000));
    buf[len] = '\0';
    assert(strstr(buf, SENSOR_MGR_IPC_ACTION_CONNECT) != NULL);

    assert(wait_until_ge(get_connect_count, &recorder, 1, 2000));
    assert(recorder.reconnect_count == 0);

    const char *config_env = "{\"action\":\"com.uniuni.savvysensor.config\",\"payload\":{\"jsonConfigDto\":{}}}";
    const char *device_env = "{\"action\":\"com.uniuni.savvysensor.device\",\"payload\":{\"jsonDeviceDto\":{}}}";

    assert(fake_mgr_send(mgr_fd1, config_env, strlen(config_env)));
    assert(fake_mgr_send(mgr_fd1, device_env, strlen(device_env)));

    assert(wait_until_ge(get_action_count, &recorder, 2, 2000));
    assert(strcmp(recorder.actions[0], SENSOR_MGR_IPC_ACTION_CONFIG) == 0);
    assert(strcmp(recorder.actions[1], SENSOR_MGR_IPC_ACTION_DEVICE) == 0);

    /* MGR closes -> Sensor must detect recv()==0 and go reconnect. */
    fake_mgr_close(mgr_fd1);
    assert(wait_until_ge(get_disconnect_count, &recorder, 1, 2000));

    /* --- reconnect --- */
    int mgr_fd2 = fake_mgr_dequeue_fd(&connector_ctx);
    assert(mgr_fd2 >= 0);
    assert(fake_mgr_recv(mgr_fd2, buf, sizeof(buf) - 1, &len, 2000));
    buf[len] = '\0';
    assert(strstr(buf, SENSOR_MGR_IPC_ACTION_CONNECT) != NULL);

    assert(wait_until_ge(get_connect_count, &recorder, 2, 2000));
    assert(recorder.reconnect_count == 1);

    assert(fake_mgr_send(mgr_fd2, config_env, strlen(config_env)));
    assert(fake_mgr_send(mgr_fd2, device_env, strlen(device_env)));
    assert(wait_until_ge(get_action_count, &recorder, 4, 2000));
    assert(strcmp(recorder.actions[2], SENSOR_MGR_IPC_ACTION_CONFIG) == 0);
    assert(strcmp(recorder.actions[3], SENSOR_MGR_IPC_ACTION_DEVICE) == 0);

    fake_mgr_close(mgr_fd2);
    sensor_mgr_ipc_client_stop(client);
    sensor_mgr_ipc_client_destroy(client);
    fake_connector_destroy(&connector_ctx);
    printf("CT-IPC-002: OK\n");
}

static char *make_upload_payload_for_encoded_size(size_t encoded_size) {
    const char *empty_payload = "{\"targetFilePath\":\"\",\"targetFileNm\":\"x\"}";
    char *text = NULL;
    size_t base_len = 0;
    assert(savvy_ipc_envelope_build(SENSOR_MGR_IPC_ACTION_UPLOAD, empty_payload,
                                    &text, &base_len) == SAVVY_OK);
    free(text);

    assert(encoded_size >= base_len);
    size_t fill_len = encoded_size - base_len;
    const char *prefix = "{\"targetFilePath\":\"";
    const char *suffix = "\",\"targetFileNm\":\"x\"}";
    size_t payload_len = strlen(prefix) + fill_len + strlen(suffix);
    char *payload = malloc(payload_len + 1u);
    assert(payload != NULL);
    size_t prefix_len = strlen(prefix);
    memcpy(payload, prefix, prefix_len);
    memset(payload + prefix_len, 'x', fill_len);
    memcpy(payload + prefix_len + fill_len, suffix, strlen(suffix));
    payload[payload_len] = '\0';
    return payload;
}

static void test_application_message_size_boundaries(void) {
    fake_connector_ctx_t connector_ctx;
    assert(fake_connector_init(&connector_ctx, 2) == SAVVY_OK);
    test_recorder_t recorder;
    recorder_init(&recorder);
    sensor_mgr_ipc_config_t config = make_default_config(&connector_ctx, &recorder);
    sensor_mgr_ipc_client_t *client = NULL;
    assert(sensor_mgr_ipc_client_create(&client, &config) == SAVVY_OK);
    assert(sensor_mgr_ipc_client_start(client) == SAVVY_OK);

    int mgr_fd = fake_mgr_dequeue_fd(&connector_ctx);
    assert(mgr_fd >= 0);
    char handshake[256];
    size_t len = 0;
    assert(fake_mgr_recv(mgr_fd, handshake, sizeof(handshake), &len, 2000));
    assert(wait_until_ge(get_connect_count, &recorder, 1, 2000));
    assert(fake_connector_send_count(&connector_ctx) == 1);

    char *payload = make_upload_payload_for_encoded_size(SAVVY_IPC_MAX_MESSAGE);
    size_t encoded_len = 0;
    char *text = NULL;
    assert(savvy_ipc_envelope_build(SENSOR_MGR_IPC_ACTION_UPLOAD, payload,
                                    &text, &encoded_len) == SAVVY_OK);
    assert(encoded_len == SAVVY_IPC_MAX_MESSAGE);
    free(text);
    boundary_send_arg_t send_arg = {client, payload, SAVVY_ERR_UNKNOWN};
    pthread_t send_thread;
    assert(pthread_create(&send_thread, NULL, boundary_send_thread_main,
                          &send_arg) == 0);
    char *record = malloc(SAVVY_IPC_MAX_MESSAGE);
    assert(record != NULL);
    assert(fake_mgr_recv(mgr_fd, record, SAVVY_IPC_MAX_MESSAGE, &len, 2000));
    pthread_join(send_thread, NULL);
    assert(send_arg.status == SAVVY_OK);
    assert(fake_connector_send_count(&connector_ctx) == 2);
    assert(len == SAVVY_IPC_MAX_MESSAGE);
    free(record);
    free(payload);

    payload = make_upload_payload_for_encoded_size(SAVVY_IPC_MAX_MESSAGE + 1u);
    assert(sensor_mgr_ipc_client_send(client, SENSOR_MGR_IPC_ACTION_UPLOAD,
                                      payload) == SAVVY_ERR_OVERFLOW);
    assert(fake_connector_send_count(&connector_ctx) == 2);
    free(payload);

    assert(sensor_mgr_ipc_client_send(client, SENSOR_MGR_IPC_ACTION_UPLOAD,
                                      "{\"targetFilePath\":1,\"targetFileNm\":\"x\"}") ==
           SAVVY_ERR_PROTOCOL);
    assert(sensor_mgr_ipc_client_send(client, SENSOR_MGR_IPC_ACTION_CONFIG,
                                      "{\"jsonConfigDto\":{}}") == SAVVY_ERR_INVALID_ARGUMENT);
    assert(fake_connector_send_count(&connector_ctx) == 2);

    fake_mgr_close(mgr_fd);
    assert(sensor_mgr_ipc_client_stop(client) == SAVVY_OK);
    sensor_mgr_ipc_client_destroy(client);
    fake_connector_destroy(&connector_ctx);
}

static void test_handshake_public_state_gate(void) {
    fake_connector_ctx_t connector_ctx;
    assert(fake_connector_init(&connector_ctx, 2) == SAVVY_OK);
    fake_connector_block_next_send(&connector_ctx);
    test_recorder_t recorder;
    recorder_init(&recorder);
    sensor_mgr_ipc_config_t config = make_default_config(&connector_ctx, &recorder);
    sensor_mgr_ipc_client_t *client = NULL;
    assert(sensor_mgr_ipc_client_create(&client, &config) == SAVVY_OK);
    assert(sensor_mgr_ipc_client_start(client) == SAVVY_OK);
    int mgr_fd = fake_mgr_dequeue_fd(&connector_ctx);
    assert(mgr_fd >= 0);
    assert(fake_connector_wait_send_blocked(&connector_ctx, 2000));

    assert(!sensor_mgr_ipc_client_is_connected(client));
    assert(get_connect_count(&recorder) == 0);
    assert(sensor_mgr_ipc_client_send(client, SENSOR_MGR_IPC_ACTION_GETSTATE,
                                      "{\"SENSOR\":\"PIR\",\"STATE\":\"1\"}") ==
           SAVVY_ERR_NOT_CONNECTED);

    fake_connector_release_blocked_send(&connector_ctx);
    char buf[512];
    size_t len = 0;
    assert(fake_mgr_recv(mgr_fd, buf, sizeof(buf), &len, 2000));
    assert(wait_until_ge(get_connect_count, &recorder, 1, 2000));
    assert(sensor_mgr_ipc_client_is_connected(client));
    assert(fake_connector_send_count(&connector_ctx) == 1);
    assert(sensor_mgr_ipc_client_send(client, SENSOR_MGR_IPC_ACTION_GETSTATE,
                                      "{\"SENSOR\":\"PIR\",\"STATE\":\"1\"}") == SAVVY_OK);
    assert(fake_connector_send_count(&connector_ctx) == 2);
    assert(fake_mgr_recv(mgr_fd, buf, sizeof(buf), &len, 2000));

    fake_mgr_close(mgr_fd);
    assert(sensor_mgr_ipc_client_stop(client) == SAVVY_OK);
    sensor_mgr_ipc_client_destroy(client);
    fake_connector_destroy(&connector_ctx);
}

static void test_inbound_rejection_and_oversize_recovery(void) {
    fake_connector_ctx_t connector_ctx;
    assert(fake_connector_init(&connector_ctx, 2) == SAVVY_OK);

    test_recorder_t recorder;
    recorder_init(&recorder);
    sensor_mgr_ipc_config_t config = make_default_config(&connector_ctx, &recorder);
    sensor_mgr_ipc_client_t *client = NULL;
    assert(sensor_mgr_ipc_client_create(&client, &config) == SAVVY_OK);
    assert(sensor_mgr_ipc_client_start(client) == SAVVY_OK);

    int mgr_fd = fake_mgr_dequeue_fd(&connector_ctx);
    assert(mgr_fd >= 0);
    char handshake[256];
    size_t handshake_len = 0;
    assert(fake_mgr_recv(mgr_fd, handshake, sizeof(handshake), &handshake_len, 2000));
    assert(wait_until_ge(get_connect_count, &recorder, 1, 2000));

    /* Invalid JSON, a Sensor->MGR action received in the wrong direction,
     * and a schema-invalid Config envelope must all be discarded. */
    assert(fake_mgr_send(mgr_fd, "{malformed", strlen("{malformed")));
    assert(fake_mgr_send(mgr_fd,
                         "{\"action\":\"com.uniuni.savvymgr.ipc.connect\",\"payload\":{}}",
                         strlen("{\"action\":\"com.uniuni.savvymgr.ipc.connect\",\"payload\":{}}")));
    assert(fake_mgr_send(mgr_fd,
                         "{\"action\":\"com.uniuni.savvysensor.config\",\"payload\":{\"wrong\":1}}",
                         strlen("{\"action\":\"com.uniuni.savvysensor.config\",\"payload\":{\"wrong\":1}}")));

    /* The framed fake transport drains an entire >64 KiB record and emits
     * SAVVY_ERR_OVERFLOW. The worker must then consume this valid record. */
    size_t oversized_len = SAVVY_IPC_MAX_MESSAGE + 1u;
    char *oversized = malloc(oversized_len);
    assert(oversized != NULL);
    memset(oversized, 'x', oversized_len);
    assert(fake_mgr_send(mgr_fd, oversized, oversized_len));
    free(oversized);

    const char *valid_config =
        "{\"action\":\"com.uniuni.savvysensor.config\",\"payload\":{\"jsonConfigDto\":{}}}";
    assert(fake_mgr_send(mgr_fd, valid_config, strlen(valid_config)));
    assert(wait_until_ge(get_action_count, &recorder, 1, 2000));
    assert(recorder.action_count == 1);
    assert(strcmp(recorder.actions[0], SENSOR_MGR_IPC_ACTION_CONFIG) == 0);

    fake_mgr_close(mgr_fd);
    assert(sensor_mgr_ipc_client_stop(client) == SAVVY_OK);
    sensor_mgr_ipc_client_destroy(client);
    fake_connector_destroy(&connector_ctx);
}

static void test_connect_handshake_failures(void) {
    const savvy_status_t failures[] = {
        SAVVY_ERR_TIMEOUT, SAVVY_ERR_CLOSED, SAVVY_ERR_IO, SAVVY_ERR_PROTOCOL
    };
    for (size_t i = 0; i < sizeof(failures) / sizeof(failures[0]); i++) {
        fake_connector_ctx_t connector_ctx;
        assert(fake_connector_init(&connector_ctx, 4) == SAVVY_OK);
        fake_connector_fail_next_sends(&connector_ctx, 1, failures[i]);

        test_recorder_t recorder;
        recorder_init(&recorder);
        sensor_mgr_ipc_config_t config = make_default_config(&connector_ctx, &recorder);
        config.reconnect_backoff_ms = 5;

        sensor_mgr_ipc_client_t *client = NULL;
        assert(sensor_mgr_ipc_client_create(&client, &config) == SAVVY_OK);
        assert(sensor_mgr_ipc_client_start(client) == SAVVY_OK);

        /* The failed CONNECT is closed and never fires on_connected. The
         * succeeding retry is the one and only connected callback. */
        int failed_fd = fake_mgr_dequeue_fd(&connector_ctx);
        assert(failed_fd >= 0);
        char buf[256];
        size_t len = 0;
        assert(fake_mgr_recv(failed_fd, buf, sizeof(buf), &len, 2000));
        assert(len == 0);
        fake_mgr_close(failed_fd);

        int good_fd = fake_mgr_dequeue_fd(&connector_ctx);
        assert(good_fd >= 0);
        assert(fake_mgr_recv(good_fd, buf, sizeof(buf), &len, 2000));
        buf[len] = '\0';
        assert(strstr(buf, SENSOR_MGR_IPC_ACTION_CONNECT) != NULL);
        assert(wait_until_ge(get_connect_count, &recorder, 1, 2000));
        assert(get_connect_count(&recorder) == 1);
        assert(fake_connector_close_count(&connector_ctx) >= 1);

        fake_mgr_close(good_fd);
        assert(sensor_mgr_ipc_client_stop(client) == SAVVY_OK);
        sensor_mgr_ipc_client_destroy(client);
        fake_connector_destroy(&connector_ctx);
    }
    printf("CT-IPC-002 handshake failures: OK\n");
}

static long count_open_fds(void) {
    DIR *d = opendir("/dev/fd");
    if (d == NULL) {
        return -1;
    }
    long count = 0;
    struct dirent *entry;
    while ((entry = readdir(d)) != NULL) {
        if (entry->d_name[0] == '.') {
            continue;
        }
        count++;
    }
    closedir(d);
    return count;
}

static long count_linux_threads(void) {
    DIR *d = opendir("/proc/self/task");
    if (d == NULL) {
        return -1; /* Linux-only direct measurement; macOS reports N/A. */
    }
    long count = 0;
    struct dirent *entry;
    while ((entry = readdir(d)) != NULL) {
        if (entry->d_name[0] != '.') {
            count++;
        }
    }
    closedir(d);
    return count;
}

static void *runtime_warmup_thread_main(void *arg) {
    (void)arg;
    return NULL;
}

static void warm_up_thread_runtime(void) {
    pthread_t thread;
    assert(pthread_create(&thread, NULL, runtime_warmup_thread_main, NULL) == 0);
    assert(pthread_join(thread, NULL) == 0);
}

static long capture_stable_thread_baseline(void) {
    long current = count_linux_threads();
    if (current < 0) {
        return -1;
    }

    int64_t deadline_ms = now_ms_test() + 2000;
    if (!SENSOR_MGR_IPC_TEST_WITH_TSAN) {
        /* A detached callback worker can finish its observable cleanup a
         * fraction before the kernel removes its task entry. Preserve the
         * direct production-process baseline of exactly one thread while
         * waiting only on the measured resource state. */
        while (current != 1 && now_ms_test() < deadline_ms) {
            sleep_ms_test(1);
            current = count_linux_threads();
        }
        assert(current == 1);
        return current;
    }

    /* TSan owns a helper thread after pthread warm-up. Require a stable
     * measured baseline rather than assuming how many runtime threads the
     * sanitizer implementation uses. */
    long candidate = current;
    unsigned int stable_samples = 0;
    while (stable_samples < 20 && now_ms_test() < deadline_ms) {
        sleep_ms_test(1);
        current = count_linux_threads();
        if (current == candidate) {
            stable_samples += 1;
        } else {
            candidate = current;
            stable_samples = 0;
        }
    }
    assert(stable_samples == 20);
    return candidate;
}

static long wait_for_thread_baseline(long expected, uint32_t timeout_ms) {
    long current = count_linux_threads();
    if (expected < 0) {
        return current;
    }

    int64_t deadline_ms = now_ms_test() + (int64_t)timeout_ms;
    while (current != expected && now_ms_test() < deadline_ms) {
        sleep_ms_test(1);
        current = count_linux_threads();
    }
    return current;
}

static void test_006_repeated_connect_disconnect(void) {
    /* Initialize sanitizer pthread helpers before process baselines. The
     * fake connector's TLS signal below identifies the client-owned worker
     * independently from any runtime helper thread. */
    warm_up_thread_runtime();

    fake_connector_ctx_t connector_ctx;
    assert(fake_connector_init(&connector_ctx, 4) == SAVVY_OK);
    assert(fake_connector_enable_worker_tracking(&connector_ctx) == SAVVY_OK);

    test_recorder_t recorder;
    recorder_init(&recorder);
    sensor_mgr_ipc_config_t config = make_default_config(&connector_ctx, &recorder);
    config.reconnect_backoff_ms = 1;

    long fds_baseline = count_open_fds();
    long threads_baseline = capture_stable_thread_baseline();
    sensor_mgr_ipc_client_t *client = NULL;
    assert(sensor_mgr_ipc_client_create(&client, &config) == SAVVY_OK);
    assert(sensor_mgr_ipc_client_start(client) == SAVVY_OK);
    assert(fake_connector_wait_worker_started(&connector_ctx, 1, 2000));
    assert(fake_connector_worker_started_count(&connector_ctx) == 1);
    assert(fake_connector_worker_exited_count(&connector_ctx) == 0);
    assert(fake_connector_worker_active_count(&connector_ctx) == 1);

    const int cycles = 500;
    long fds_before = -1;
    long threads_running = -1;
    for (int i = 0; i < cycles; i++) {
        int mgr_fd = fake_mgr_dequeue_fd(&connector_ctx);
        assert(mgr_fd >= 0);
        char buf[256];
        size_t len = 0;
        fake_mgr_recv(mgr_fd, buf, sizeof(buf), &len, 500); /* drain CONNECT handshake */
        if (i == 0) {
            threads_running = count_linux_threads();
            if (threads_baseline >= 0 && !SENSOR_MGR_IPC_TEST_WITH_TSAN) {
                /* Preserve the direct non-sanitized Linux 1 -> 2 -> 1
                 * production-process assertion. */
                assert(threads_baseline == 1);
                assert(threads_running == 2);
            }
        }
        fake_mgr_close(mgr_fd);
        if (i == 4) {
            fds_before = count_open_fds();
        }
    }
    assert(wait_until_ge(get_disconnect_count, &recorder, cycles, 10000));

    long fds_after = count_open_fds();
    assert(fds_before >= 0 && fds_after >= 0);
    /* Must not grow with the number of cycles - small constant slack for
     * whatever transient fds this test loop itself still has open. */
    assert(fds_after <= fds_before + 4);

    assert(sensor_mgr_ipc_client_stop(client) == SAVVY_OK);
    assert(fake_connector_wait_worker_exited(&connector_ctx, 1, 2000));
    assert(fake_connector_worker_started_count(&connector_ctx) == 1);
    assert(fake_connector_worker_exited_count(&connector_ctx) == 1);
    assert(fake_connector_worker_active_count(&connector_ctx) == 0);
    assert(fake_connector_connect_count(&connector_ctx) >= cycles);
    long fds_final = count_open_fds();
    long threads_final = wait_for_thread_baseline(threads_baseline, 2000);
    assert(fds_baseline >= 0 && fds_final >= 0);
    /* count_open_fds itself opens the directory it reads, so both samples
     * have the same one-descriptor measurement overhead. No client fd may
     * remain after stop/join. */
    assert(fds_final <= fds_baseline + 1);
    if (threads_baseline >= 0) {
        if (SENSOR_MGR_IPC_TEST_WITH_TSAN) {
            /* TSan helpers belong to the warmed process baseline; client
             * ownership is proved by the TLS counters above. */
            assert(threads_final == threads_baseline);
        } else {
            assert(threads_final == 1);
        }
    }
    sensor_mgr_ipc_client_destroy(client);
    fake_connector_destroy(&connector_ctx);
    printf("SNS-CORE-006: OK (fd=%ld/%ld/%ld threads=%ld/%ld/%ld cycles=%d)\n",
           fds_baseline, fds_after, fds_final, threads_baseline,
           threads_running, threads_final, cycles);
}

typedef struct lifecycle_thread_arg {
    sensor_mgr_ipc_client_t *client;
    savvy_status_t status;
} lifecycle_thread_arg_t;

#ifdef SENSOR_MGR_IPC_TESTING
typedef struct operation_latch {
    pthread_mutex_t lock;
    pthread_cond_t cond;
    bool done;
} operation_latch_t;

typedef struct race_thread_arg {
    sensor_mgr_ipc_client_t *client;
    savvy_status_t status;
    operation_latch_t *completion;
} race_thread_arg_t;

static void operation_latch_init(operation_latch_t *latch) {
    assert(pthread_mutex_init(&latch->lock, NULL) == 0);
    assert(pthread_cond_init(&latch->cond, NULL) == 0);
    latch->done = false;
}

static void operation_latch_mark_done(operation_latch_t *latch) {
    pthread_mutex_lock(&latch->lock);
    latch->done = true;
    pthread_cond_broadcast(&latch->cond);
    pthread_mutex_unlock(&latch->lock);
}

static bool operation_latch_wait(operation_latch_t *latch, uint32_t timeout_ms) {
    struct timespec deadline = realtime_deadline(timeout_ms);
    pthread_mutex_lock(&latch->lock);
    while (!latch->done) {
        if (pthread_cond_timedwait(&latch->cond, &latch->lock, &deadline) != 0) {
            pthread_mutex_unlock(&latch->lock);
            return false;
        }
    }
    pthread_mutex_unlock(&latch->lock);
    return true;
}

static void operation_latch_destroy(operation_latch_t *latch) {
    pthread_cond_destroy(&latch->cond);
    pthread_mutex_destroy(&latch->lock);
}
#endif

static void *stop_thread_main(void *arg) {
    lifecycle_thread_arg_t *thread_arg = (lifecycle_thread_arg_t *)arg;
    thread_arg->status = sensor_mgr_ipc_client_stop(thread_arg->client);
    return NULL;
}

static void *start_thread_main(void *arg) {
    lifecycle_thread_arg_t *thread_arg = (lifecycle_thread_arg_t *)arg;
    thread_arg->status = sensor_mgr_ipc_client_start(thread_arg->client);
    return NULL;
}

static void *destroy_thread_main(void *arg) {
    lifecycle_thread_arg_t *thread_arg = (lifecycle_thread_arg_t *)arg;
    sensor_mgr_ipc_client_destroy(thread_arg->client);
    thread_arg->status = SAVVY_OK;
    return NULL;
}

static void *send_thread_main(void *arg) {
    lifecycle_thread_arg_t *thread_arg = (lifecycle_thread_arg_t *)arg;
    thread_arg->status = sensor_mgr_ipc_client_send(
        thread_arg->client, SENSOR_MGR_IPC_ACTION_GETSTATE,
        "{\"SENSOR\":\"PIR\",\"STATE\":\"1\"}");
    return NULL;
}

#ifdef SENSOR_MGR_IPC_TESTING
static void *race_start_thread_main(void *arg) {
    race_thread_arg_t *thread_arg = (race_thread_arg_t *)arg;
    thread_arg->status = sensor_mgr_ipc_client_start(thread_arg->client);
    operation_latch_mark_done(thread_arg->completion);
    return NULL;
}

static void *race_destroy_thread_main(void *arg) {
    race_thread_arg_t *thread_arg = (race_thread_arg_t *)arg;
    sensor_mgr_ipc_client_destroy(thread_arg->client);
    thread_arg->status = SAVVY_OK;
    operation_latch_mark_done(thread_arg->completion);
    return NULL;
}

static void assert_batch_resource_baseline(long fds_baseline,
                                           long threads_baseline) {
    long fds_final = count_open_fds();
    long threads_final = wait_for_thread_baseline(threads_baseline, 2000);
    assert(fds_final == fds_baseline);
    if (threads_baseline >= 0) {
        assert(threads_final == threads_baseline);
    }
}

static void test_stopped_start_destroy_destroy_wins(void) {
    const int cycles = 500;
    warm_up_thread_runtime();
    long fds_baseline = count_open_fds();
    long threads_baseline = capture_stable_thread_baseline();

    for (int i = 0; i < cycles; i++) {
        fake_connector_ctx_t connector_ctx;
        assert(fake_connector_init(&connector_ctx, 2) == SAVVY_OK);
        connector_ctx.fail_all = true;
        test_recorder_t recorder;
        recorder_init(&recorder);
        sensor_mgr_ipc_config_t config = make_default_config(&connector_ctx, &recorder);
        sensor_mgr_ipc_client_t *client = NULL;
        assert(sensor_mgr_ipc_client_create(&client, &config) == SAVVY_OK);

        lifecycle_probe_t probe;
        lifecycle_probe_init(&probe, client);
        lifecycle_probe_block(&probe, SENSOR_MGR_IPC_TEST_START_AFTER_DESTROY_CHECK);
        operation_latch_t start_done;
        operation_latch_t destroy_done;
        operation_latch_init(&start_done);
        operation_latch_init(&destroy_done);
        race_thread_arg_t start_arg = {client, SAVVY_ERR_UNKNOWN, &start_done};
        race_thread_arg_t destroy_arg = {client, SAVVY_ERR_UNKNOWN, &destroy_done};
        pthread_t start_thread;
        pthread_t destroy_thread;

        assert(pthread_create(&start_thread, NULL, race_start_thread_main, &start_arg) == 0);
        assert(lifecycle_probe_wait(&probe,
                                    SENSOR_MGR_IPC_TEST_START_AFTER_DESTROY_CHECK,
                                    1, 2000));
        assert(pthread_create(&destroy_thread, NULL, race_destroy_thread_main,
                              &destroy_arg) == 0);
        assert(lifecycle_probe_wait(&probe, SENSOR_MGR_IPC_TEST_DESTROY_CLAIMED,
                                    1, 2000));
        lifecycle_probe_release(&probe, SENSOR_MGR_IPC_TEST_START_AFTER_DESTROY_CHECK);

        assert(operation_latch_wait(&start_done, 2000));
        assert(operation_latch_wait(&destroy_done, 2000));
        assert(pthread_join(start_thread, NULL) == 0);
        assert(pthread_join(destroy_thread, NULL) == 0);
        assert(start_arg.status == SAVVY_ERR_INVALID_ARGUMENT);
        assert(lifecycle_probe_count(&probe,
                                     SENSOR_MGR_IPC_TEST_START_BEFORE_CANCEL_INIT) == 0);
        assert(lifecycle_probe_count(&probe,
                                     SENSOR_MGR_IPC_TEST_START_AFTER_WORKER_CREATE) == 0);
        assert(lifecycle_probe_count(&probe,
                                     SENSOR_MGR_IPC_TEST_WORKER_ENTERED) == 0);
        assert(lifecycle_probe_count(&probe,
                                     SENSOR_MGR_IPC_TEST_WORKER_FINISHED) == 0);
        assert(lifecycle_probe_count(&probe,
                                     SENSOR_MGR_IPC_TEST_CANCEL_INITIALIZED) == 0);
        assert(lifecycle_probe_count(&probe,
                                     SENSOR_MGR_IPC_TEST_CANCEL_DESTROYED) == 0);
        assert(fake_connector_connect_count(&connector_ctx) == 0);
        assert(fake_connector_close_count(&connector_ctx) == 0);

        lifecycle_probe_destroy(&probe);
        operation_latch_destroy(&start_done);
        operation_latch_destroy(&destroy_done);
        fake_connector_destroy(&connector_ctx);
        pthread_mutex_destroy(&recorder.lock);
    }

    assert_batch_resource_baseline(fds_baseline, threads_baseline);
    printf("SNS-CORE-007 stopped start/destroy destroy-wins: OK (%d cycles)\n",
           cycles);
}

static void test_stopped_start_destroy_start_wins(void) {
    const int cycles = 500;
    warm_up_thread_runtime();
    long fds_baseline = count_open_fds();
    long threads_baseline = capture_stable_thread_baseline();

    for (int i = 0; i < cycles; i++) {
        fake_connector_ctx_t connector_ctx;
        assert(fake_connector_init(&connector_ctx, 2) == SAVVY_OK);
        connector_ctx.fail_all = true;
        test_recorder_t recorder;
        recorder_init(&recorder);
        sensor_mgr_ipc_config_t config = make_default_config(&connector_ctx, &recorder);
        config.connect_timeout_ms = 5000;
        sensor_mgr_ipc_client_t *client = NULL;
        assert(sensor_mgr_ipc_client_create(&client, &config) == SAVVY_OK);

        lifecycle_probe_t probe;
        lifecycle_probe_init(&probe, client);
        sensor_mgr_ipc_test_event_t blocked_event =
            (i % 2) == 0 ? SENSOR_MGR_IPC_TEST_START_BEFORE_CANCEL_INIT
                         : SENSOR_MGR_IPC_TEST_START_AFTER_WORKER_CREATE;
        lifecycle_probe_block(&probe, blocked_event);
        operation_latch_t start_done;
        operation_latch_t destroy_done;
        operation_latch_init(&start_done);
        operation_latch_init(&destroy_done);
        race_thread_arg_t start_arg = {client, SAVVY_ERR_UNKNOWN, &start_done};
        race_thread_arg_t destroy_arg = {client, SAVVY_ERR_UNKNOWN, &destroy_done};
        pthread_t start_thread;
        pthread_t destroy_thread;

        assert(pthread_create(&start_thread, NULL, race_start_thread_main, &start_arg) == 0);
        assert(lifecycle_probe_wait(&probe, blocked_event, 1, 2000));
        assert(pthread_create(&destroy_thread, NULL, race_destroy_thread_main,
                              &destroy_arg) == 0);
        assert(lifecycle_probe_wait(&probe,
                                    SENSOR_MGR_IPC_TEST_DESTROY_WAITING_TRANSITION,
                                    1, 2000));
        lifecycle_probe_release(&probe, blocked_event);

        assert(operation_latch_wait(&start_done, 2000));
        assert(operation_latch_wait(&destroy_done, 2000));
        assert(pthread_join(start_thread, NULL) == 0);
        assert(pthread_join(destroy_thread, NULL) == 0);
        assert(start_arg.status == SAVVY_OK);
        assert(lifecycle_probe_count(&probe,
                                     SENSOR_MGR_IPC_TEST_START_AFTER_WORKER_CREATE) == 1);
        assert(lifecycle_probe_count(&probe,
                                     SENSOR_MGR_IPC_TEST_WORKER_ENTERED) == 1);
        assert(lifecycle_probe_count(&probe,
                                     SENSOR_MGR_IPC_TEST_WORKER_FINISHED) == 1);
        assert(lifecycle_probe_count(&probe,
                                     SENSOR_MGR_IPC_TEST_CANCEL_INITIALIZED) == 1);
        assert(lifecycle_probe_count(&probe,
                                     SENSOR_MGR_IPC_TEST_CANCEL_DESTROYED) == 1);
        assert(fake_connector_close_count(&connector_ctx) == 0);

        lifecycle_probe_destroy(&probe);
        operation_latch_destroy(&start_done);
        operation_latch_destroy(&destroy_done);
        fake_connector_destroy(&connector_ctx);
        pthread_mutex_destroy(&recorder.lock);
    }

    assert_batch_resource_baseline(fds_baseline, threads_baseline);
    printf("SNS-CORE-007 stopped start/destroy start-wins: OK (%d cycles)\n",
           cycles);
}

static void test_callback_stop_start_destroy_overlap(void) {
    const int cycles = 500;
    warm_up_thread_runtime();
    long fds_baseline = count_open_fds();
    long threads_baseline = capture_stable_thread_baseline();

    for (int i = 0; i < cycles; i++) {
        fake_connector_ctx_t connector_ctx;
        assert(fake_connector_init(&connector_ctx, 2) == SAVVY_OK);
        assert(fake_connector_enable_worker_tracking(&connector_ctx) == SAVVY_OK);
        test_recorder_t recorder;
        recorder_init(&recorder);
        callback_barrier_t callback_barrier;
        callback_barrier_init(&callback_barrier);
        sensor_mgr_ipc_config_t config = make_default_config(&connector_ctx, &recorder);
        config.recv_poll_timeout_ms = 5;
        sensor_mgr_ipc_client_t *client = NULL;
        assert(sensor_mgr_ipc_client_create(&client, &config) == SAVVY_OK);

        lifecycle_probe_t probe;
        lifecycle_probe_init(&probe, client);
        recorder.callback_client = client;
        recorder.callback_lifecycle_action = 1;
        recorder.callback_barrier = &callback_barrier;
        assert(sensor_mgr_ipc_client_start(client) == SAVVY_OK);
        int mgr_fd = fake_mgr_dequeue_fd(&connector_ctx);
        assert(mgr_fd >= 0);
        char buf[256];
        size_t len = 0;
        assert(fake_mgr_recv(mgr_fd, buf, sizeof(buf), &len, 2000));
        assert(len > 0);
        callback_barrier_wait_action(&callback_barrier);
        assert(fake_connector_wait_worker_started(&connector_ctx, 1, 2000));

        operation_latch_t start_done;
        operation_latch_t destroy_done;
        operation_latch_init(&start_done);
        operation_latch_init(&destroy_done);
        race_thread_arg_t start_arg = {client, SAVVY_ERR_UNKNOWN, &start_done};
        race_thread_arg_t destroy_arg = {client, SAVVY_ERR_UNKNOWN, &destroy_done};
        pthread_t start_thread;
        pthread_t destroy_thread;
        assert(pthread_create(&start_thread, NULL, race_start_thread_main, &start_arg) == 0);
        assert(lifecycle_probe_wait(&probe, SENSOR_MGR_IPC_TEST_JOIN_CLAIMED,
                                    1, 2000));
        assert(pthread_create(&destroy_thread, NULL, race_destroy_thread_main,
                              &destroy_arg) == 0);
        assert(lifecycle_probe_wait(&probe, SENSOR_MGR_IPC_TEST_DESTROY_CLAIMED,
                                    1, 2000));
        assert(lifecycle_probe_wait(&probe, SENSOR_MGR_IPC_TEST_WAITING_JOIN,
                                    1, 2000));
        callback_barrier_release(&callback_barrier);

        assert(operation_latch_wait(&start_done, 2000));
        assert(operation_latch_wait(&destroy_done, 2000));
        assert(pthread_join(start_thread, NULL) == 0);
        assert(pthread_join(destroy_thread, NULL) == 0);
        assert(start_arg.status == SAVVY_ERR_INVALID_ARGUMENT);
        assert(fake_connector_wait_worker_exited(&connector_ctx, 1, 2000));
        assert(fake_connector_worker_started_count(&connector_ctx) == 1);
        assert(fake_connector_worker_exited_count(&connector_ctx) == 1);
        assert(fake_connector_worker_active_count(&connector_ctx) == 0);
        assert(fake_connector_connect_count(&connector_ctx) == 1);
        assert(fake_connector_close_count(&connector_ctx) == 1);
        assert(lifecycle_probe_count(&probe,
                                     SENSOR_MGR_IPC_TEST_START_AFTER_WORKER_CREATE) == 1);
        assert(lifecycle_probe_count(&probe,
                                     SENSOR_MGR_IPC_TEST_WORKER_ENTERED) == 1);
        assert(lifecycle_probe_count(&probe,
                                     SENSOR_MGR_IPC_TEST_WORKER_FINISHED) == 1);
        assert(lifecycle_probe_count(&probe,
                                     SENSOR_MGR_IPC_TEST_CANCEL_INITIALIZED) == 1);
        assert(lifecycle_probe_count(&probe,
                                     SENSOR_MGR_IPC_TEST_CANCEL_DESTROYED) == 1);

        fake_mgr_close(mgr_fd);
        lifecycle_probe_destroy(&probe);
        operation_latch_destroy(&start_done);
        operation_latch_destroy(&destroy_done);
        callback_barrier_destroy(&callback_barrier);
        fake_connector_destroy(&connector_ctx);
        pthread_mutex_destroy(&recorder.lock);
    }

    assert_batch_resource_baseline(fds_baseline, threads_baseline);
    printf("SNS-CORE-007 callback-stop start/destroy overlap: OK (%d cycles)\n",
           cycles);
}
#endif

static void test_send_shutdown_race(void) {
    const int cycles = 250;
    for (int i = 0; i < cycles; i++) {
        fake_connector_ctx_t connector_ctx;
        assert(fake_connector_init(&connector_ctx, 2) == SAVVY_OK);
        test_recorder_t recorder;
        recorder_init(&recorder);
        sensor_mgr_ipc_config_t config = make_default_config(&connector_ctx, &recorder);
        /* Keep the stress focused on send/close ownership rather than the
         * intentionally bounded recv poll; each shutdown still traverses
         * the same state_lock -> io_lock path. */
        config.recv_poll_timeout_ms = 5;
        sensor_mgr_ipc_client_t *client = NULL;
        assert(sensor_mgr_ipc_client_create(&client, &config) == SAVVY_OK);
        assert(sensor_mgr_ipc_client_start(client) == SAVVY_OK);
        int mgr_fd = fake_mgr_dequeue_fd(&connector_ctx);
        assert(mgr_fd >= 0);
        char buf[256];
        size_t len = 0;
        assert(fake_mgr_recv(mgr_fd, buf, sizeof(buf), &len, 2000));

        lifecycle_thread_arg_t send_arg = {client, SAVVY_ERR_UNKNOWN};
        lifecycle_thread_arg_t shutdown_arg = {client, SAVVY_ERR_UNKNOWN};
        pthread_t send_thread;
        pthread_t shutdown_thread;
        assert(pthread_create(&send_thread, NULL, send_thread_main, &send_arg) == 0);
        if ((i % 2) == 0) {
            assert(pthread_create(&shutdown_thread, NULL, stop_thread_main, &shutdown_arg) == 0);
        } else {
            assert(pthread_create(&shutdown_thread, NULL, destroy_thread_main, &shutdown_arg) == 0);
        }
        pthread_join(send_thread, NULL);
        pthread_join(shutdown_thread, NULL);
        assert(send_arg.status == SAVVY_OK || send_arg.status == SAVVY_ERR_NOT_CONNECTED ||
               send_arg.status == SAVVY_ERR_CLOSED || send_arg.status == SAVVY_ERR_INVALID_ARGUMENT);

        fake_mgr_close(mgr_fd);
        if ((i % 2) == 0) {
            sensor_mgr_ipc_client_destroy(client);
        }
        fake_connector_destroy(&connector_ctx);
    }
}

static void test_concurrent_stop_destroy_stress(void) {
    for (int i = 0; i < 500; i++) {
        fake_connector_ctx_t connector_ctx;
        assert(fake_connector_init(&connector_ctx, 2) == SAVVY_OK);
        test_recorder_t recorder;
        recorder_init(&recorder);
        sensor_mgr_ipc_config_t config = make_default_config(&connector_ctx, &recorder);
        sensor_mgr_ipc_client_t *client = NULL;
        assert(sensor_mgr_ipc_client_create(&client, &config) == SAVVY_OK);

        lifecycle_thread_arg_t first = {client, SAVVY_ERR_UNKNOWN};
        lifecycle_thread_arg_t second = {client, SAVVY_ERR_UNKNOWN};
        pthread_t first_thread;
        pthread_t second_thread;
        if ((i % 3) == 0) {
            assert(pthread_create(&first_thread, NULL, stop_thread_main, &first) == 0);
            assert(pthread_create(&second_thread, NULL, stop_thread_main, &second) == 0);
            pthread_join(first_thread, NULL);
            pthread_join(second_thread, NULL);
            assert(first.status == SAVVY_OK && second.status == SAVVY_OK);
            sensor_mgr_ipc_client_destroy(client);
        } else if ((i % 3) == 1) {
            assert(pthread_create(&first_thread, NULL, stop_thread_main, &first) == 0);
            assert(pthread_create(&second_thread, NULL, destroy_thread_main, &second) == 0);
            pthread_join(first_thread, NULL);
            pthread_join(second_thread, NULL);
            assert(first.status == SAVVY_OK || first.status == SAVVY_ERR_INVALID_ARGUMENT);
        } else {
            assert(pthread_create(&first_thread, NULL, destroy_thread_main, &first) == 0);
            assert(pthread_create(&second_thread, NULL, destroy_thread_main, &second) == 0);
            pthread_join(first_thread, NULL);
            pthread_join(second_thread, NULL);
        }
        fake_connector_destroy(&connector_ctx);
    }
}

static void test_callback_lifecycle_operations(void) {
    const int cycles = 500;
    for (int i = 0; i < cycles; i++) {
        int scenario = i % 3;
        fake_connector_ctx_t connector_ctx;
        assert(fake_connector_init(&connector_ctx, 2) == SAVVY_OK);
        /* Scenario 2 transfers terminal ownership to a detached callback
         * worker. Track the worker itself so the next stress batch never
         * samples a merely signalled-but-not-yet-exited predecessor. */
        assert(fake_connector_enable_worker_tracking(&connector_ctx) == SAVVY_OK);
        test_recorder_t recorder;
        recorder_init(&recorder);
        callback_barrier_t barrier;
        callback_barrier_init(&barrier);
        sensor_mgr_ipc_config_t config = make_default_config(&connector_ctx, &recorder);
        config.recv_poll_timeout_ms = 5;
        sensor_mgr_ipc_client_t *client = NULL;
        assert(sensor_mgr_ipc_client_create(&client, &config) == SAVVY_OK);
        recorder.callback_client = client;
        recorder.callback_lifecycle_action = scenario == 2 ? 2 : 1;
        recorder.callback_barrier = &barrier;
        assert(sensor_mgr_ipc_client_start(client) == SAVVY_OK);

        int mgr_fd = fake_mgr_dequeue_fd(&connector_ctx);
        assert(mgr_fd >= 0);
        char buf[256];
        size_t len = 0;
        assert(fake_mgr_recv(mgr_fd, buf, sizeof(buf), &len, 2000));
        assert(len > 0);
        callback_barrier_wait_action(&barrier);

        lifecycle_thread_arg_t operation = {client, SAVVY_ERR_UNKNOWN};
        pthread_t operation_thread;
        if (scenario == 0) {
            /* callback-stop + immediate external destroy: destroy cannot
             * return/free until the blocked callback is released and the
             * joinable worker completes terminal cleanup. */
            assert(pthread_create(&operation_thread, NULL, destroy_thread_main,
                                  &operation) == 0);
            callback_barrier_release(&barrier);
            pthread_join(operation_thread, NULL);
        } else if (scenario == 1) {
            /* callback-stop + immediate restart: restart must reap the old
             * worker before initializing a fresh cancel source. */
            recorder.callback_lifecycle_action = 0;
            assert(pthread_create(&operation_thread, NULL, start_thread_main,
                                  &operation) == 0);
            callback_barrier_release(&barrier);
            pthread_join(operation_thread, NULL);
            assert(operation.status == SAVVY_OK);

            int restarted_mgr_fd = fake_mgr_dequeue_fd(&connector_ctx);
            assert(restarted_mgr_fd >= 0);
            assert(fake_mgr_recv(restarted_mgr_fd, buf, sizeof(buf), &len, 2000));
            assert(wait_until_ge(get_connect_count, &recorder, 2, 2000));
            fake_mgr_close(restarted_mgr_fd);
            assert(sensor_mgr_ipc_client_stop(client) == SAVVY_OK);
            sensor_mgr_ipc_client_destroy(client);
        } else {
            /* callback-destroy has already claimed detached terminal
             * ownership. A concurrent stop is safely rejected/no-op. */
            assert(pthread_create(&operation_thread, NULL, stop_thread_main,
                                  &operation) == 0);
            pthread_join(operation_thread, NULL);
            assert(operation.status == SAVVY_ERR_INVALID_ARGUMENT);
            callback_barrier_release(&barrier);
            assert(fake_connector_wait_close_count(&connector_ctx, 1, 2000));
            assert(wait_until_ge(get_disconnect_count, &recorder, 1, 2000));
        }

        int expected_workers = scenario == 1 ? 2 : 1;
        assert(fake_connector_wait_worker_exited(&connector_ctx,
                                                 expected_workers, 2000));
        assert(fake_connector_worker_started_count(&connector_ctx) ==
               expected_workers);
        assert(fake_connector_worker_exited_count(&connector_ctx) ==
               expected_workers);
        assert(fake_connector_worker_active_count(&connector_ctx) == 0);
        fake_mgr_close(mgr_fd);
        callback_barrier_destroy(&barrier);
        fake_connector_destroy(&connector_ctx);
    }
    printf("SNS-CORE-007 callback terminal barrier: OK (%d cycles)\n", cycles);
}

static void test_007_shutdown_wakes_blocked_operations(void) {
    /* Case A: shutdown while blocked in a repeatedly-failing connect. */
    {
        fake_connector_ctx_t connector_ctx;
        assert(fake_connector_init(&connector_ctx, 4) == SAVVY_OK);
        connector_ctx.fail_all = true;

        test_recorder_t recorder;
        recorder_init(&recorder);
        sensor_mgr_ipc_config_t config = make_default_config(&connector_ctx, &recorder);
        config.connect_timeout_ms = 5000; /* deliberately long - cancel must still wake it promptly */

        sensor_mgr_ipc_client_t *client = NULL;
        assert(sensor_mgr_ipc_client_create(&client, &config) == SAVVY_OK);
        assert(sensor_mgr_ipc_client_start(client) == SAVVY_OK);
        sleep_ms_test(50); /* let the worker thread actually enter the blocked connect */

        int64_t t0 = now_ms_test();
        assert(sensor_mgr_ipc_client_stop(client) == SAVVY_OK);
        int64_t elapsed = now_ms_test() - t0;
        assert(elapsed < 1000);

        /* Idempotent: a second stop() must also return promptly and safely. */
        t0 = now_ms_test();
        assert(sensor_mgr_ipc_client_stop(client) == SAVVY_OK);
        assert(now_ms_test() - t0 < 100);

        sensor_mgr_ipc_client_destroy(client);
        fake_connector_destroy(&connector_ctx);
    }

    /* Case B: shutdown while blocked in recv() after a successful
     * connect - bounded by recv_poll_timeout_ms (Foundation's transport
     * recv() takes no cancel source of its own), not instant; this test
     * uses a short, realistic poll interval and asserts the bound holds. */
    {
        fake_connector_ctx_t connector_ctx;
        assert(fake_connector_init(&connector_ctx, 4) == SAVVY_OK);

        test_recorder_t recorder;
        recorder_init(&recorder);
        sensor_mgr_ipc_config_t config = make_default_config(&connector_ctx, &recorder);
        config.recv_poll_timeout_ms = 150;

        sensor_mgr_ipc_client_t *client = NULL;
        assert(sensor_mgr_ipc_client_create(&client, &config) == SAVVY_OK);
        assert(sensor_mgr_ipc_client_start(client) == SAVVY_OK);

        int mgr_fd = fake_mgr_dequeue_fd(&connector_ctx);
        assert(mgr_fd >= 0);
        char buf[256];
        size_t len = 0;
        fake_mgr_recv(mgr_fd, buf, sizeof(buf), &len, 2000); /* drain CONNECT handshake */
        assert(wait_until_ge(get_connect_count, &recorder, 1, 2000));

        int64_t t0 = now_ms_test();
        assert(sensor_mgr_ipc_client_stop(client) == SAVVY_OK);
        int64_t elapsed = now_ms_test() - t0;
        assert(elapsed < 1000); /* generous margin over the 150ms poll bound */

        fake_mgr_close(mgr_fd);
        sensor_mgr_ipc_client_destroy(client);
        fake_connector_destroy(&connector_ctx);
    }

    /* Case C: destroy itself owns the blocked-connect shutdown, so no
     * caller may race a free against cancel-source/worker teardown. */
    {
        fake_connector_ctx_t connector_ctx;
        assert(fake_connector_init(&connector_ctx, 2) == SAVVY_OK);
        connector_ctx.fail_all = true;
        test_recorder_t recorder;
        recorder_init(&recorder);
        sensor_mgr_ipc_config_t config = make_default_config(&connector_ctx, &recorder);
        config.connect_timeout_ms = 5000;
        sensor_mgr_ipc_client_t *client = NULL;
        assert(sensor_mgr_ipc_client_create(&client, &config) == SAVVY_OK);
        assert(sensor_mgr_ipc_client_start(client) == SAVVY_OK);
        sleep_ms_test(50);
        lifecycle_thread_arg_t destroy_arg = {client, SAVVY_ERR_UNKNOWN};
        pthread_t destroy_thread;
        assert(pthread_create(&destroy_thread, NULL, destroy_thread_main, &destroy_arg) == 0);
        pthread_join(destroy_thread, NULL);
        fake_connector_destroy(&connector_ctx);
    }

    /* Case D: destroy while the worker is inside its bounded recv poll. */
    {
        fake_connector_ctx_t connector_ctx;
        assert(fake_connector_init(&connector_ctx, 2) == SAVVY_OK);
        test_recorder_t recorder;
        recorder_init(&recorder);
        sensor_mgr_ipc_config_t config = make_default_config(&connector_ctx, &recorder);
        config.recv_poll_timeout_ms = 150;
        sensor_mgr_ipc_client_t *client = NULL;
        assert(sensor_mgr_ipc_client_create(&client, &config) == SAVVY_OK);
        assert(sensor_mgr_ipc_client_start(client) == SAVVY_OK);
        int mgr_fd = fake_mgr_dequeue_fd(&connector_ctx);
        assert(mgr_fd >= 0);
        char buf[256];
        size_t len = 0;
        assert(fake_mgr_recv(mgr_fd, buf, sizeof(buf), &len, 2000));

        lifecycle_thread_arg_t destroy_arg = {client, SAVVY_ERR_UNKNOWN};
        pthread_t destroy_thread;
        int64_t t0 = now_ms_test();
        assert(pthread_create(&destroy_thread, NULL, destroy_thread_main, &destroy_arg) == 0);
        pthread_join(destroy_thread, NULL);
        assert(now_ms_test() - t0 < 1000);
        fake_mgr_close(mgr_fd);
        fake_connector_destroy(&connector_ctx);
    }

    test_concurrent_stop_destroy_stress();
    test_send_shutdown_race();
    test_callback_lifecycle_operations();
#ifdef SENSOR_MGR_IPC_TESTING
    test_stopped_start_destroy_destroy_wins();
    test_stopped_start_destroy_start_wins();
    test_callback_stop_start_destroy_overlap();
#endif

    printf("SNS-CORE-007(mgr_ipc): OK\n");
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s <subtest-id>\n", argv[0]);
        return 2;
    }

    if (strcmp(argv[1], "003a") == 0) {
        test_003a_pre_connect_drop();
    } else if (strcmp(argv[1], "003b") == 0) {
        test_003b_repeated_drop_is_safe();
    } else if (strcmp(argv[1], "ct-ipc-002") == 0) {
        test_ct_ipc_002_reconnect();
        test_handshake_public_state_gate();
        test_connect_handshake_failures();
        test_application_message_size_boundaries();
        test_inbound_rejection_and_oversize_recovery();
    } else if (strcmp(argv[1], "006") == 0) {
        test_006_repeated_connect_disconnect();
    } else if (strcmp(argv[1], "007") == 0) {
        test_007_shutdown_wakes_blocked_operations();
    } else {
        fprintf(stderr, "unknown subtest id: %s\n", argv[1]);
        return 2;
    }

    return 0;
}
