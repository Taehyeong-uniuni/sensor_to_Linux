/* SNS-CORE-003a, SNS-CORE-003b (mgr_ipc angle), CT-IPC-002, SNS-CORE-006,
 * SNS-CORE-007 (mgr_ipc angle). Dispatches on argv[1]. */

/* clock_gettime()/CLOCK_MONOTONIC are POSIX.1-2008; glibc hides them under
 * -std=c11 without this (see src/core/clock.c for Foundation's own
 * instance of the same fix). Must be defined before any system header. */
#define _POSIX_C_SOURCE 200809L

#include "fake_transport.h"
#include "mgr_ipc_client.h"
#include "savvy/protocol/ipc_envelope.h"

#include <assert.h>
#include <dirent.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

typedef struct test_recorder {
    pthread_mutex_t lock;
    int connect_count;
    int reconnect_count;
    int disconnect_count;
    char actions[32][128];
    int action_count;
    sensor_mgr_ipc_client_t *callback_client;
    int callback_lifecycle_action; /* 0 none, 1 stop, 2 destroy */
} test_recorder_t;

static void recorder_init(test_recorder_t *r) {
    pthread_mutex_init(&r->lock, NULL);
    r->connect_count = 0;
    r->reconnect_count = 0;
    r->disconnect_count = 0;
    r->action_count = 0;
    r->callback_client = NULL;
    r->callback_lifecycle_action = 0;
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

    const char *payload = "{\"SENSOR\":\"PIR\",\"STATE\":\"1\"}";
    /* Same local-state-derived report attempted twice while disconnected
     * (mirrors "drop, then the same state recurs") - mgr_ipc's own
     * concern is that repeated drops are uniformly safe/idempotent with
     * no special-cased second-attempt behavior; the semantic decision of
     * *whether* to even attempt a second send for an unchanged value is
     * state_report's tracker (see its own SNS-CORE-003b test). */
    assert(sensor_mgr_ipc_client_send(client, SENSOR_MGR_IPC_ACTION_GETSTATE, payload) == SAVVY_ERR_NOT_CONNECTED);
    assert(sensor_mgr_ipc_client_send(client, SENSOR_MGR_IPC_ACTION_GETSTATE, payload) == SAVVY_ERR_NOT_CONNECTED);
    assert(sensor_mgr_ipc_client_is_connected(client) == false);

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

static void test_application_message_size_boundaries(void) {
    const char *empty_payload = "{\"x\":\"\"}";
    char *text = NULL;
    size_t base_len = 0;
    assert(savvy_ipc_envelope_build(SENSOR_MGR_IPC_ACTION_CONNECT, empty_payload,
                                    &text, &base_len) == SAVVY_OK);
    free(text);

    size_t payload_len = strlen(empty_payload) + (SAVVY_IPC_MAX_MESSAGE - base_len);
    char *payload = malloc(payload_len + 2u);
    assert(payload != NULL);
    const char *prefix = "{\"x\":\"";
    const char *suffix = "\"}";
    size_t prefix_len = strlen(prefix);
    size_t suffix_len = strlen(suffix);
    assert(prefix_len + suffix_len <= payload_len);
    memcpy(payload, prefix, prefix_len);
    memset(payload + prefix_len, 'x', payload_len - prefix_len - suffix_len);
    memcpy(payload + payload_len - suffix_len, suffix, suffix_len);
    payload[payload_len] = '\0';

    size_t encoded_len = 0;
    assert(savvy_ipc_envelope_build(SENSOR_MGR_IPC_ACTION_CONNECT, payload,
                                    &text, &encoded_len) == SAVVY_OK);
    assert(encoded_len == SAVVY_IPC_MAX_MESSAGE);
    free(text);

    /* Inserting one more JSON character before the suffix makes the encoded
     * application record 65,537 bytes and must be rejected before send. */
    memmove(payload + payload_len - suffix_len + 1u,
            payload + payload_len - suffix_len, suffix_len + 1u);
    payload[payload_len - suffix_len] = 'y';
    payload_len += 1u;
    assert(savvy_ipc_envelope_build(SENSOR_MGR_IPC_ACTION_CONNECT, payload,
                                    &text, &encoded_len) == SAVVY_ERR_OVERFLOW);
    free(payload);
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
    const savvy_status_t failures[] = {SAVVY_ERR_TIMEOUT, SAVVY_ERR_CLOSED, SAVVY_ERR_IO};
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

static void test_006_repeated_connect_disconnect(void) {
    fake_connector_ctx_t connector_ctx;
    assert(fake_connector_init(&connector_ctx, 4) == SAVVY_OK);

    test_recorder_t recorder;
    recorder_init(&recorder);
    sensor_mgr_ipc_config_t config = make_default_config(&connector_ctx, &recorder);
    config.reconnect_backoff_ms = 5;

    long fds_baseline = count_open_fds();
    sensor_mgr_ipc_client_t *client = NULL;
    assert(sensor_mgr_ipc_client_create(&client, &config) == SAVVY_OK);
    assert(sensor_mgr_ipc_client_start(client) == SAVVY_OK);

    const int cycles = 60;
    long fds_before = -1;
    for (int i = 0; i < cycles; i++) {
        int mgr_fd = fake_mgr_dequeue_fd(&connector_ctx);
        assert(mgr_fd >= 0);
        char buf[256];
        size_t len = 0;
        fake_mgr_recv(mgr_fd, buf, sizeof(buf), &len, 500); /* drain CONNECT handshake */
        fake_mgr_close(mgr_fd);
        if (i == 4) {
            fds_before = count_open_fds();
        }
    }
    assert(wait_until_ge(get_disconnect_count, &recorder, cycles - 5, 5000));

    long fds_after = count_open_fds();
    assert(fds_before >= 0 && fds_after >= 0);
    /* Must not grow with the number of cycles - small constant slack for
     * whatever transient fds this test loop itself still has open. */
    assert(fds_after <= fds_before + 4);

    sensor_mgr_ipc_client_stop(client);
    long fds_final = count_open_fds();
    assert(fds_baseline >= 0 && fds_final >= 0);
    /* count_open_fds itself opens the directory it reads, so both samples
     * have the same one-descriptor measurement overhead. No client fd may
     * remain after stop/join. */
    assert(fds_final <= fds_baseline + 1);
    sensor_mgr_ipc_client_destroy(client);
    fake_connector_destroy(&connector_ctx);
    printf("SNS-CORE-006: OK (baseline=%ld cycle=%ld final=%ld over %d cycles)\n",
           fds_baseline, fds_after, fds_final, cycles);
}

typedef struct lifecycle_thread_arg {
    sensor_mgr_ipc_client_t *client;
    savvy_status_t status;
} lifecycle_thread_arg_t;

static void *stop_thread_main(void *arg) {
    lifecycle_thread_arg_t *thread_arg = (lifecycle_thread_arg_t *)arg;
    thread_arg->status = sensor_mgr_ipc_client_stop(thread_arg->client);
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
    for (int i = 0; i < 100; i++) {
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
    for (int action = 1; action <= 2; action++) {
        fake_connector_ctx_t connector_ctx;
        assert(fake_connector_init(&connector_ctx, 2) == SAVVY_OK);
        test_recorder_t recorder;
        recorder_init(&recorder);
        sensor_mgr_ipc_config_t config = make_default_config(&connector_ctx, &recorder);
        sensor_mgr_ipc_client_t *client = NULL;
        assert(sensor_mgr_ipc_client_create(&client, &config) == SAVVY_OK);
        recorder.callback_client = client;
        recorder.callback_lifecycle_action = action;
        assert(sensor_mgr_ipc_client_start(client) == SAVVY_OK);

        int mgr_fd = fake_mgr_dequeue_fd(&connector_ctx);
        assert(mgr_fd >= 0);
        char buf[256];
        size_t len = 0;
        assert(fake_mgr_recv(mgr_fd, buf, sizeof(buf), &len, 2000));
        assert(len > 0);
        fake_mgr_close(mgr_fd);
        sleep_ms_test(100);

        if (action == 1) {
            sensor_mgr_ipc_client_destroy(client);
        }
        /* action == 2 has already deferred and completed destroy on the
         * worker; do not dereference client again. */
        sleep_ms_test(50);
        fake_connector_destroy(&connector_ctx);
    }
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
