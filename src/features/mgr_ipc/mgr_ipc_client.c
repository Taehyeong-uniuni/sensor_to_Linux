/* clock_gettime()/CLOCK_REALTIME are POSIX.1-2008. */
#define _POSIX_C_SOURCE 200809L

#include "mgr_ipc_client.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "savvy/platform/ipc_reconnect.h"
#include "savvy/protocol/ipc_action_catalog.h"
#include "savvy/protocol/ipc_envelope.h"

struct sensor_mgr_ipc_client {
    sensor_mgr_ipc_config_t config;

    savvy_ipc_cancel_source_t cancel_source;
    savvy_ipc_reconnect_tracker_t reconnect_tracker;

    /* Lock order is state_lock -> io_lock.  The latter pins the original
     * transport object through send/recv/close; transport structs and fds
     * are never copied out for I/O. */
    pthread_mutex_t state_lock;
    pthread_mutex_t io_lock;
    savvy_ipc_transport_t transport;
    bool connected;

    pthread_mutex_t lifecycle_lock;
    pthread_cond_t shutdown_cond;
    bool started;
    bool shutdown_requested;
    bool stop_complete;
    bool cancel_initialized;
    bool worker_detached;
    pthread_t worker_thread;

    /* The public API is a raw pointer ABI.  A process-lifetime registry
     * lets a concurrently-entering operation validate/pin that pointer
     * before it dereferences client storage; destroy waits existing pins. */
    bool destroy_requested;
    bool destroy_from_worker;
    size_t api_callers;
    struct sensor_mgr_ipc_client *registry_next;
};

static pthread_mutex_t g_registry_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_registry_idle = PTHREAD_COND_INITIALIZER;
static sensor_mgr_ipc_client_t *g_registry_clients = NULL;

static void *worker_main(void *arg);
static void finalize_destroy(sensor_mgr_ipc_client_t *client);

static void registry_add(sensor_mgr_ipc_client_t *client) {
    pthread_mutex_lock(&g_registry_lock);
    client->registry_next = g_registry_clients;
    g_registry_clients = client;
    pthread_mutex_unlock(&g_registry_lock);
}

static bool client_enter(sensor_mgr_ipc_client_t *candidate) {
    if (candidate == NULL) {
        return false;
    }

    pthread_mutex_lock(&g_registry_lock);
    for (sensor_mgr_ipc_client_t *current = g_registry_clients;
         current != NULL;
         current = current->registry_next) {
        if (current == candidate) {
            if (current->destroy_requested) {
                pthread_mutex_unlock(&g_registry_lock);
                return false;
            }
            current->api_callers += 1;
            pthread_mutex_unlock(&g_registry_lock);
            return true;
        }
    }
    pthread_mutex_unlock(&g_registry_lock);
    return false;
}

static void client_leave(sensor_mgr_ipc_client_t *client) {
    pthread_mutex_lock(&g_registry_lock);
    client->api_callers -= 1;
    pthread_cond_broadcast(&g_registry_idle);
    pthread_mutex_unlock(&g_registry_lock);
}

static bool client_claim_destroy(sensor_mgr_ipc_client_t *client, bool from_worker) {
    pthread_mutex_lock(&g_registry_lock);
    if (client->destroy_requested) {
        pthread_mutex_unlock(&g_registry_lock);
        return false;
    }
    client->destroy_requested = true;
    client->destroy_from_worker = from_worker;
    pthread_cond_broadcast(&g_registry_idle);
    pthread_mutex_unlock(&g_registry_lock);
    return true;
}

static void wait_for_other_api_callers(sensor_mgr_ipc_client_t *client, size_t self_count) {
    pthread_mutex_lock(&g_registry_lock);
    while (client->api_callers > self_count) {
        pthread_cond_wait(&g_registry_idle, &g_registry_lock);
    }
    pthread_mutex_unlock(&g_registry_lock);
}

static void registry_remove_and_wait(sensor_mgr_ipc_client_t *client) {
    pthread_mutex_lock(&g_registry_lock);
    while (client->api_callers != 0) {
        pthread_cond_wait(&g_registry_idle, &g_registry_lock);
    }
    sensor_mgr_ipc_client_t **link = &g_registry_clients;
    while (*link != NULL && *link != client) {
        link = &(*link)->registry_next;
    }
    if (*link == client) {
        *link = client->registry_next;
    }
    pthread_mutex_unlock(&g_registry_lock);
}

static bool caller_is_worker(sensor_mgr_ipc_client_t *client) {
    pthread_mutex_lock(&client->lifecycle_lock);
    bool is_worker = client->started && pthread_equal(pthread_self(), client->worker_thread);
    pthread_mutex_unlock(&client->lifecycle_lock);
    return is_worker;
}

static void close_transport(sensor_mgr_ipc_client_t *client) {
    pthread_mutex_lock(&client->state_lock);
    pthread_mutex_lock(&client->io_lock);
    if (client->connected) {
        client->transport.close(&client->transport);
        client->connected = false;
    }
    pthread_mutex_unlock(&client->io_lock);
    pthread_mutex_unlock(&client->state_lock);
}

static void worker_mark_finished(sensor_mgr_ipc_client_t *client) {
    bool worker_owns_cleanup = false;
    bool destroy_after_cleanup = false;

    pthread_mutex_lock(&client->lifecycle_lock);
    if (client->worker_detached) {
        client->started = false;
        client->stop_complete = true;
        worker_owns_cleanup = client->cancel_initialized;
        client->cancel_initialized = false;
    }
    destroy_after_cleanup = client->destroy_from_worker;
    pthread_cond_broadcast(&client->shutdown_cond);
    pthread_mutex_unlock(&client->lifecycle_lock);

    if (worker_owns_cleanup) {
        savvy_ipc_cancel_source_destroy(&client->cancel_source);
    }
    if (destroy_after_cleanup) {
        /* A callback-triggered destroy runs on this worker.  It was
         * detached before the callback returned; all future client access
         * has ended at this point. */
        finalize_destroy(client);
    }
}

static savvy_status_t stop_impl(sensor_mgr_ipc_client_t *client) {
    pthread_t worker;
    bool self_worker;
    bool do_join = false;
    bool do_cancel = false;

    pthread_mutex_lock(&client->lifecycle_lock);
    if (!client->started) {
        pthread_mutex_unlock(&client->lifecycle_lock);
        return SAVVY_OK;
    }

    self_worker = pthread_equal(pthread_self(), client->worker_thread);
    if (!client->shutdown_requested) {
        client->shutdown_requested = true;
        pthread_cond_broadcast(&client->shutdown_cond);
        worker = client->worker_thread;
        do_cancel = client->cancel_initialized;

        if (self_worker) {
            /* A worker callback cannot join itself. Detaching makes the
             * worker's terminal cleanup authoritative and prevents a
             * joinable-thread resource leak. */
            (void)pthread_detach(worker);
            client->worker_detached = true;
        } else {
            do_join = true;
        }
        pthread_mutex_unlock(&client->lifecycle_lock);
    } else {
        if (self_worker) {
            pthread_mutex_unlock(&client->lifecycle_lock);
            return SAVVY_OK;
        }
        while (!client->stop_complete) {
            pthread_cond_wait(&client->shutdown_cond, &client->lifecycle_lock);
        }
        pthread_mutex_unlock(&client->lifecycle_lock);
        return SAVVY_OK;
    }

    if (do_cancel) {
        savvy_ipc_cancel_source_cancel(&client->cancel_source);
    }
    if (!do_join) {
        return SAVVY_OK;
    }

    (void)pthread_join(worker, NULL);
    close_transport(client);

    pthread_mutex_lock(&client->lifecycle_lock);
    bool destroy_cancel = client->cancel_initialized;
    client->cancel_initialized = false;
    client->started = false;
    client->stop_complete = true;
    pthread_cond_broadcast(&client->shutdown_cond);
    pthread_mutex_unlock(&client->lifecycle_lock);

    if (destroy_cancel) {
        savvy_ipc_cancel_source_destroy(&client->cancel_source);
    }
    return SAVVY_OK;
}

savvy_status_t sensor_mgr_ipc_client_create(sensor_mgr_ipc_client_t **out_client,
                                             const sensor_mgr_ipc_config_t *config) {
    if (out_client == NULL || config == NULL || config->connector == NULL) {
        return SAVVY_ERR_INVALID_ARGUMENT;
    }

    sensor_mgr_ipc_client_t *client = calloc(1, sizeof(*client));
    if (client == NULL) {
        return SAVVY_ERR_OUT_OF_MEMORY;
    }
    client->config = *config;
    if (pthread_mutex_init(&client->state_lock, NULL) != 0) {
        free(client);
        return SAVVY_ERR_UNKNOWN;
    }
    if (pthread_mutex_init(&client->io_lock, NULL) != 0) {
        pthread_mutex_destroy(&client->state_lock);
        free(client);
        return SAVVY_ERR_UNKNOWN;
    }
    if (pthread_mutex_init(&client->lifecycle_lock, NULL) != 0) {
        pthread_mutex_destroy(&client->io_lock);
        pthread_mutex_destroy(&client->state_lock);
        free(client);
        return SAVVY_ERR_UNKNOWN;
    }
    if (pthread_cond_init(&client->shutdown_cond, NULL) != 0) {
        pthread_mutex_destroy(&client->lifecycle_lock);
        pthread_mutex_destroy(&client->io_lock);
        pthread_mutex_destroy(&client->state_lock);
        free(client);
        return SAVVY_ERR_UNKNOWN;
    }

    client->connected = false;
    client->stop_complete = true;
    registry_add(client);
    *out_client = client;
    return SAVVY_OK;
}

savvy_status_t sensor_mgr_ipc_client_start(sensor_mgr_ipc_client_t *client) {
    if (!client_enter(client)) {
        return SAVVY_ERR_INVALID_ARGUMENT;
    }

    pthread_mutex_lock(&client->lifecycle_lock);
    if (client->started) {
        pthread_mutex_unlock(&client->lifecycle_lock);
        client_leave(client);
        return SAVVY_OK;
    }

    savvy_status_t st = savvy_ipc_cancel_source_init(&client->cancel_source);
    if (st != SAVVY_OK) {
        pthread_mutex_unlock(&client->lifecycle_lock);
        client_leave(client);
        return st;
    }
    client->cancel_initialized = true;
    savvy_ipc_reconnect_tracker_init(&client->reconnect_tracker);
    client->shutdown_requested = false;
    client->stop_complete = false;
    client->worker_detached = false;

    if (pthread_create(&client->worker_thread, NULL, worker_main, client) != 0) {
        client->cancel_initialized = false;
        client->stop_complete = true;
        savvy_ipc_cancel_source_destroy(&client->cancel_source);
        pthread_mutex_unlock(&client->lifecycle_lock);
        client_leave(client);
        return SAVVY_ERR_UNKNOWN;
    }
    client->started = true;
    pthread_mutex_unlock(&client->lifecycle_lock);
    client_leave(client);
    return SAVVY_OK;
}

static savvy_status_t send_impl(sensor_mgr_ipc_client_t *client,
                                const char *action,
                                const char *payload_json) {
    const char *payload = payload_json != NULL ? payload_json : "{}";
    if (!savvy_ipc_action_known(action) ||
        savvy_ipc_action_direction(action) != SAVVY_IPC_SENSOR_TO_MGR) {
        return SAVVY_ERR_INVALID_ARGUMENT;
    }
    if (savvy_ipc_action_validate_payload(action, payload) != SAVVY_OK) {
        return SAVVY_ERR_PROTOCOL;
    }

    char *envelope = NULL;
    size_t envelope_len = 0;
    savvy_status_t st = savvy_ipc_envelope_build(action, payload, &envelope, &envelope_len);
    if (st != SAVVY_OK) {
        return st;
    }

    pthread_mutex_lock(&client->state_lock);
    pthread_mutex_lock(&client->io_lock);
    if (!client->connected) {
        st = SAVVY_ERR_NOT_CONNECTED;
    } else {
        st = client->transport.send(&client->transport, envelope, envelope_len,
                                    client->config.send_timeout_ms);
    }
    pthread_mutex_unlock(&client->io_lock);
    pthread_mutex_unlock(&client->state_lock);
    free(envelope);
    return st;
}

savvy_status_t sensor_mgr_ipc_client_send(sensor_mgr_ipc_client_t *client,
                                          const char *action, const char *payload_json) {
    if (action == NULL || !client_enter(client)) {
        return SAVVY_ERR_INVALID_ARGUMENT;
    }
    savvy_status_t st = send_impl(client, action, payload_json);
    client_leave(client);
    return st;
}

bool sensor_mgr_ipc_client_is_connected(sensor_mgr_ipc_client_t *client) {
    if (!client_enter(client)) {
        return false;
    }
    pthread_mutex_lock(&client->state_lock);
    bool connected = client->connected;
    pthread_mutex_unlock(&client->state_lock);
    client_leave(client);
    return connected;
}

savvy_status_t sensor_mgr_ipc_client_stop(sensor_mgr_ipc_client_t *client) {
    if (!client_enter(client)) {
        return SAVVY_ERR_INVALID_ARGUMENT;
    }
    savvy_status_t st = stop_impl(client);
    client_leave(client);
    return st;
}

void sensor_mgr_ipc_client_destroy(sensor_mgr_ipc_client_t *client) {
    if (!client_enter(client)) {
        return;
    }

    bool self_worker = caller_is_worker(client);
    if (!client_claim_destroy(client, self_worker)) {
        client_leave(client);
        return;
    }
    if (self_worker) {
        /* Worker callback: request terminal shutdown, then return to the
         * callback/dispatch stack. worker_mark_finished() finalizes after
         * that stack has stopped using client. */
        (void)stop_impl(client);
        client_leave(client);
        return;
    }

    /* Request worker shutdown before waiting for a pinned send. Otherwise
     * a recv worker can repeatedly reacquire state_lock between short poll
     * slices while the sender waits for it, and destroy would wait for that
     * sender without ever setting shutdown_requested (lock starvation).
     * stop_impl joins the worker before transport/cancel teardown; existing
     * API pins are still retained until the wait immediately below. */
    (void)stop_impl(client);
    wait_for_other_api_callers(client, 1);
    client_leave(client);
    finalize_destroy(client);
}

static bool is_shutdown_requested(sensor_mgr_ipc_client_t *client) {
    pthread_mutex_lock(&client->lifecycle_lock);
    bool requested = client->shutdown_requested;
    pthread_mutex_unlock(&client->lifecycle_lock);
    return requested;
}

static void interruptible_backoff_sleep(sensor_mgr_ipc_client_t *client, uint32_t ms) {
    struct timespec deadline;
    clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_sec += (time_t)(ms / 1000u);
    deadline.tv_nsec += (long)(ms % 1000u) * 1000000L;
    if (deadline.tv_nsec >= 1000000000L) {
        deadline.tv_sec += 1;
        deadline.tv_nsec -= 1000000000L;
    }

    pthread_mutex_lock(&client->lifecycle_lock);
    while (!client->shutdown_requested) {
        if (pthread_cond_timedwait(&client->shutdown_cond, &client->lifecycle_lock, &deadline) != 0) {
            break;
        }
    }
    pthread_mutex_unlock(&client->lifecycle_lock);
}

static void set_connected(sensor_mgr_ipc_client_t *client, const savvy_ipc_transport_t *transport) {
    pthread_mutex_lock(&client->state_lock);
    client->transport = *transport;
    client->connected = true;
    pthread_mutex_unlock(&client->state_lock);
}

static void mark_reconnect(void *user_data) {
    *(bool *)user_data = true;
}

static void dispatch_envelope(sensor_mgr_ipc_client_t *client, const char *buf, size_t len) {
    savvy_ipc_envelope_t env;
    if (savvy_ipc_envelope_parse(buf, len, &env) != SAVVY_OK) {
        return;
    }

    if (savvy_ipc_action_known(env.action) &&
        savvy_ipc_action_direction(env.action) == SAVVY_IPC_MGR_TO_SENSOR &&
        savvy_ipc_action_validate_payload(env.action, env.payload_json) == SAVVY_OK &&
        client->config.callbacks.on_envelope != NULL) {
        /* No state or I/O lock is held while user code runs. */
        client->config.callbacks.on_envelope(env.action, env.payload_json,
                                             client->config.callbacks.user_data);
    }
    savvy_ipc_envelope_free(&env);
}

static void notify_disconnected(sensor_mgr_ipc_client_t *client) {
    if (client->config.callbacks.on_disconnected != NULL) {
        client->config.callbacks.on_disconnected(client->config.callbacks.user_data);
    }
}

static void *worker_main(void *arg) {
    sensor_mgr_ipc_client_t *client = (sensor_mgr_ipc_client_t *)arg;

    while (!is_shutdown_requested(client)) {
        savvy_ipc_transport_t transport;
        savvy_status_t st = client->config.connector(client->config.connector_ctx,
                                                      client->config.connect_timeout_ms,
                                                      &client->cancel_source, &transport);
        if (st == SAVVY_ERR_CANCELLED) {
            break;
        }
        if (st != SAVVY_OK) {
            interruptible_backoff_sleep(client, client->config.reconnect_backoff_ms);
            continue;
        }

        set_connected(client, &transport);

        /* CONNECT is a real handshake: it must complete before the client
         * becomes observable as connected or starts receiving messages. */
        st = send_impl(client, SENSOR_MGR_IPC_ACTION_CONNECT, "{}");
        if (st != SAVVY_OK) {
            close_transport(client);
            notify_disconnected(client);
            interruptible_backoff_sleep(client, client->config.reconnect_backoff_ms);
            continue;
        }

        bool was_reconnect = false;
        savvy_ipc_reconnect_hooks_t hooks = {mark_reconnect, mark_reconnect, &was_reconnect};
        savvy_ipc_reconnect_tracker_on_connected(&client->reconnect_tracker, &hooks);
        if (client->config.callbacks.on_connected != NULL) {
            client->config.callbacks.on_connected(was_reconnect, client->config.callbacks.user_data);
        }

        while (!is_shutdown_requested(client)) {
            char buf[SAVVY_IPC_MAX_MESSAGE];
            size_t out_len = 0;

            pthread_mutex_lock(&client->state_lock);
            pthread_mutex_lock(&client->io_lock);
            if (!client->connected) {
                pthread_mutex_unlock(&client->io_lock);
                pthread_mutex_unlock(&client->state_lock);
                break;
            }
            savvy_status_t recv_st = client->transport.recv(&client->transport, buf, sizeof(buf),
                                                             &out_len, client->config.recv_poll_timeout_ms);
            pthread_mutex_unlock(&client->io_lock);
            pthread_mutex_unlock(&client->state_lock);

            if (recv_st == SAVVY_ERR_TIMEOUT || recv_st == SAVVY_ERR_OVERFLOW) {
                continue;
            }
            if (recv_st == SAVVY_OK && out_len > 0) {
                dispatch_envelope(client, buf, out_len);
                continue;
            }
            break; /* EOF, close, I/O, or malformed transport result */
        }

        close_transport(client);
        notify_disconnected(client);
    }

    close_transport(client);
    worker_mark_finished(client);
    return NULL;
}

static void finalize_destroy(sensor_mgr_ipc_client_t *client) {
    registry_remove_and_wait(client);
    pthread_cond_destroy(&client->shutdown_cond);
    pthread_mutex_destroy(&client->lifecycle_lock);
    pthread_mutex_destroy(&client->io_lock);
    pthread_mutex_destroy(&client->state_lock);
    free(client);
}
