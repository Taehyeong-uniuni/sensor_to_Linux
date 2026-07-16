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

#include <dirent.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define CHECK(cond, msg) do { \
    if (!(cond)) { \
        fprintf(stderr, "CHECK failed: %s (%s:%d)\n", (msg), __FILE__, __LINE__); \
        abort(); \
    } \
} while (0)

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
    CHECK(client == probe->client, "lifecycle_probe_hook client matches probe owner");
    CHECK(event >= 0 && event < SENSOR_MGR_IPC_TEST_EVENT_COUNT, "lifecycle_probe_hook event in range");
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
    CHECK(pthread_mutex_init(&probe->lock, NULL) == 0, "lifecycle_probe lock initialized");
    CHECK(pthread_cond_init(&probe->cond, NULL) == 0, "lifecycle_probe cond initialized");
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
    CHECK(pthread_mutex_init(&barrier->lock, NULL) == 0, "callback_barrier lock initialized");
    CHECK(pthread_cond_init(&barrier->cond, NULL) == 0, "callback_barrier cond initialized");
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
        CHECK(sensor_mgr_ipc_client_stop(r->callback_client) == SAVVY_OK, "callback-triggered stop succeeds");
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
    CHECK(fake_connector_init(&connector_ctx, 4) == SAVVY_OK, "connector_ctx initialized");

    test_recorder_t recorder;
    recorder_init(&recorder);
    sensor_mgr_ipc_config_t config = make_default_config(&connector_ctx, &recorder);

    sensor_mgr_ipc_client_t *client = NULL;
    CHECK(sensor_mgr_ipc_client_create(&client, &config) == SAVVY_OK, "client_create succeeds");
    CHECK(sensor_mgr_ipc_client_is_connected(client) == false, "client not connected before start");

    /* Client never started -> never connected. Send must be dropped
     * before ever touching a transport (there is none to touch). */
    savvy_status_t st = sensor_mgr_ipc_client_send(client, SENSOR_MGR_IPC_ACTION_GETSTATE,
                                                    "{\"SENSOR\":\"PIR\",\"STATE\":\"1\"}");
    CHECK(st == SAVVY_ERR_NOT_CONNECTED, "send before start is dropped as not-connected");

    sensor_mgr_ipc_client_destroy(client);
    fake_connector_destroy(&connector_ctx);
    printf("SNS-CORE-003a: OK\n");
}

static void test_003b_repeated_drop_is_safe(void) {
    fake_connector_ctx_t connector_ctx;
    CHECK(fake_connector_init(&connector_ctx, 4) == SAVVY_OK, "connector_ctx initialized");

    test_recorder_t recorder;
    recorder_init(&recorder);
    sensor_mgr_ipc_config_t config = make_default_config(&connector_ctx, &recorder);

    sensor_mgr_ipc_client_t *client = NULL;
    CHECK(sensor_mgr_ipc_client_create(&client, &config) == SAVVY_OK, "client_create succeeds");

    sensor_state_report_tracker_t tracker;
    sensor_state_report_tracker_init(&tracker);
    const char *state_payload = "{\"SENSOR\":\"PIR\",\"STATE\":\"1\"}";
    CHECK(sensor_state_report_should_send(&tracker, SENSOR_REPORT_TYPE_PIR, 1), "first PIR state report should send");
    CHECK(sensor_mgr_ipc_client_send(client, SENSOR_MGR_IPC_ACTION_GETSTATE,
                                      state_payload) == SAVVY_ERR_NOT_CONNECTED, "GETSTATE send before start is dropped");
    CHECK(!sensor_state_report_should_send(&tracker, SENSOR_REPORT_TYPE_PIR, 1), "repeated PIR state report is suppressed");
    CHECK(sensor_mgr_ipc_client_send(client, SENSOR_MGR_IPC_ACTION_PROPERTY,
                                      "{}") == SAVVY_ERR_NOT_CONNECTED, "PROPERTY send before start is dropped");
    CHECK(sensor_mgr_ipc_client_send(client, SENSOR_MGR_IPC_ACTION_ALERT,
                                      "{\"IFCOMM_START\":\"1\"}") == SAVVY_ERR_NOT_CONNECTED, "ALERT send before start is dropped");
    CHECK(sensor_mgr_ipc_client_send(client, SENSOR_MGR_IPC_ACTION_UPLOAD,
                                      "{\"targetFilePath\":\"/tmp/a\",\"targetFileNm\":\"a\"}") ==
           SAVVY_ERR_NOT_CONNECTED, "UPLOAD send before start is dropped");
    CHECK(fake_connector_send_count(&connector_ctx) == 0, "no sends reached transport before start");

    CHECK(sensor_mgr_ipc_client_start(client) == SAVVY_OK, "client_start succeeds");
    const char *config_env =
        "{\"action\":\"com.uniuni.savvysensor.config\",\"payload\":{\"jsonConfigDto\":{}}}";
    const char *device_env =
        "{\"action\":\"com.uniuni.savvysensor.device\",\"payload\":{\"jsonDeviceDto\":{}}}";
    char buf[512];
    size_t len = 0;
    int mgr_fd1 = fake_mgr_dequeue_fd(&connector_ctx);
    CHECK(mgr_fd1 >= 0, "first mgr fd dequeued");
    CHECK(fake_mgr_recv(mgr_fd1, buf, sizeof(buf), &len, 2000), "mgr receives first CONNECT handshake");
    CHECK(fake_connector_send_count(&connector_ctx) == 1, "only CONNECT sent so far"); /* CONNECT only */
    CHECK(fake_mgr_send(mgr_fd1, config_env, strlen(config_env)), "mgr sends config envelope");
    CHECK(fake_mgr_send(mgr_fd1, device_env, strlen(device_env)), "mgr sends device envelope");
    CHECK(wait_until_ge(get_action_count, &recorder, 2, 2000), "first config/device actions observed");
    fake_mgr_close(mgr_fd1);
    CHECK(wait_until_ge(get_disconnect_count, &recorder, 1, 2000), "disconnect observed after mgr close");

    int mgr_fd2 = fake_mgr_dequeue_fd(&connector_ctx);
    CHECK(mgr_fd2 >= 0, "second mgr fd dequeued");
    CHECK(fake_mgr_recv(mgr_fd2, buf, sizeof(buf), &len, 2000), "mgr receives reconnect CONNECT handshake");
    CHECK(fake_connector_send_count(&connector_ctx) == 2, "reconnect CONNECT sent"); /* reconnect CONNECT only */
    CHECK(fake_mgr_send(mgr_fd2, config_env, strlen(config_env)), "mgr resends config envelope");
    CHECK(fake_mgr_send(mgr_fd2, device_env, strlen(device_env)), "mgr resends device envelope");
    CHECK(wait_until_ge(get_action_count, &recorder, 4, 2000), "second config/device actions observed");
    CHECK(strcmp(recorder.actions[0], SENSOR_MGR_IPC_ACTION_CONFIG) == 0, "action 0 is CONFIG");
    CHECK(strcmp(recorder.actions[1], SENSOR_MGR_IPC_ACTION_DEVICE) == 0, "action 1 is DEVICE");
    CHECK(strcmp(recorder.actions[2], SENSOR_MGR_IPC_ACTION_CONFIG) == 0, "action 2 is CONFIG");
    CHECK(strcmp(recorder.actions[3], SENSOR_MGR_IPC_ACTION_DEVICE) == 0, "action 3 is DEVICE");
    CHECK(fake_connector_send_count(&connector_ctx) == 2, "no extra sends after reconnect actions");

    fake_mgr_close(mgr_fd2);
    CHECK(sensor_mgr_ipc_client_stop(client) == SAVVY_OK, "client_stop succeeds");
    sensor_mgr_ipc_client_destroy(client);
    fake_connector_destroy(&connector_ctx);
    printf("SNS-CORE-003b(mgr_ipc): OK\n");
}

static void test_ct_ipc_002_reconnect(void) {
    fake_connector_ctx_t connector_ctx;
    CHECK(fake_connector_init(&connector_ctx, 4) == SAVVY_OK, "connector_ctx initialized");

    test_recorder_t recorder;
    recorder_init(&recorder);
    sensor_mgr_ipc_config_t config = make_default_config(&connector_ctx, &recorder);
    config.recv_poll_timeout_ms = 1000;

    sensor_mgr_ipc_client_t *client = NULL;
    CHECK(sensor_mgr_ipc_client_create(&client, &config) == SAVVY_OK, "client_create succeeds");
    CHECK(sensor_mgr_ipc_client_start(client) == SAVVY_OK, "client_start succeeds");

    char buf[4096];
    size_t len = 0;

    /* --- first connection --- */
    int mgr_fd1 = fake_mgr_dequeue_fd(&connector_ctx);
    CHECK(mgr_fd1 >= 0, "first mgr fd dequeued");
    CHECK(fake_mgr_recv(mgr_fd1, buf, sizeof(buf) - 1, &len, 2000), "mgr receives first handshake");
    buf[len] = '\0';
    CHECK(strstr(buf, SENSOR_MGR_IPC_ACTION_CONNECT) != NULL, "first handshake is a CONNECT action");

    CHECK(wait_until_ge(get_connect_count, &recorder, 1, 2000), "first connect observed");
    CHECK(recorder.reconnect_count == 0, "first connect is not a reconnect");

    const char *config_env = "{\"action\":\"com.uniuni.savvysensor.config\",\"payload\":{\"jsonConfigDto\":{}}}";
    const char *device_env = "{\"action\":\"com.uniuni.savvysensor.device\",\"payload\":{\"jsonDeviceDto\":{}}}";

    CHECK(fake_mgr_send(mgr_fd1, config_env, strlen(config_env)), "mgr sends config envelope");
    CHECK(fake_mgr_send(mgr_fd1, device_env, strlen(device_env)), "mgr sends device envelope");

    CHECK(wait_until_ge(get_action_count, &recorder, 2, 2000), "first config/device actions observed");
    CHECK(strcmp(recorder.actions[0], SENSOR_MGR_IPC_ACTION_CONFIG) == 0, "action 0 is CONFIG");
    CHECK(strcmp(recorder.actions[1], SENSOR_MGR_IPC_ACTION_DEVICE) == 0, "action 1 is DEVICE");

    /* MGR closes -> Sensor must detect recv()==0 and go reconnect. */
    fake_mgr_close(mgr_fd1);
    CHECK(wait_until_ge(get_disconnect_count, &recorder, 1, 2000), "disconnect observed after mgr close");

    /* --- reconnect --- */
    int mgr_fd2 = fake_mgr_dequeue_fd(&connector_ctx);
    CHECK(mgr_fd2 >= 0, "second mgr fd dequeued");
    CHECK(fake_mgr_recv(mgr_fd2, buf, sizeof(buf) - 1, &len, 2000), "mgr receives reconnect handshake");
    buf[len] = '\0';
    CHECK(strstr(buf, SENSOR_MGR_IPC_ACTION_CONNECT) != NULL, "reconnect handshake is a CONNECT action");

    CHECK(wait_until_ge(get_connect_count, &recorder, 2, 2000), "second connect observed");
    CHECK(recorder.reconnect_count == 1, "second connect is a reconnect");

    CHECK(fake_mgr_send(mgr_fd2, config_env, strlen(config_env)), "mgr resends config envelope");
    CHECK(fake_mgr_send(mgr_fd2, device_env, strlen(device_env)), "mgr resends device envelope");
    CHECK(wait_until_ge(get_action_count, &recorder, 4, 2000), "second config/device actions observed");
    CHECK(strcmp(recorder.actions[2], SENSOR_MGR_IPC_ACTION_CONFIG) == 0, "action 2 is CONFIG");
    CHECK(strcmp(recorder.actions[3], SENSOR_MGR_IPC_ACTION_DEVICE) == 0, "action 3 is DEVICE");

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
    CHECK(savvy_ipc_envelope_build(SENSOR_MGR_IPC_ACTION_UPLOAD, empty_payload,
                                    &text, &base_len) == SAVVY_OK, "base envelope builds");
    free(text);

    CHECK(encoded_size >= base_len, "requested encoded size fits base envelope");
    size_t fill_len = encoded_size - base_len;
    const char *prefix = "{\"targetFilePath\":\"";
    const char *suffix = "\",\"targetFileNm\":\"x\"}";
    size_t payload_len = strlen(prefix) + fill_len + strlen(suffix);
    char *payload = malloc(payload_len + 1u);
    CHECK(payload != NULL, "payload buffer allocated");
    size_t prefix_len = strlen(prefix);
    memcpy(payload, prefix, prefix_len);
    memset(payload + prefix_len, 'x', fill_len);
    memcpy(payload + prefix_len + fill_len, suffix, strlen(suffix));
    payload[payload_len] = '\0';
    return payload;
}

static void test_application_message_size_boundaries(void) {
    fake_connector_ctx_t connector_ctx;
    CHECK(fake_connector_init(&connector_ctx, 2) == SAVVY_OK, "connector_ctx initialized");
    test_recorder_t recorder;
    recorder_init(&recorder);
    sensor_mgr_ipc_config_t config = make_default_config(&connector_ctx, &recorder);
    sensor_mgr_ipc_client_t *client = NULL;
    CHECK(sensor_mgr_ipc_client_create(&client, &config) == SAVVY_OK, "client_create succeeds");
    CHECK(sensor_mgr_ipc_client_start(client) == SAVVY_OK, "client_start succeeds");

    int mgr_fd = fake_mgr_dequeue_fd(&connector_ctx);
    CHECK(mgr_fd >= 0, "mgr fd dequeued");
    char handshake[256];
    size_t len = 0;
    CHECK(fake_mgr_recv(mgr_fd, handshake, sizeof(handshake), &len, 2000), "mgr receives CONNECT handshake");
    CHECK(wait_until_ge(get_connect_count, &recorder, 1, 2000), "connect observed");
    CHECK(fake_connector_send_count(&connector_ctx) == 1, "only CONNECT sent so far");

    char *payload = make_upload_payload_for_encoded_size(SAVVY_IPC_MAX_MESSAGE);
    size_t encoded_len = 0;
    char *text = NULL;
    CHECK(savvy_ipc_envelope_build(SENSOR_MGR_IPC_ACTION_UPLOAD, payload,
                                    &text, &encoded_len) == SAVVY_OK, "max-size envelope builds");
    CHECK(encoded_len == SAVVY_IPC_MAX_MESSAGE, "envelope encodes to exactly SAVVY_IPC_MAX_MESSAGE");
    free(text);
    boundary_send_arg_t send_arg = {client, payload, SAVVY_ERR_UNKNOWN};
    pthread_t send_thread;
    int send_thread_create_rc = pthread_create(&send_thread, NULL, boundary_send_thread_main,
                          &send_arg);
    CHECK(send_thread_create_rc == 0, "pthread_create(send_thread) succeeds");
    bool send_thread_created = (send_thread_create_rc == 0);
    char *record = malloc(SAVVY_IPC_MAX_MESSAGE);
    CHECK(record != NULL, "record buffer allocated");
    CHECK(fake_mgr_recv(mgr_fd, record, SAVVY_IPC_MAX_MESSAGE, &len, 2000), "mgr receives boundary-sized record");
    if (send_thread_created) {
        int send_thread_join_rc = pthread_join(send_thread, NULL);
        CHECK(send_thread_join_rc == 0, "pthread_join(send_thread) succeeds");
    }
    CHECK(send_arg.status == SAVVY_OK, "boundary send reports SAVVY_OK");
    CHECK(fake_connector_send_count(&connector_ctx) == 2, "boundary send reached transport");
    CHECK(len == SAVVY_IPC_MAX_MESSAGE, "mgr received exactly SAVVY_IPC_MAX_MESSAGE bytes");
    free(record);
    free(payload);

    payload = make_upload_payload_for_encoded_size(SAVVY_IPC_MAX_MESSAGE + 1u);
    CHECK(sensor_mgr_ipc_client_send(client, SENSOR_MGR_IPC_ACTION_UPLOAD,
                                      payload) == SAVVY_ERR_OVERFLOW, "over-max send is rejected as overflow");
    CHECK(fake_connector_send_count(&connector_ctx) == 2, "oversize send never reached transport");
    free(payload);

    CHECK(sensor_mgr_ipc_client_send(client, SENSOR_MGR_IPC_ACTION_UPLOAD,
                                      "{\"targetFilePath\":1,\"targetFileNm\":\"x\"}") ==
           SAVVY_ERR_PROTOCOL, "non-string targetFilePath is a protocol error");
    CHECK(sensor_mgr_ipc_client_send(client, SENSOR_MGR_IPC_ACTION_CONFIG,
                                      "{\"jsonConfigDto\":{}}") == SAVVY_ERR_INVALID_ARGUMENT, "CONFIG is not a sendable action");
    CHECK(fake_connector_send_count(&connector_ctx) == 2, "no additional sends reached transport");

    fake_mgr_close(mgr_fd);
    CHECK(sensor_mgr_ipc_client_stop(client) == SAVVY_OK, "client_stop succeeds");
    sensor_mgr_ipc_client_destroy(client);
    fake_connector_destroy(&connector_ctx);
}

static void test_handshake_public_state_gate(void) {
    fake_connector_ctx_t connector_ctx;
    CHECK(fake_connector_init(&connector_ctx, 2) == SAVVY_OK, "connector_ctx initialized");
    fake_connector_block_next_send(&connector_ctx);
    test_recorder_t recorder;
    recorder_init(&recorder);
    sensor_mgr_ipc_config_t config = make_default_config(&connector_ctx, &recorder);
    sensor_mgr_ipc_client_t *client = NULL;
    CHECK(sensor_mgr_ipc_client_create(&client, &config) == SAVVY_OK, "client_create succeeds");
    CHECK(sensor_mgr_ipc_client_start(client) == SAVVY_OK, "client_start succeeds");
    int mgr_fd = fake_mgr_dequeue_fd(&connector_ctx);
    CHECK(mgr_fd >= 0, "mgr fd dequeued");
    CHECK(fake_connector_wait_send_blocked(&connector_ctx, 2000), "connector send is blocked mid-handshake");

    CHECK(!sensor_mgr_ipc_client_is_connected(client), "client not yet connected while send is blocked");
    CHECK(get_connect_count(&recorder) == 0, "no connect callback fired yet");
    CHECK(sensor_mgr_ipc_client_send(client, SENSOR_MGR_IPC_ACTION_GETSTATE,
                                      "{\"SENSOR\":\"PIR\",\"STATE\":\"1\"}") ==
           SAVVY_ERR_NOT_CONNECTED, "send during blocked handshake is dropped as not-connected");

    fake_connector_release_blocked_send(&connector_ctx);
    char buf[512];
    size_t len = 0;
    CHECK(fake_mgr_recv(mgr_fd, buf, sizeof(buf), &len, 2000), "mgr receives handshake after release");
    CHECK(wait_until_ge(get_connect_count, &recorder, 1, 2000), "connect observed after handshake completes");
    CHECK(sensor_mgr_ipc_client_is_connected(client), "client reports connected after handshake");
    CHECK(fake_connector_send_count(&connector_ctx) == 1, "only CONNECT sent so far");
    CHECK(sensor_mgr_ipc_client_send(client, SENSOR_MGR_IPC_ACTION_GETSTATE,
                                      "{\"SENSOR\":\"PIR\",\"STATE\":\"1\"}") == SAVVY_OK, "send after connect succeeds");
    CHECK(fake_connector_send_count(&connector_ctx) == 2, "GETSTATE send reached transport");
    CHECK(fake_mgr_recv(mgr_fd, buf, sizeof(buf), &len, 2000), "mgr receives GETSTATE send");

    fake_mgr_close(mgr_fd);
    CHECK(sensor_mgr_ipc_client_stop(client) == SAVVY_OK, "client_stop succeeds");
    sensor_mgr_ipc_client_destroy(client);
    fake_connector_destroy(&connector_ctx);
}

static void test_inbound_rejection_and_oversize_recovery(void) {
    fake_connector_ctx_t connector_ctx;
    CHECK(fake_connector_init(&connector_ctx, 2) == SAVVY_OK, "connector_ctx initialized");

    test_recorder_t recorder;
    recorder_init(&recorder);
    sensor_mgr_ipc_config_t config = make_default_config(&connector_ctx, &recorder);
    sensor_mgr_ipc_client_t *client = NULL;
    CHECK(sensor_mgr_ipc_client_create(&client, &config) == SAVVY_OK, "client_create succeeds");
    CHECK(sensor_mgr_ipc_client_start(client) == SAVVY_OK, "client_start succeeds");

    int mgr_fd = fake_mgr_dequeue_fd(&connector_ctx);
    CHECK(mgr_fd >= 0, "mgr fd dequeued");
    char handshake[256];
    size_t handshake_len = 0;
    CHECK(fake_mgr_recv(mgr_fd, handshake, sizeof(handshake), &handshake_len, 2000), "mgr receives CONNECT handshake");
    CHECK(wait_until_ge(get_connect_count, &recorder, 1, 2000), "connect observed");

    /* Invalid JSON, a Sensor->MGR action received in the wrong direction,
     * and a schema-invalid Config envelope must all be discarded. */
    CHECK(fake_mgr_send(mgr_fd, "{malformed", strlen("{malformed")), "mgr sends malformed JSON");
    CHECK(fake_mgr_send(mgr_fd,
                         "{\"action\":\"com.uniuni.savvymgr.ipc.connect\",\"payload\":{}}",
                         strlen("{\"action\":\"com.uniuni.savvymgr.ipc.connect\",\"payload\":{}}")), "mgr sends wrong-direction connect action");
    CHECK(fake_mgr_send(mgr_fd,
                         "{\"action\":\"com.uniuni.savvysensor.config\",\"payload\":{\"wrong\":1}}",
                         strlen("{\"action\":\"com.uniuni.savvysensor.config\",\"payload\":{\"wrong\":1}}")), "mgr sends schema-invalid config envelope");

    /* The framed fake transport drains an entire >64 KiB record and emits
     * SAVVY_ERR_OVERFLOW. The worker must then consume this valid record. */
    size_t oversized_len = SAVVY_IPC_MAX_MESSAGE + 1u;
    char *oversized = malloc(oversized_len);
    CHECK(oversized != NULL, "oversized buffer allocated");
    memset(oversized, 'x', oversized_len);
    CHECK(fake_mgr_send(mgr_fd, oversized, oversized_len), "mgr sends oversized record");
    free(oversized);

    const char *valid_config =
        "{\"action\":\"com.uniuni.savvysensor.config\",\"payload\":{\"jsonConfigDto\":{}}}";
    CHECK(fake_mgr_send(mgr_fd, valid_config, strlen(valid_config)), "mgr sends valid config envelope");
    CHECK(wait_until_ge(get_action_count, &recorder, 1, 2000), "valid config action observed");
    CHECK(recorder.action_count == 1, "exactly one action recorded despite prior rejects");
    CHECK(strcmp(recorder.actions[0], SENSOR_MGR_IPC_ACTION_CONFIG) == 0, "recorded action is CONFIG");

    fake_mgr_close(mgr_fd);
    CHECK(sensor_mgr_ipc_client_stop(client) == SAVVY_OK, "client_stop succeeds");
    sensor_mgr_ipc_client_destroy(client);
    fake_connector_destroy(&connector_ctx);
}

static void test_connect_handshake_failures(void) {
    const savvy_status_t failures[] = {
        SAVVY_ERR_TIMEOUT, SAVVY_ERR_CLOSED, SAVVY_ERR_IO, SAVVY_ERR_PROTOCOL
    };
    for (size_t i = 0; i < sizeof(failures) / sizeof(failures[0]); i++) {
        fake_connector_ctx_t connector_ctx;
        CHECK(fake_connector_init(&connector_ctx, 4) == SAVVY_OK, "connector_ctx initialized");
        fake_connector_fail_next_sends(&connector_ctx, 1, failures[i]);

        test_recorder_t recorder;
        recorder_init(&recorder);
        sensor_mgr_ipc_config_t config = make_default_config(&connector_ctx, &recorder);
        config.reconnect_backoff_ms = 5;

        sensor_mgr_ipc_client_t *client = NULL;
        CHECK(sensor_mgr_ipc_client_create(&client, &config) == SAVVY_OK, "client_create succeeds");
        CHECK(sensor_mgr_ipc_client_start(client) == SAVVY_OK, "client_start succeeds");

        /* The failed CONNECT is closed and never fires on_connected. The
         * succeeding retry is the one and only connected callback. */
        int failed_fd = fake_mgr_dequeue_fd(&connector_ctx);
        CHECK(failed_fd >= 0, "failed-attempt mgr fd dequeued");
        char buf[256];
        size_t len = 0;
        CHECK(fake_mgr_recv(failed_fd, buf, sizeof(buf), &len, 2000), "mgr recv observes closed failed attempt");
        CHECK(len == 0, "failed attempt closes without sending a handshake");
        fake_mgr_close(failed_fd);

        int good_fd = fake_mgr_dequeue_fd(&connector_ctx);
        CHECK(good_fd >= 0, "retry mgr fd dequeued");
        CHECK(fake_mgr_recv(good_fd, buf, sizeof(buf), &len, 2000), "mgr receives retry handshake");
        buf[len] = '\0';
        CHECK(strstr(buf, SENSOR_MGR_IPC_ACTION_CONNECT) != NULL, "retry handshake is a CONNECT action");
        CHECK(wait_until_ge(get_connect_count, &recorder, 1, 2000), "connect observed on retry");
        CHECK(get_connect_count(&recorder) == 1, "exactly one connect callback fired");
        CHECK(fake_connector_close_count(&connector_ctx) >= 1, "failed attempt's transport was closed");

        fake_mgr_close(good_fd);
        CHECK(sensor_mgr_ipc_client_stop(client) == SAVVY_OK, "client_stop succeeds");
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
    int thread_create_rc = pthread_create(&thread, NULL, runtime_warmup_thread_main, NULL);
    CHECK(thread_create_rc == 0, "pthread_create(thread) succeeds");
    bool thread_created = (thread_create_rc == 0);
    if (thread_created) {
        int thread_join_rc = pthread_join(thread, NULL);
        CHECK(thread_join_rc == 0, "pthread_join(thread) succeeds");
    }
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
        CHECK(current == 1, "thread count settles to production baseline of 1");
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
    CHECK(stable_samples == 20, "TSan thread count reaches a stable sample baseline");
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
    CHECK(fake_connector_init(&connector_ctx, 4) == SAVVY_OK, "connector_ctx initialized");
    CHECK(fake_connector_enable_worker_tracking(&connector_ctx) == SAVVY_OK, "worker tracking enabled");

    test_recorder_t recorder;
    recorder_init(&recorder);
    sensor_mgr_ipc_config_t config = make_default_config(&connector_ctx, &recorder);
    config.reconnect_backoff_ms = 1;

    long fds_baseline = count_open_fds();
    long threads_baseline = capture_stable_thread_baseline();
    sensor_mgr_ipc_client_t *client = NULL;
    CHECK(sensor_mgr_ipc_client_create(&client, &config) == SAVVY_OK, "client_create succeeds");
    CHECK(sensor_mgr_ipc_client_start(client) == SAVVY_OK, "client_start succeeds");
    CHECK(fake_connector_wait_worker_started(&connector_ctx, 1, 2000), "worker started");
    CHECK(fake_connector_worker_started_count(&connector_ctx) == 1, "worker started count is 1");
    CHECK(fake_connector_worker_exited_count(&connector_ctx) == 0, "worker not yet exited");
    CHECK(fake_connector_worker_active_count(&connector_ctx) == 1, "worker active count is 1");

    const int cycles = 500;
    long fds_before = -1;
    long threads_running = -1;
    for (int i = 0; i < cycles; i++) {
        int mgr_fd = fake_mgr_dequeue_fd(&connector_ctx);
        CHECK(mgr_fd >= 0, "mgr fd dequeued this cycle");
        char buf[256];
        size_t len = 0;
        fake_mgr_recv(mgr_fd, buf, sizeof(buf), &len, 500); /* drain CONNECT handshake */
        if (i == 0) {
            threads_running = count_linux_threads();
            if (threads_baseline >= 0 && !SENSOR_MGR_IPC_TEST_WITH_TSAN) {
                /* Preserve the direct non-sanitized Linux 1 -> 2 -> 1
                 * production-process assertion. */
                CHECK(threads_baseline == 1, "pre-connect thread baseline is 1");
                CHECK(threads_running == 2, "connected thread count is 2");
            }
        }
        fake_mgr_close(mgr_fd);
        if (i == 4) {
            fds_before = count_open_fds();
        }
    }
    CHECK(wait_until_ge(get_disconnect_count, &recorder, cycles, 10000), "all cycles observed a disconnect");

    long fds_after = count_open_fds();
    CHECK(fds_before >= 0 && fds_after >= 0, "fd samples captured");
    /* Must not grow with the number of cycles - small constant slack for
     * whatever transient fds this test loop itself still has open. */
    CHECK(fds_after <= fds_before + 4, "fd count does not grow with cycle count");

    CHECK(sensor_mgr_ipc_client_stop(client) == SAVVY_OK, "client_stop succeeds");
    CHECK(fake_connector_wait_worker_exited(&connector_ctx, 1, 2000), "worker exited after stop");
    CHECK(fake_connector_worker_started_count(&connector_ctx) == 1, "worker started count remains 1");
    CHECK(fake_connector_worker_exited_count(&connector_ctx) == 1, "worker exited count is 1");
    CHECK(fake_connector_worker_active_count(&connector_ctx) == 0, "worker active count is 0 after stop");
    CHECK(fake_connector_connect_count(&connector_ctx) >= cycles, "connect count covers all cycles");
    long fds_final = count_open_fds();
    long threads_final = wait_for_thread_baseline(threads_baseline, 2000);
    CHECK(fds_baseline >= 0 && fds_final >= 0, "final fd samples captured");
    /* count_open_fds itself opens the directory it reads, so both samples
     * have the same one-descriptor measurement overhead. No client fd may
     * remain after stop/join. */
    CHECK(fds_final <= fds_baseline + 1, "no client fd remains after stop/join");
    if (threads_baseline >= 0) {
        if (SENSOR_MGR_IPC_TEST_WITH_TSAN) {
            /* TSan helpers belong to the warmed process baseline; client
             * ownership is proved by the TLS counters above. */
            CHECK(threads_final == threads_baseline, "thread count returns to TSan baseline");
        } else {
            CHECK(threads_final == 1, "thread count returns to 1 after stop");
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
    CHECK(pthread_mutex_init(&latch->lock, NULL) == 0, "operation_latch lock initialized");
    CHECK(pthread_cond_init(&latch->cond, NULL) == 0, "operation_latch cond initialized");
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
    CHECK(fds_final == fds_baseline, "fd count returns to batch baseline");
    if (threads_baseline >= 0) {
        CHECK(threads_final == threads_baseline, "thread count returns to batch baseline");
    }
}

static void test_stopped_start_destroy_destroy_wins(void) {
    const int cycles = 500;
    warm_up_thread_runtime();
    long fds_baseline = count_open_fds();
    long threads_baseline = capture_stable_thread_baseline();

    for (int i = 0; i < cycles; i++) {
        fake_connector_ctx_t connector_ctx;
        CHECK(fake_connector_init(&connector_ctx, 2) == SAVVY_OK, "connector_ctx initialized");
        connector_ctx.fail_all = true;
        test_recorder_t recorder;
        recorder_init(&recorder);
        sensor_mgr_ipc_config_t config = make_default_config(&connector_ctx, &recorder);
        sensor_mgr_ipc_client_t *client = NULL;
        CHECK(sensor_mgr_ipc_client_create(&client, &config) == SAVVY_OK, "client_create succeeds");

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

        int start_thread_create_rc = pthread_create(&start_thread, NULL, race_start_thread_main, &start_arg);
        CHECK(start_thread_create_rc == 0, "pthread_create(start_thread) succeeds");
        bool start_thread_created = (start_thread_create_rc == 0);
        CHECK(lifecycle_probe_wait(&probe,
                                    SENSOR_MGR_IPC_TEST_START_AFTER_DESTROY_CHECK,
                                    1, 2000), "probe observes START_AFTER_DESTROY_CHECK");
        int destroy_thread_create_rc = pthread_create(&destroy_thread, NULL, race_destroy_thread_main,
                              &destroy_arg);
        CHECK(destroy_thread_create_rc == 0, "pthread_create(destroy_thread) succeeds");
        bool destroy_thread_created = (destroy_thread_create_rc == 0);
        CHECK(lifecycle_probe_wait(&probe, SENSOR_MGR_IPC_TEST_DESTROY_CLAIMED,
                                    1, 2000), "probe observes DESTROY_CLAIMED");
        lifecycle_probe_release(&probe, SENSOR_MGR_IPC_TEST_START_AFTER_DESTROY_CHECK);

        CHECK(operation_latch_wait(&start_done, 2000), "start_done latch reached");
        CHECK(operation_latch_wait(&destroy_done, 2000), "destroy_done latch reached");
        if (start_thread_created) {
            int start_thread_join_rc = pthread_join(start_thread, NULL);
            CHECK(start_thread_join_rc == 0, "pthread_join(start_thread) succeeds");
        }
        if (destroy_thread_created) {
            int destroy_thread_join_rc = pthread_join(destroy_thread, NULL);
            CHECK(destroy_thread_join_rc == 0, "pthread_join(destroy_thread) succeeds");
        }
        CHECK(start_arg.status == SAVVY_ERR_INVALID_ARGUMENT, "start loses the destroy race");
        CHECK(lifecycle_probe_count(&probe,
                                     SENSOR_MGR_IPC_TEST_START_BEFORE_CANCEL_INIT) == 0, "start never reached BEFORE_CANCEL_INIT");
        CHECK(lifecycle_probe_count(&probe,
                                     SENSOR_MGR_IPC_TEST_START_AFTER_WORKER_CREATE) == 0, "start never reached AFTER_WORKER_CREATE");
        CHECK(lifecycle_probe_count(&probe,
                                     SENSOR_MGR_IPC_TEST_WORKER_ENTERED) == 0, "worker never entered");
        CHECK(lifecycle_probe_count(&probe,
                                     SENSOR_MGR_IPC_TEST_WORKER_FINISHED) == 0, "worker never finished");
        CHECK(lifecycle_probe_count(&probe,
                                     SENSOR_MGR_IPC_TEST_CANCEL_INITIALIZED) == 0, "cancel source never initialized");
        CHECK(lifecycle_probe_count(&probe,
                                     SENSOR_MGR_IPC_TEST_CANCEL_DESTROYED) == 0, "cancel source never destroyed");
        CHECK(fake_connector_connect_count(&connector_ctx) == 0, "no connect attempt was made");
        CHECK(fake_connector_close_count(&connector_ctx) == 0, "no transport close was made");

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
        CHECK(fake_connector_init(&connector_ctx, 2) == SAVVY_OK, "connector_ctx initialized");
        connector_ctx.fail_all = true;
        test_recorder_t recorder;
        recorder_init(&recorder);
        sensor_mgr_ipc_config_t config = make_default_config(&connector_ctx, &recorder);
        config.connect_timeout_ms = 5000;
        sensor_mgr_ipc_client_t *client = NULL;
        CHECK(sensor_mgr_ipc_client_create(&client, &config) == SAVVY_OK, "client_create succeeds");

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

        int start_thread_create_rc = pthread_create(&start_thread, NULL, race_start_thread_main, &start_arg);
        CHECK(start_thread_create_rc == 0, "pthread_create(start_thread) succeeds");
        bool start_thread_created = (start_thread_create_rc == 0);
        CHECK(lifecycle_probe_wait(&probe, blocked_event, 1, 2000), "probe observes blocked_event");
        int destroy_thread_create_rc = pthread_create(&destroy_thread, NULL, race_destroy_thread_main,
                              &destroy_arg);
        CHECK(destroy_thread_create_rc == 0, "pthread_create(destroy_thread) succeeds");
        bool destroy_thread_created = (destroy_thread_create_rc == 0);
        CHECK(lifecycle_probe_wait(&probe,
                                    SENSOR_MGR_IPC_TEST_DESTROY_WAITING_TRANSITION,
                                    1, 2000), "probe observes DESTROY_WAITING_TRANSITION");
        lifecycle_probe_release(&probe, blocked_event);

        CHECK(operation_latch_wait(&start_done, 2000), "start_done latch reached");
        CHECK(operation_latch_wait(&destroy_done, 2000), "destroy_done latch reached");
        if (start_thread_created) {
            int start_thread_join_rc = pthread_join(start_thread, NULL);
            CHECK(start_thread_join_rc == 0, "pthread_join(start_thread) succeeds");
        }
        if (destroy_thread_created) {
            int destroy_thread_join_rc = pthread_join(destroy_thread, NULL);
            CHECK(destroy_thread_join_rc == 0, "pthread_join(destroy_thread) succeeds");
        }
        CHECK(start_arg.status == SAVVY_OK, "start wins the destroy race");
        CHECK(lifecycle_probe_count(&probe,
                                     SENSOR_MGR_IPC_TEST_START_AFTER_WORKER_CREATE) == 1, "start reached AFTER_WORKER_CREATE once");
        CHECK(lifecycle_probe_count(&probe,
                                     SENSOR_MGR_IPC_TEST_WORKER_ENTERED) == 1, "worker entered once");
        CHECK(lifecycle_probe_count(&probe,
                                     SENSOR_MGR_IPC_TEST_WORKER_FINISHED) == 1, "worker finished once");
        CHECK(lifecycle_probe_count(&probe,
                                     SENSOR_MGR_IPC_TEST_CANCEL_INITIALIZED) == 1, "cancel source initialized once");
        CHECK(lifecycle_probe_count(&probe,
                                     SENSOR_MGR_IPC_TEST_CANCEL_DESTROYED) == 1, "cancel source destroyed once");
        CHECK(fake_connector_close_count(&connector_ctx) == 0, "no transport close occurred");

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
        CHECK(fake_connector_init(&connector_ctx, 2) == SAVVY_OK, "connector_ctx initialized");
        CHECK(fake_connector_enable_worker_tracking(&connector_ctx) == SAVVY_OK, "worker tracking enabled");
        test_recorder_t recorder;
        recorder_init(&recorder);
        callback_barrier_t callback_barrier;
        callback_barrier_init(&callback_barrier);
        sensor_mgr_ipc_config_t config = make_default_config(&connector_ctx, &recorder);
        config.recv_poll_timeout_ms = 5;
        sensor_mgr_ipc_client_t *client = NULL;
        CHECK(sensor_mgr_ipc_client_create(&client, &config) == SAVVY_OK, "client_create succeeds");

        lifecycle_probe_t probe;
        lifecycle_probe_init(&probe, client);
        recorder.callback_client = client;
        recorder.callback_lifecycle_action = 1;
        recorder.callback_barrier = &callback_barrier;
        CHECK(sensor_mgr_ipc_client_start(client) == SAVVY_OK, "client_start succeeds");
        int mgr_fd = fake_mgr_dequeue_fd(&connector_ctx);
        CHECK(mgr_fd >= 0, "mgr fd dequeued");
        char buf[256];
        size_t len = 0;
        CHECK(fake_mgr_recv(mgr_fd, buf, sizeof(buf), &len, 2000), "mgr receives handshake");
        CHECK(len > 0, "handshake payload is non-empty");
        callback_barrier_wait_action(&callback_barrier);
        CHECK(fake_connector_wait_worker_started(&connector_ctx, 1, 2000), "worker started");

        operation_latch_t start_done;
        operation_latch_t destroy_done;
        operation_latch_init(&start_done);
        operation_latch_init(&destroy_done);
        race_thread_arg_t start_arg = {client, SAVVY_ERR_UNKNOWN, &start_done};
        race_thread_arg_t destroy_arg = {client, SAVVY_ERR_UNKNOWN, &destroy_done};
        pthread_t start_thread;
        pthread_t destroy_thread;
        int start_thread_create_rc = pthread_create(&start_thread, NULL, race_start_thread_main, &start_arg);
        CHECK(start_thread_create_rc == 0, "pthread_create(start_thread) succeeds");
        bool start_thread_created = (start_thread_create_rc == 0);
        CHECK(lifecycle_probe_wait(&probe, SENSOR_MGR_IPC_TEST_JOIN_CLAIMED,
                                    1, 2000), "probe observes JOIN_CLAIMED");
        int destroy_thread_create_rc = pthread_create(&destroy_thread, NULL, race_destroy_thread_main,
                              &destroy_arg);
        CHECK(destroy_thread_create_rc == 0, "pthread_create(destroy_thread) succeeds");
        bool destroy_thread_created = (destroy_thread_create_rc == 0);
        CHECK(lifecycle_probe_wait(&probe, SENSOR_MGR_IPC_TEST_DESTROY_CLAIMED,
                                    1, 2000), "probe observes DESTROY_CLAIMED");
        CHECK(lifecycle_probe_wait(&probe, SENSOR_MGR_IPC_TEST_WAITING_JOIN,
                                    1, 2000), "probe observes WAITING_JOIN");
        callback_barrier_release(&callback_barrier);

        CHECK(operation_latch_wait(&start_done, 2000), "start_done latch reached");
        CHECK(operation_latch_wait(&destroy_done, 2000), "destroy_done latch reached");
        if (start_thread_created) {
            int start_thread_join_rc = pthread_join(start_thread, NULL);
            CHECK(start_thread_join_rc == 0, "pthread_join(start_thread) succeeds");
        }
        if (destroy_thread_created) {
            int destroy_thread_join_rc = pthread_join(destroy_thread, NULL);
            CHECK(destroy_thread_join_rc == 0, "pthread_join(destroy_thread) succeeds");
        }
        CHECK(start_arg.status == SAVVY_ERR_INVALID_ARGUMENT, "start loses the destroy race");
        CHECK(fake_connector_wait_worker_exited(&connector_ctx, 1, 2000), "worker exited");
        CHECK(fake_connector_worker_started_count(&connector_ctx) == 1, "worker started count is 1");
        CHECK(fake_connector_worker_exited_count(&connector_ctx) == 1, "worker exited count is 1");
        CHECK(fake_connector_worker_active_count(&connector_ctx) == 0, "worker active count is 0");
        CHECK(fake_connector_connect_count(&connector_ctx) == 1, "exactly one connect occurred");
        CHECK(fake_connector_close_count(&connector_ctx) == 1, "exactly one close occurred");
        CHECK(lifecycle_probe_count(&probe,
                                     SENSOR_MGR_IPC_TEST_START_AFTER_WORKER_CREATE) == 1, "start reached AFTER_WORKER_CREATE once");
        CHECK(lifecycle_probe_count(&probe,
                                     SENSOR_MGR_IPC_TEST_WORKER_ENTERED) == 1, "worker entered once");
        CHECK(lifecycle_probe_count(&probe,
                                     SENSOR_MGR_IPC_TEST_WORKER_FINISHED) == 1, "worker finished once");
        CHECK(lifecycle_probe_count(&probe,
                                     SENSOR_MGR_IPC_TEST_CANCEL_INITIALIZED) == 1, "cancel source initialized once");
        CHECK(lifecycle_probe_count(&probe,
                                     SENSOR_MGR_IPC_TEST_CANCEL_DESTROYED) == 1, "cancel source destroyed once");

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
        CHECK(fake_connector_init(&connector_ctx, 2) == SAVVY_OK, "connector_ctx initialized");
        test_recorder_t recorder;
        recorder_init(&recorder);
        sensor_mgr_ipc_config_t config = make_default_config(&connector_ctx, &recorder);
        /* Keep the stress focused on send/close ownership rather than the
         * intentionally bounded recv poll; each shutdown still traverses
         * the same state_lock -> io_lock path. */
        config.recv_poll_timeout_ms = 5;
        sensor_mgr_ipc_client_t *client = NULL;
        CHECK(sensor_mgr_ipc_client_create(&client, &config) == SAVVY_OK, "client_create succeeds");
        CHECK(sensor_mgr_ipc_client_start(client) == SAVVY_OK, "client_start succeeds");
        int mgr_fd = fake_mgr_dequeue_fd(&connector_ctx);
        CHECK(mgr_fd >= 0, "mgr fd dequeued");
        char buf[256];
        size_t len = 0;
        CHECK(fake_mgr_recv(mgr_fd, buf, sizeof(buf), &len, 2000), "mgr receives handshake");

        lifecycle_thread_arg_t send_arg = {client, SAVVY_ERR_UNKNOWN};
        lifecycle_thread_arg_t shutdown_arg = {client, SAVVY_ERR_UNKNOWN};
        pthread_t send_thread;
        pthread_t shutdown_thread;
        int send_thread_create_rc = pthread_create(&send_thread, NULL, send_thread_main, &send_arg);
        CHECK(send_thread_create_rc == 0, "pthread_create(send_thread) succeeds");
        bool send_thread_created = (send_thread_create_rc == 0);
        int shutdown_thread_create_rc;
        if ((i % 2) == 0) {
            shutdown_thread_create_rc = pthread_create(&shutdown_thread, NULL, stop_thread_main, &shutdown_arg);
            CHECK(shutdown_thread_create_rc == 0, "pthread_create(shutdown_thread, stop) succeeds");
        } else {
            shutdown_thread_create_rc = pthread_create(&shutdown_thread, NULL, destroy_thread_main, &shutdown_arg);
            CHECK(shutdown_thread_create_rc == 0, "pthread_create(shutdown_thread, destroy) succeeds");
        }
        bool shutdown_thread_created = (shutdown_thread_create_rc == 0);
        if (send_thread_created) {
            int send_thread_join_rc = pthread_join(send_thread, NULL);
            CHECK(send_thread_join_rc == 0, "pthread_join(send_thread) succeeds");
        }
        if (shutdown_thread_created) {
            int shutdown_thread_join_rc = pthread_join(shutdown_thread, NULL);
            CHECK(shutdown_thread_join_rc == 0, "pthread_join(shutdown_thread) succeeds");
        }
        CHECK(send_arg.status == SAVVY_OK || send_arg.status == SAVVY_ERR_NOT_CONNECTED ||
               send_arg.status == SAVVY_ERR_CLOSED || send_arg.status == SAVVY_ERR_INVALID_ARGUMENT,
               "send resolves to one of the expected racing outcomes");

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
        CHECK(fake_connector_init(&connector_ctx, 2) == SAVVY_OK, "connector_ctx initialized");
        test_recorder_t recorder;
        recorder_init(&recorder);
        sensor_mgr_ipc_config_t config = make_default_config(&connector_ctx, &recorder);
        sensor_mgr_ipc_client_t *client = NULL;
        CHECK(sensor_mgr_ipc_client_create(&client, &config) == SAVVY_OK, "client_create succeeds");

        lifecycle_thread_arg_t first = {client, SAVVY_ERR_UNKNOWN};
        lifecycle_thread_arg_t second = {client, SAVVY_ERR_UNKNOWN};
        pthread_t first_thread;
        pthread_t second_thread;
        bool first_thread_created = false;
        bool second_thread_created = false;
        if ((i % 3) == 0) {
            int first_thread_create_rc = pthread_create(&first_thread, NULL, stop_thread_main, &first);
            CHECK(first_thread_create_rc == 0, "pthread_create(first_thread, stop) succeeds");
            first_thread_created = (first_thread_create_rc == 0);
            int second_thread_create_rc = pthread_create(&second_thread, NULL, stop_thread_main, &second);
            CHECK(second_thread_create_rc == 0, "pthread_create(second_thread, stop) succeeds");
            second_thread_created = (second_thread_create_rc == 0);
            if (first_thread_created) {
                int first_thread_join_rc = pthread_join(first_thread, NULL);
                CHECK(first_thread_join_rc == 0, "pthread_join(first_thread) succeeds");
            }
            if (second_thread_created) {
                int second_thread_join_rc = pthread_join(second_thread, NULL);
                CHECK(second_thread_join_rc == 0, "pthread_join(second_thread) succeeds");
            }
            CHECK(first.status == SAVVY_OK && second.status == SAVVY_OK, "both concurrent stops succeed");
            sensor_mgr_ipc_client_destroy(client);
        } else if ((i % 3) == 1) {
            int first_thread_create_rc = pthread_create(&first_thread, NULL, stop_thread_main, &first);
            CHECK(first_thread_create_rc == 0, "pthread_create(first_thread, stop) succeeds");
            first_thread_created = (first_thread_create_rc == 0);
            int second_thread_create_rc = pthread_create(&second_thread, NULL, destroy_thread_main, &second);
            CHECK(second_thread_create_rc == 0, "pthread_create(second_thread, destroy) succeeds");
            second_thread_created = (second_thread_create_rc == 0);
            if (first_thread_created) {
                int first_thread_join_rc = pthread_join(first_thread, NULL);
                CHECK(first_thread_join_rc == 0, "pthread_join(first_thread) succeeds");
            }
            if (second_thread_created) {
                int second_thread_join_rc = pthread_join(second_thread, NULL);
                CHECK(second_thread_join_rc == 0, "pthread_join(second_thread) succeeds");
            }
            CHECK(first.status == SAVVY_OK || first.status == SAVVY_ERR_INVALID_ARGUMENT, "concurrent stop-vs-destroy resolves to an expected status");
        } else {
            int first_thread_create_rc = pthread_create(&first_thread, NULL, destroy_thread_main, &first);
            CHECK(first_thread_create_rc == 0, "pthread_create(first_thread, destroy) succeeds");
            first_thread_created = (first_thread_create_rc == 0);
            int second_thread_create_rc = pthread_create(&second_thread, NULL, destroy_thread_main, &second);
            CHECK(second_thread_create_rc == 0, "pthread_create(second_thread, destroy) succeeds");
            second_thread_created = (second_thread_create_rc == 0);
            if (first_thread_created) {
                int first_thread_join_rc = pthread_join(first_thread, NULL);
                CHECK(first_thread_join_rc == 0, "pthread_join(first_thread) succeeds");
            }
            if (second_thread_created) {
                int second_thread_join_rc = pthread_join(second_thread, NULL);
                CHECK(second_thread_join_rc == 0, "pthread_join(second_thread) succeeds");
            }
        }
        fake_connector_destroy(&connector_ctx);
    }
}

static void test_callback_lifecycle_operations(void) {
    const int cycles = 500;
    for (int i = 0; i < cycles; i++) {
        int scenario = i % 3;
        fake_connector_ctx_t connector_ctx;
        CHECK(fake_connector_init(&connector_ctx, 2) == SAVVY_OK, "connector_ctx initialized");
        /* Scenario 2 transfers terminal ownership to a detached callback
         * worker. Track the worker itself so the next stress batch never
         * samples a merely signalled-but-not-yet-exited predecessor. */
        CHECK(fake_connector_enable_worker_tracking(&connector_ctx) == SAVVY_OK, "worker tracking enabled");
        test_recorder_t recorder;
        recorder_init(&recorder);
        callback_barrier_t barrier;
        callback_barrier_init(&barrier);
        sensor_mgr_ipc_config_t config = make_default_config(&connector_ctx, &recorder);
        config.recv_poll_timeout_ms = 5;
        sensor_mgr_ipc_client_t *client = NULL;
        CHECK(sensor_mgr_ipc_client_create(&client, &config) == SAVVY_OK, "client_create succeeds");
        recorder.callback_client = client;
        recorder.callback_lifecycle_action = scenario == 2 ? 2 : 1;
        recorder.callback_barrier = &barrier;
        CHECK(sensor_mgr_ipc_client_start(client) == SAVVY_OK, "client_start succeeds");

        int mgr_fd = fake_mgr_dequeue_fd(&connector_ctx);
        CHECK(mgr_fd >= 0, "mgr fd dequeued");
        char buf[256];
        size_t len = 0;
        CHECK(fake_mgr_recv(mgr_fd, buf, sizeof(buf), &len, 2000), "mgr receives handshake");
        CHECK(len > 0, "handshake payload is non-empty");
        callback_barrier_wait_action(&barrier);

        lifecycle_thread_arg_t operation = {client, SAVVY_ERR_UNKNOWN};
        pthread_t operation_thread;
        bool operation_thread_created = false;
        if (scenario == 0) {
            /* callback-stop + immediate external destroy: destroy cannot
             * return/free until the blocked callback is released and the
             * joinable worker completes terminal cleanup. */
            int operation_thread_create_rc = pthread_create(&operation_thread, NULL, destroy_thread_main,
                                  &operation);
            CHECK(operation_thread_create_rc == 0, "pthread_create(operation_thread, destroy) succeeds");
            operation_thread_created = (operation_thread_create_rc == 0);
            callback_barrier_release(&barrier);
            if (operation_thread_created) {
                int operation_thread_join_rc = pthread_join(operation_thread, NULL);
                CHECK(operation_thread_join_rc == 0, "pthread_join(operation_thread) succeeds");
            }
        } else if (scenario == 1) {
            /* callback-stop + immediate restart: restart must reap the old
             * worker before initializing a fresh cancel source. */
            recorder.callback_lifecycle_action = 0;
            int operation_thread_create_rc = pthread_create(&operation_thread, NULL, start_thread_main,
                                  &operation);
            CHECK(operation_thread_create_rc == 0, "pthread_create(operation_thread, start) succeeds");
            operation_thread_created = (operation_thread_create_rc == 0);
            callback_barrier_release(&barrier);
            if (operation_thread_created) {
                int operation_thread_join_rc = pthread_join(operation_thread, NULL);
                CHECK(operation_thread_join_rc == 0, "pthread_join(operation_thread) succeeds");
            }
            CHECK(operation.status == SAVVY_OK, "restart succeeds");

            int restarted_mgr_fd = fake_mgr_dequeue_fd(&connector_ctx);
            CHECK(restarted_mgr_fd >= 0, "restarted mgr fd dequeued");
            CHECK(fake_mgr_recv(restarted_mgr_fd, buf, sizeof(buf), &len, 2000), "mgr receives restarted handshake");
            CHECK(wait_until_ge(get_connect_count, &recorder, 2, 2000), "second connect observed after restart");
            fake_mgr_close(restarted_mgr_fd);
            CHECK(sensor_mgr_ipc_client_stop(client) == SAVVY_OK, "client_stop succeeds");
            sensor_mgr_ipc_client_destroy(client);
        } else {
            /* callback-destroy has already claimed detached terminal
             * ownership. A concurrent stop is safely rejected/no-op. */
            int operation_thread_create_rc = pthread_create(&operation_thread, NULL, stop_thread_main,
                                  &operation);
            CHECK(operation_thread_create_rc == 0, "pthread_create(operation_thread, stop) succeeds");
            operation_thread_created = (operation_thread_create_rc == 0);
            if (operation_thread_created) {
                int operation_thread_join_rc = pthread_join(operation_thread, NULL);
                CHECK(operation_thread_join_rc == 0, "pthread_join(operation_thread) succeeds");
            }
            CHECK(operation.status == SAVVY_ERR_INVALID_ARGUMENT, "concurrent stop is rejected after callback-destroy claimed ownership");
            callback_barrier_release(&barrier);
            CHECK(fake_connector_wait_close_count(&connector_ctx, 1, 2000), "transport close observed");
            CHECK(wait_until_ge(get_disconnect_count, &recorder, 1, 2000), "disconnect observed");
        }

        int expected_workers = scenario == 1 ? 2 : 1;
        CHECK(fake_connector_wait_worker_exited(&connector_ctx,
                                                 expected_workers, 2000), "expected worker count exited");
        CHECK(fake_connector_worker_started_count(&connector_ctx) ==
               expected_workers, "worker started count matches expectation");
        CHECK(fake_connector_worker_exited_count(&connector_ctx) ==
               expected_workers, "worker exited count matches expectation");
        CHECK(fake_connector_worker_active_count(&connector_ctx) == 0, "no worker remains active");
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
        CHECK(fake_connector_init(&connector_ctx, 4) == SAVVY_OK, "connector_ctx initialized");
        connector_ctx.fail_all = true;

        test_recorder_t recorder;
        recorder_init(&recorder);
        sensor_mgr_ipc_config_t config = make_default_config(&connector_ctx, &recorder);
        config.connect_timeout_ms = 5000; /* deliberately long - cancel must still wake it promptly */

        sensor_mgr_ipc_client_t *client = NULL;
        CHECK(sensor_mgr_ipc_client_create(&client, &config) == SAVVY_OK, "client_create succeeds");
        CHECK(sensor_mgr_ipc_client_start(client) == SAVVY_OK, "client_start succeeds");
        sleep_ms_test(50); /* let the worker thread actually enter the blocked connect */

        int64_t t0 = now_ms_test();
        CHECK(sensor_mgr_ipc_client_stop(client) == SAVVY_OK, "stop succeeds while blocked in connect");
        int64_t elapsed = now_ms_test() - t0;
        CHECK(elapsed < 1000, "stop wakes the blocked connect promptly");

        /* Idempotent: a second stop() must also return promptly and safely. */
        t0 = now_ms_test();
        CHECK(sensor_mgr_ipc_client_stop(client) == SAVVY_OK, "second stop is idempotent");
        CHECK(now_ms_test() - t0 < 100, "second stop returns promptly");

        sensor_mgr_ipc_client_destroy(client);
        fake_connector_destroy(&connector_ctx);
    }

    /* Case B: shutdown while blocked in recv() after a successful
     * connect - bounded by recv_poll_timeout_ms (Foundation's transport
     * recv() takes no cancel source of its own), not instant; this test
     * uses a short, realistic poll interval and asserts the bound holds. */
    {
        fake_connector_ctx_t connector_ctx;
        CHECK(fake_connector_init(&connector_ctx, 4) == SAVVY_OK, "connector_ctx initialized");

        test_recorder_t recorder;
        recorder_init(&recorder);
        sensor_mgr_ipc_config_t config = make_default_config(&connector_ctx, &recorder);
        config.recv_poll_timeout_ms = 150;

        sensor_mgr_ipc_client_t *client = NULL;
        CHECK(sensor_mgr_ipc_client_create(&client, &config) == SAVVY_OK, "client_create succeeds");
        CHECK(sensor_mgr_ipc_client_start(client) == SAVVY_OK, "client_start succeeds");

        int mgr_fd = fake_mgr_dequeue_fd(&connector_ctx);
        CHECK(mgr_fd >= 0, "mgr fd dequeued");
        char buf[256];
        size_t len = 0;
        fake_mgr_recv(mgr_fd, buf, sizeof(buf), &len, 2000); /* drain CONNECT handshake */
        CHECK(wait_until_ge(get_connect_count, &recorder, 1, 2000), "connect observed");

        int64_t t0 = now_ms_test();
        CHECK(sensor_mgr_ipc_client_stop(client) == SAVVY_OK, "stop succeeds while blocked in recv poll");
        int64_t elapsed = now_ms_test() - t0;
        CHECK(elapsed < 1000, "stop wakes the blocked recv poll within bound"); /* generous margin over the 150ms poll bound */

        fake_mgr_close(mgr_fd);
        sensor_mgr_ipc_client_destroy(client);
        fake_connector_destroy(&connector_ctx);
    }

    /* Case C: destroy itself owns the blocked-connect shutdown, so no
     * caller may race a free against cancel-source/worker teardown. */
    {
        fake_connector_ctx_t connector_ctx;
        CHECK(fake_connector_init(&connector_ctx, 2) == SAVVY_OK, "connector_ctx initialized");
        connector_ctx.fail_all = true;
        test_recorder_t recorder;
        recorder_init(&recorder);
        sensor_mgr_ipc_config_t config = make_default_config(&connector_ctx, &recorder);
        config.connect_timeout_ms = 5000;
        sensor_mgr_ipc_client_t *client = NULL;
        CHECK(sensor_mgr_ipc_client_create(&client, &config) == SAVVY_OK, "client_create succeeds");
        CHECK(sensor_mgr_ipc_client_start(client) == SAVVY_OK, "client_start succeeds");
        sleep_ms_test(50);
        lifecycle_thread_arg_t destroy_arg = {client, SAVVY_ERR_UNKNOWN};
        pthread_t destroy_thread;
        int destroy_thread_create_rc = pthread_create(&destroy_thread, NULL, destroy_thread_main, &destroy_arg);
        CHECK(destroy_thread_create_rc == 0, "pthread_create(destroy_thread) succeeds");
        bool destroy_thread_created = (destroy_thread_create_rc == 0);
        if (destroy_thread_created) {
            int destroy_thread_join_rc = pthread_join(destroy_thread, NULL);
            CHECK(destroy_thread_join_rc == 0, "pthread_join(destroy_thread) succeeds");
        }
        fake_connector_destroy(&connector_ctx);
    }

    /* Case D: destroy while the worker is inside its bounded recv poll. */
    {
        fake_connector_ctx_t connector_ctx;
        CHECK(fake_connector_init(&connector_ctx, 2) == SAVVY_OK, "connector_ctx initialized");
        test_recorder_t recorder;
        recorder_init(&recorder);
        sensor_mgr_ipc_config_t config = make_default_config(&connector_ctx, &recorder);
        config.recv_poll_timeout_ms = 150;
        sensor_mgr_ipc_client_t *client = NULL;
        CHECK(sensor_mgr_ipc_client_create(&client, &config) == SAVVY_OK, "client_create succeeds");
        CHECK(sensor_mgr_ipc_client_start(client) == SAVVY_OK, "client_start succeeds");
        int mgr_fd = fake_mgr_dequeue_fd(&connector_ctx);
        CHECK(mgr_fd >= 0, "mgr fd dequeued");
        char buf[256];
        size_t len = 0;
        CHECK(fake_mgr_recv(mgr_fd, buf, sizeof(buf), &len, 2000), "mgr receives handshake");

        lifecycle_thread_arg_t destroy_arg = {client, SAVVY_ERR_UNKNOWN};
        pthread_t destroy_thread;
        int64_t t0 = now_ms_test();
        int destroy_thread_create_rc = pthread_create(&destroy_thread, NULL, destroy_thread_main, &destroy_arg);
        CHECK(destroy_thread_create_rc == 0, "pthread_create(destroy_thread) succeeds");
        bool destroy_thread_created = (destroy_thread_create_rc == 0);
        if (destroy_thread_created) {
            int destroy_thread_join_rc = pthread_join(destroy_thread, NULL);
            CHECK(destroy_thread_join_rc == 0, "pthread_join(destroy_thread) succeeds");
        }
        CHECK(now_ms_test() - t0 < 1000, "destroy wakes the blocked recv poll within bound");
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
