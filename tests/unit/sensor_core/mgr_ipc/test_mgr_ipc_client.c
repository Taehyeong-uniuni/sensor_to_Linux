/* SNS-CORE-003a, SNS-CORE-003b (mgr_ipc angle), CT-IPC-002, SNS-CORE-006,
 * SNS-CORE-007 (mgr_ipc angle). Dispatches on argv[1]. */

/* clock_gettime()/CLOCK_MONOTONIC are POSIX.1-2008; glibc hides them under
 * -std=c11 without this (see src/core/clock.c for Foundation's own
 * instance of the same fix). Must be defined before any system header. */
#define _POSIX_C_SOURCE 200809L

#include "fake_transport.h"
#include "mgr_ipc_client.h"

#include <assert.h>
#include <dirent.h>
#include <pthread.h>
#include <stdio.h>
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
    sensor_mgr_ipc_client_destroy(client);
    fake_connector_destroy(&connector_ctx);
    printf("SNS-CORE-006: OK (fds_before=%ld fds_after=%ld over %d cycles)\n", fds_before, fds_after, cycles);
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
