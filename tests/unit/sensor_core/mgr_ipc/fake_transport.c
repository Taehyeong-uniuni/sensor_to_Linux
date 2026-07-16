/* clock_gettime()/CLOCK_MONOTONIC are POSIX.1-2008; glibc hides them under
 * -std=c11 without this (see src/core/clock.c for Foundation's own
 * instance of the same fix). Must be defined before any system header. */
#define _POSIX_C_SOURCE 200809L

#include "fake_transport.h"

#include <errno.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

static int64_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static bool poll_readable(int fd, uint32_t timeout_ms) {
    struct pollfd pfd;
    pfd.fd = fd;
    pfd.events = POLLIN;
    pfd.revents = 0;
    int rc = poll(&pfd, 1, (int)timeout_ms);
    return rc > 0 && (pfd.revents & (POLLIN | POLLHUP)) != 0;
}

/* Reads exactly `n` bytes or returns false on EOF/error/deadline; on a
 * clean EOF with zero bytes read so far, *out_eof is set true. */
static bool read_exact(int fd, void *buf, size_t n, int64_t deadline_ms, bool *out_eof) {
    size_t got = 0;
    unsigned char *p = (unsigned char *)buf;
    *out_eof = false;
    while (got < n) {
        int64_t remaining = deadline_ms - now_ms();
        if (remaining <= 0) {
            return false;
        }
        if (!poll_readable(fd, (uint32_t)remaining)) {
            return false;
        }
        ssize_t r = read(fd, p + got, n - got);
        if (r == 0) {
            if (got == 0) {
                *out_eof = true;
            }
            return false;
        }
        if (r < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        got += (size_t)r;
    }
    return true;
}

static bool write_all(int fd, const void *buf, size_t n) {
    size_t sent = 0;
    const unsigned char *p = (const unsigned char *)buf;
    while (sent < n) {
        ssize_t w = write(fd, p + sent, n - sent);
        if (w < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        sent += (size_t)w;
    }
    return true;
}

typedef struct fake_transport_impl {
    int fd;
    fake_connector_ctx_t *ctx;
} fake_transport_impl_t;

static void worker_tracking_destructor(void *value) {
    fake_connector_ctx_t *ctx = (fake_connector_ctx_t *)value;
    pthread_mutex_lock(&ctx->lock);
    ctx->worker_exited_count += 1;
    ctx->worker_active_count -= 1;
    pthread_cond_broadcast(&ctx->send_cond);
    pthread_mutex_unlock(&ctx->lock);
}

static savvy_status_t fd_transport_send(savvy_ipc_transport_t *self, const void *buf, size_t len, uint32_t timeout_ms) {
    (void)timeout_ms;
    fake_transport_impl_t *impl = (fake_transport_impl_t *)self->impl;
    int fd = impl->fd;
    pthread_mutex_lock(&impl->ctx->lock);
    impl->ctx->send_count += 1;
    if (impl->ctx->block_next_send) {
        impl->ctx->block_next_send = false;
        impl->ctx->send_is_blocked = true;
        pthread_cond_broadcast(&impl->ctx->send_cond);
        while (!impl->ctx->release_blocked_send) {
            pthread_cond_wait(&impl->ctx->send_cond, &impl->ctx->lock);
        }
        impl->ctx->send_is_blocked = false;
        impl->ctx->release_blocked_send = false;
    }
    if (impl->ctx->send_failures_remaining > 0) {
        impl->ctx->send_failures_remaining -= 1;
        savvy_status_t status = impl->ctx->send_failure_status;
        pthread_mutex_unlock(&impl->ctx->lock);
        return status;
    }
    pthread_mutex_unlock(&impl->ctx->lock);
    uint32_t be_len = (uint32_t)len;
    unsigned char prefix[4];
    prefix[0] = (unsigned char)((be_len >> 24) & 0xFF);
    prefix[1] = (unsigned char)((be_len >> 16) & 0xFF);
    prefix[2] = (unsigned char)((be_len >> 8) & 0xFF);
    prefix[3] = (unsigned char)(be_len & 0xFF);

    if (!write_all(fd, prefix, sizeof(prefix))) {
        return SAVVY_ERR_CLOSED;
    }
    if (len > 0 && !write_all(fd, buf, len)) {
        return SAVVY_ERR_CLOSED;
    }
    return SAVVY_OK;
}

static savvy_status_t fd_transport_recv(savvy_ipc_transport_t *self, void *buf, size_t cap, size_t *out_len,
                                        uint32_t timeout_ms) {
    fake_transport_impl_t *impl = (fake_transport_impl_t *)self->impl;
    int fd = impl->fd;
    int64_t deadline = now_ms() + (int64_t)timeout_ms;

    unsigned char prefix[4];
    bool eof = false;
    if (!read_exact(fd, prefix, sizeof(prefix), deadline, &eof)) {
        if (eof) {
            *out_len = 0;
            return SAVVY_OK; /* peer closed - mirrors recv()==0 */
        }
        return SAVVY_ERR_TIMEOUT;
    }

    uint32_t len = ((uint32_t)prefix[0] << 24) | ((uint32_t)prefix[1] << 16) |
                   ((uint32_t)prefix[2] << 8) | (uint32_t)prefix[3];
    if (len > cap) {
        /* Drain and discard the oversized body so the stream stays framed
         * for the next message, mirroring "discarded whole" (B-006). */
        unsigned char discard[256];
        uint32_t remaining = len;
        while (remaining > 0) {
            size_t chunk = remaining < sizeof(discard) ? remaining : sizeof(discard);
            if (!read_exact(fd, discard, chunk, deadline, &eof)) {
                if (eof) {
                    *out_len = 0;
                    return SAVVY_OK;
                }
                return SAVVY_ERR_TIMEOUT;
            }
            remaining -= (uint32_t)chunk;
        }
        return SAVVY_ERR_OVERFLOW;
    }

    if (len > 0 && !read_exact(fd, buf, len, deadline, &eof)) {
        if (eof) {
            *out_len = 0;
            return SAVVY_OK;
        }
        return SAVVY_ERR_TIMEOUT;
    }
    *out_len = len;
    return SAVVY_OK;
}

static void fd_transport_close(savvy_ipc_transport_t *self) {
    fake_transport_impl_t *impl = (fake_transport_impl_t *)self->impl;
    int fd = impl->fd;
    if (fd >= 0) {
        shutdown(fd, SHUT_RDWR);
        close(fd);
    }
    pthread_mutex_lock(&impl->ctx->lock);
    impl->ctx->close_count += 1;
    pthread_cond_broadcast(&impl->ctx->send_cond);
    pthread_mutex_unlock(&impl->ctx->lock);
    free(impl);
    self->impl = NULL;
}

savvy_status_t fake_connector_init(fake_connector_ctx_t *ctx, size_t queue_capacity) {
    ctx->fail_all = false;
    ctx->worker_key = (pthread_key_t)0;
    ctx->worker_tracking_enabled = false;
    ctx->worker_key_initialized = false;
    ctx->worker_started_count = 0;
    ctx->worker_exited_count = 0;
    ctx->worker_active_count = 0;
    ctx->connect_count = 0;
    ctx->send_failures_remaining = 0;
    ctx->send_failure_status = SAVVY_ERR_IO;
    ctx->close_count = 0;
    ctx->send_count = 0;
    ctx->block_next_send = false;
    ctx->send_is_blocked = false;
    ctx->release_blocked_send = false;
    if (pthread_mutex_init(&ctx->lock, NULL) != 0) {
        return SAVVY_ERR_UNKNOWN;
    }
    if (pthread_cond_init(&ctx->send_cond, NULL) != 0) {
        pthread_mutex_destroy(&ctx->lock);
        return SAVVY_ERR_UNKNOWN;
    }
    savvy_status_t st = savvy_queue_init(&ctx->mgr_fd_queue, queue_capacity, sizeof(int), NULL);
    if (st != SAVVY_OK) {
        pthread_cond_destroy(&ctx->send_cond);
        pthread_mutex_destroy(&ctx->lock);
    }
    return st;
}

void fake_connector_destroy(fake_connector_ctx_t *ctx) {
    savvy_queue_close(&ctx->mgr_fd_queue);
    savvy_queue_destroy(&ctx->mgr_fd_queue);
    if (ctx->worker_key_initialized) {
        pthread_key_delete(ctx->worker_key);
    }
    pthread_cond_destroy(&ctx->send_cond);
    pthread_mutex_destroy(&ctx->lock);
}

void fake_connector_fail_next_sends(fake_connector_ctx_t *ctx, size_t count, savvy_status_t status) {
    pthread_mutex_lock(&ctx->lock);
    ctx->send_failures_remaining = count;
    ctx->send_failure_status = status;
    pthread_mutex_unlock(&ctx->lock);
}

int fake_connector_close_count(fake_connector_ctx_t *ctx) {
    pthread_mutex_lock(&ctx->lock);
    int count = ctx->close_count;
    pthread_mutex_unlock(&ctx->lock);
    return count;
}

int fake_connector_send_count(fake_connector_ctx_t *ctx) {
    pthread_mutex_lock(&ctx->lock);
    int count = ctx->send_count;
    pthread_mutex_unlock(&ctx->lock);
    return count;
}

void fake_connector_block_next_send(fake_connector_ctx_t *ctx) {
    pthread_mutex_lock(&ctx->lock);
    ctx->block_next_send = true;
    ctx->send_is_blocked = false;
    ctx->release_blocked_send = false;
    pthread_mutex_unlock(&ctx->lock);
}

bool fake_connector_wait_send_blocked(fake_connector_ctx_t *ctx, uint32_t timeout_ms) {
    struct timespec deadline;
    clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_sec += (time_t)(timeout_ms / 1000u);
    deadline.tv_nsec += (long)(timeout_ms % 1000u) * 1000000L;
    if (deadline.tv_nsec >= 1000000000L) {
        deadline.tv_sec += 1;
        deadline.tv_nsec -= 1000000000L;
    }

    pthread_mutex_lock(&ctx->lock);
    while (!ctx->send_is_blocked) {
        if (pthread_cond_timedwait(&ctx->send_cond, &ctx->lock, &deadline) != 0) {
            pthread_mutex_unlock(&ctx->lock);
            return false;
        }
    }
    pthread_mutex_unlock(&ctx->lock);
    return true;
}

void fake_connector_release_blocked_send(fake_connector_ctx_t *ctx) {
    pthread_mutex_lock(&ctx->lock);
    ctx->release_blocked_send = true;
    pthread_cond_broadcast(&ctx->send_cond);
    pthread_mutex_unlock(&ctx->lock);
}

bool fake_connector_wait_close_count(fake_connector_ctx_t *ctx, int expected,
                                     uint32_t timeout_ms) {
    struct timespec deadline;
    clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_sec += (time_t)(timeout_ms / 1000u);
    deadline.tv_nsec += (long)(timeout_ms % 1000u) * 1000000L;
    if (deadline.tv_nsec >= 1000000000L) {
        deadline.tv_sec += 1;
        deadline.tv_nsec -= 1000000000L;
    }

    pthread_mutex_lock(&ctx->lock);
    while (ctx->close_count < expected) {
        if (pthread_cond_timedwait(&ctx->send_cond, &ctx->lock, &deadline) != 0) {
            pthread_mutex_unlock(&ctx->lock);
            return false;
        }
    }
    pthread_mutex_unlock(&ctx->lock);
    return true;
}

savvy_status_t fake_connector_enable_worker_tracking(fake_connector_ctx_t *ctx) {
    pthread_mutex_lock(&ctx->lock);
    if (!ctx->worker_key_initialized) {
        if (pthread_key_create(&ctx->worker_key, worker_tracking_destructor) != 0) {
            pthread_mutex_unlock(&ctx->lock);
            return SAVVY_ERR_UNKNOWN;
        }
        ctx->worker_key_initialized = true;
    }
    ctx->worker_tracking_enabled = true;
    pthread_mutex_unlock(&ctx->lock);
    return SAVVY_OK;
}

static bool wait_worker_count(fake_connector_ctx_t *ctx, int expected,
                              bool wait_for_exit, uint32_t timeout_ms) {
    struct timespec deadline;
    clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_sec += (time_t)(timeout_ms / 1000u);
    deadline.tv_nsec += (long)(timeout_ms % 1000u) * 1000000L;
    if (deadline.tv_nsec >= 1000000000L) {
        deadline.tv_sec += 1;
        deadline.tv_nsec -= 1000000000L;
    }

    pthread_mutex_lock(&ctx->lock);
    int *count = wait_for_exit ? &ctx->worker_exited_count : &ctx->worker_started_count;
    while (*count < expected) {
        if (pthread_cond_timedwait(&ctx->send_cond, &ctx->lock, &deadline) != 0) {
            pthread_mutex_unlock(&ctx->lock);
            return false;
        }
    }
    pthread_mutex_unlock(&ctx->lock);
    return true;
}

bool fake_connector_wait_worker_started(fake_connector_ctx_t *ctx, int expected,
                                        uint32_t timeout_ms) {
    return wait_worker_count(ctx, expected, false, timeout_ms);
}

bool fake_connector_wait_worker_exited(fake_connector_ctx_t *ctx, int expected,
                                       uint32_t timeout_ms) {
    return wait_worker_count(ctx, expected, true, timeout_ms);
}

int fake_connector_worker_started_count(fake_connector_ctx_t *ctx) {
    pthread_mutex_lock(&ctx->lock);
    int count = ctx->worker_started_count;
    pthread_mutex_unlock(&ctx->lock);
    return count;
}

int fake_connector_worker_exited_count(fake_connector_ctx_t *ctx) {
    pthread_mutex_lock(&ctx->lock);
    int count = ctx->worker_exited_count;
    pthread_mutex_unlock(&ctx->lock);
    return count;
}

int fake_connector_worker_active_count(fake_connector_ctx_t *ctx) {
    pthread_mutex_lock(&ctx->lock);
    int count = ctx->worker_active_count;
    pthread_mutex_unlock(&ctx->lock);
    return count;
}

int fake_connector_connect_count(fake_connector_ctx_t *ctx) {
    pthread_mutex_lock(&ctx->lock);
    int count = ctx->connect_count;
    pthread_mutex_unlock(&ctx->lock);
    return count;
}

savvy_status_t fake_connector_connect(void *connector_ctx, uint32_t timeout_ms,
                                       const savvy_ipc_cancel_source_t *cancel,
                                       savvy_ipc_transport_t *out_transport) {
    fake_connector_ctx_t *ctx = (fake_connector_ctx_t *)connector_ctx;

    pthread_mutex_lock(&ctx->lock);
    ctx->connect_count += 1;
    bool track_worker = ctx->worker_tracking_enabled;
    pthread_key_t worker_key = ctx->worker_key;
    pthread_mutex_unlock(&ctx->lock);

    if (track_worker && pthread_getspecific(worker_key) == NULL) {
        if (pthread_setspecific(worker_key, ctx) != 0) {
            return SAVVY_ERR_UNKNOWN;
        }
        pthread_mutex_lock(&ctx->lock);
        ctx->worker_started_count += 1;
        ctx->worker_active_count += 1;
        pthread_cond_broadcast(&ctx->send_cond);
        pthread_mutex_unlock(&ctx->lock);
    }

    if (cancel != NULL && savvy_ipc_cancel_source_is_cancelled(cancel)) {
        return SAVVY_ERR_CANCELLED;
    }
    if (ctx->fail_all) {
        /* Simulate "server not reachable yet" by waiting out the
         * connect timeout while still honoring cancellation, exactly
         * like a real connect attempt against a down/unreachable
         * server would. */
        struct pollfd pfd;
        pfd.fd = cancel != NULL ? cancel->read_fd : -1;
        pfd.events = POLLIN;
        pfd.revents = 0;
        if (pfd.fd >= 0) {
            poll(&pfd, 1, (int)timeout_ms);
            if (savvy_ipc_cancel_source_is_cancelled(cancel)) {
                return SAVVY_ERR_CANCELLED;
            }
        }
        return SAVVY_ERR_TIMEOUT;
    }

    int fds[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0) {
        return SAVVY_ERR_IO;
    }

    fake_transport_impl_t *impl = malloc(sizeof(*impl));
    if (impl == NULL) {
        shutdown(fds[0], SHUT_RDWR);
        close(fds[0]);
        shutdown(fds[1], SHUT_RDWR);
        close(fds[1]);
        return SAVVY_ERR_OUT_OF_MEMORY;
    }
    impl->fd = fds[0];
    impl->ctx = ctx;
    out_transport->send = fd_transport_send;
    out_transport->recv = fd_transport_recv;
    out_transport->close = fd_transport_close;
    out_transport->impl = impl;

    int mgr_fd = fds[1];
    savvy_status_t st = savvy_queue_push(&ctx->mgr_fd_queue, &mgr_fd);
    if (st != SAVVY_OK) {
        shutdown(fds[0], SHUT_RDWR);
        close(fds[0]);
        shutdown(fds[1], SHUT_RDWR);
        close(fds[1]);
        free(impl);
        return SAVVY_ERR_IO;
    }
    return SAVVY_OK;
}

int fake_mgr_dequeue_fd(fake_connector_ctx_t *ctx) {
    int fd = -1;
    savvy_status_t st = savvy_queue_pop(&ctx->mgr_fd_queue, &fd);
    if (st != SAVVY_OK) {
        return -1;
    }
    return fd;
}

bool fake_mgr_send(int mgr_fd, const char *text, size_t len) {
    uint32_t be_len = (uint32_t)len;
    unsigned char prefix[4];
    prefix[0] = (unsigned char)((be_len >> 24) & 0xFF);
    prefix[1] = (unsigned char)((be_len >> 16) & 0xFF);
    prefix[2] = (unsigned char)((be_len >> 8) & 0xFF);
    prefix[3] = (unsigned char)(be_len & 0xFF);
    if (!write_all(mgr_fd, prefix, sizeof(prefix))) {
        return false;
    }
    return len == 0 || write_all(mgr_fd, text, len);
}

bool fake_mgr_recv(int mgr_fd, char *buf, size_t cap, size_t *out_len, uint32_t timeout_ms) {
    int64_t deadline = now_ms() + (int64_t)timeout_ms;
    unsigned char prefix[4];
    bool eof = false;
    if (!read_exact(mgr_fd, prefix, sizeof(prefix), deadline, &eof)) {
        *out_len = 0;
        return eof;
    }
    uint32_t len = ((uint32_t)prefix[0] << 24) | ((uint32_t)prefix[1] << 16) |
                   ((uint32_t)prefix[2] << 8) | (uint32_t)prefix[3];
    if (len > cap || !read_exact(mgr_fd, buf, len, deadline, &eof)) {
        *out_len = 0;
        return false;
    }
    *out_len = len;
    return true;
}

void fake_mgr_close(int mgr_fd) {
    if (mgr_fd >= 0) {
        shutdown(mgr_fd, SHUT_RDWR);
        close(mgr_fd);
    }
}
