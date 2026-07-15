/* clock_gettime()/CLOCK_REALTIME are POSIX.1-2008; glibc hides them under
 * -std=c11 without this (Apple's libc exposes them regardless, which is
 * why this only surfaces when building on Linux - see src/core/clock.c
 * for Foundation's own instance of the same fix). Must be defined before
 * any system header is included. */
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

    pthread_mutex_t state_lock;
    savvy_ipc_transport_t transport;
    bool connected;

    pthread_mutex_t lifecycle_lock;
    pthread_cond_t shutdown_cond;
    bool started;
    bool shutdown_requested;
    bool stopped;
    pthread_t worker_thread;
};

static void *worker_main(void *arg);

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
    if (pthread_mutex_init(&client->lifecycle_lock, NULL) != 0) {
        pthread_mutex_destroy(&client->state_lock);
        free(client);
        return SAVVY_ERR_UNKNOWN;
    }
    if (pthread_cond_init(&client->shutdown_cond, NULL) != 0) {
        pthread_mutex_destroy(&client->state_lock);
        pthread_mutex_destroy(&client->lifecycle_lock);
        free(client);
        return SAVVY_ERR_UNKNOWN;
    }

    client->connected = false;
    client->started = false;
    client->shutdown_requested = false;
    client->stopped = true; /* not-yet-started reads as "already stopped" for idempotency */

    *out_client = client;
    return SAVVY_OK;
}

savvy_status_t sensor_mgr_ipc_client_start(sensor_mgr_ipc_client_t *client) {
    if (client == NULL) {
        return SAVVY_ERR_INVALID_ARGUMENT;
    }

    pthread_mutex_lock(&client->lifecycle_lock);
    if (client->started) {
        pthread_mutex_unlock(&client->lifecycle_lock);
        return SAVVY_OK;
    }

    savvy_status_t st = savvy_ipc_cancel_source_init(&client->cancel_source);
    if (st != SAVVY_OK) {
        pthread_mutex_unlock(&client->lifecycle_lock);
        return st;
    }
    savvy_ipc_reconnect_tracker_init(&client->reconnect_tracker);

    client->shutdown_requested = false;
    client->stopped = false;

    if (pthread_create(&client->worker_thread, NULL, worker_main, client) != 0) {
        savvy_ipc_cancel_source_destroy(&client->cancel_source);
        client->stopped = true;
        pthread_mutex_unlock(&client->lifecycle_lock);
        return SAVVY_ERR_UNKNOWN;
    }

    client->started = true;
    pthread_mutex_unlock(&client->lifecycle_lock);
    return SAVVY_OK;
}

savvy_status_t sensor_mgr_ipc_client_send(sensor_mgr_ipc_client_t *client,
                                          const char *action, const char *payload_json) {
    if (client == NULL || action == NULL) {
        return SAVVY_ERR_INVALID_ARGUMENT;
    }
    if (!savvy_ipc_action_known(action) || savvy_ipc_action_direction(action) != SAVVY_IPC_SENSOR_TO_MGR) {
        return SAVVY_ERR_INVALID_ARGUMENT;
    }

    const char *payload = (payload_json != NULL) ? payload_json : "{}";
    if (savvy_ipc_action_validate_payload(action, payload) != SAVVY_OK) {
        return SAVVY_ERR_PROTOCOL;
    }

    char *envelope_text = NULL;
    size_t envelope_len = 0;
    savvy_status_t st = savvy_ipc_envelope_build(action, payload, &envelope_text, &envelope_len);
    if (st != SAVVY_OK) {
        /* SAVVY_ERR_OVERFLOW here means the built envelope would exceed
         * SAVVY_IPC_MAX_MESSAGE - rejected before the transport is ever
         * touched, exactly like the not-connected case below. */
        return st;
    }

    pthread_mutex_lock(&client->state_lock);
    if (!client->connected) {
        pthread_mutex_unlock(&client->state_lock);
        free(envelope_text);
        return SAVVY_ERR_NOT_CONNECTED;
    }
    savvy_ipc_transport_t transport_copy = client->transport;
    pthread_mutex_unlock(&client->state_lock);

    st = transport_copy.send(&transport_copy, envelope_text, envelope_len, client->config.send_timeout_ms);

    free(envelope_text);
    return st;
}

bool sensor_mgr_ipc_client_is_connected(sensor_mgr_ipc_client_t *client) {
    if (client == NULL) {
        return false;
    }
    pthread_mutex_lock(&client->state_lock);
    bool connected = client->connected;
    pthread_mutex_unlock(&client->state_lock);
    return connected;
}

savvy_status_t sensor_mgr_ipc_client_stop(sensor_mgr_ipc_client_t *client) {
    if (client == NULL) {
        return SAVVY_ERR_INVALID_ARGUMENT;
    }

    pthread_t worker_to_join;

    pthread_mutex_lock(&client->lifecycle_lock);
    if (client->stopped) {
        pthread_mutex_unlock(&client->lifecycle_lock);
        return SAVVY_OK;
    }
    client->stopped = true;
    client->shutdown_requested = true;
    worker_to_join = client->worker_thread;
    pthread_cond_broadcast(&client->shutdown_cond);
    pthread_mutex_unlock(&client->lifecycle_lock);

    /* Steps 3-4: execute cancel (wakes a blocked connector waiter right
     * away; a blocked recv() wakes within one recv_poll_timeout_ms slice,
     * since Foundation's transport recv() has no cancel-source parameter
     * of its own - see mgr_ipc_client.h's stop() doc for the full
     * 7-step rationale). */
    savvy_ipc_cancel_source_cancel(&client->cancel_source);

    /* Step 5: the single worker thread is this design's only
     * waiter/worker - join it fully before touching anything it might
     * still be using. */
    pthread_join(worker_to_join, NULL);

    /* Step 6: defensive idempotent close - the worker thread already
     * closes its own transport on every exit path, so this normally
     * finds connected == false already. */
    pthread_mutex_lock(&client->state_lock);
    if (client->connected) {
        client->transport.close(&client->transport);
        client->connected = false;
    }
    pthread_mutex_unlock(&client->state_lock);

    /* Step 7: only now, after the sole waiter has joined, matching
     * savvy_ipc_cancel_source_destroy()'s documented precondition. */
    savvy_ipc_cancel_source_destroy(&client->cancel_source);

    pthread_mutex_lock(&client->lifecycle_lock);
    client->started = false;
    pthread_mutex_unlock(&client->lifecycle_lock);

    return SAVVY_OK;
}

void sensor_mgr_ipc_client_destroy(sensor_mgr_ipc_client_t *client) {
    if (client == NULL) {
        return;
    }
    sensor_mgr_ipc_client_stop(client);
    pthread_cond_destroy(&client->shutdown_cond);
    pthread_mutex_destroy(&client->lifecycle_lock);
    pthread_mutex_destroy(&client->state_lock);
    free(client);
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
        int rc = pthread_cond_timedwait(&client->shutdown_cond, &client->lifecycle_lock, &deadline);
        if (rc != 0) {
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

static void set_disconnected_and_close(sensor_mgr_ipc_client_t *client) {
    pthread_mutex_lock(&client->state_lock);
    if (client->connected) {
        client->transport.close(&client->transport);
        client->connected = false;
    }
    pthread_mutex_unlock(&client->state_lock);
}

static void mark_reconnect(void *user_data) {
    *(bool *)user_data = true;
}

static void dispatch_envelope(sensor_mgr_ipc_client_t *client, const char *buf, size_t len) {
    savvy_ipc_envelope_t env;
    if (savvy_ipc_envelope_parse(buf, len, &env) != SAVVY_OK) {
        return; /* malformed - reject + drop, never crash (S-003) */
    }

    if (savvy_ipc_action_known(env.action) &&
        savvy_ipc_action_direction(env.action) == SAVVY_IPC_MGR_TO_SENSOR &&
        savvy_ipc_action_validate_payload(env.action, env.payload_json) == SAVVY_OK) {
        if (client->config.callbacks.on_envelope != NULL) {
            client->config.callbacks.on_envelope(env.action, env.payload_json, client->config.callbacks.user_data);
        }
    }
    /* else: unknown action, wrong direction, or invalid payload shape -
     * drop silently, matching S-003 (reject malformed input, never
     * crash), never surfaced past this layer. */

    savvy_ipc_envelope_free(&env);
}

static void *worker_main(void *arg) {
    sensor_mgr_ipc_client_t *client = (sensor_mgr_ipc_client_t *)arg;

    while (!is_shutdown_requested(client)) {
        savvy_ipc_transport_t transport;
        savvy_status_t st = client->config.connector(client->config.connector_ctx,
                                                      client->config.connect_timeout_ms,
                                                      &client->cancel_source,
                                                      &transport);
        if (st == SAVVY_ERR_CANCELLED) {
            break;
        }
        if (st != SAVVY_OK) {
            interruptible_backoff_sleep(client, client->config.reconnect_backoff_ms);
            continue;
        }

        set_connected(client, &transport);

        bool was_reconnect = false;
        savvy_ipc_reconnect_hooks_t hooks = {mark_reconnect, mark_reconnect, &was_reconnect};
        savvy_ipc_reconnect_tracker_on_connected(&client->reconnect_tracker, &hooks);

        /* Sensor always announces itself right after a successful
         * (re)connect - Android's onServiceConnected() does this
         * unconditionally too (MainActivity.java line 2406), with no
         * special-casing for a first connect vs. a later one. */
        sensor_mgr_ipc_client_send(client, SENSOR_MGR_IPC_ACTION_CONNECT, "{}");

        if (client->config.callbacks.on_connected != NULL) {
            client->config.callbacks.on_connected(was_reconnect, client->config.callbacks.user_data);
        }

        while (!is_shutdown_requested(client)) {
            pthread_mutex_lock(&client->state_lock);
            if (!client->connected) {
                pthread_mutex_unlock(&client->state_lock);
                break;
            }
            savvy_ipc_transport_t transport_copy = client->transport;
            pthread_mutex_unlock(&client->state_lock);

            char buf[SAVVY_IPC_MAX_MESSAGE + 1];
            size_t out_len = 0;
            savvy_status_t recv_st = transport_copy.recv(&transport_copy, buf, sizeof(buf), &out_len,
                                                          client->config.recv_poll_timeout_ms);

            if (recv_st == SAVVY_ERR_TIMEOUT) {
                continue; /* bounded poll slice elapsed - loop back and re-check shutdown */
            }
            if (recv_st == SAVVY_ERR_OVERFLOW) {
                continue; /* oversized single record discarded whole (B-006) - connection stays up */
            }
            if (recv_st == SAVVY_OK && out_len == 0) {
                break; /* peer EOF - matches recv()==0 detection */
            }
            if (recv_st != SAVVY_OK) {
                break; /* real transport error - treat as disconnect */
            }

            dispatch_envelope(client, buf, out_len);
        }

        set_disconnected_and_close(client);
        if (client->config.callbacks.on_disconnected != NULL) {
            client->config.callbacks.on_disconnected(client->config.callbacks.user_data);
        }
        /* loop back: reconnect, unless shutdown was requested meanwhile */
    }

    return NULL;
}
