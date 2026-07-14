#include "ipc_transport_common.h"
#include "savvy/protocol/ipc_envelope.h" /* SAVVY_IPC_MAX_MESSAGE */
#include <sys/socket.h>
#include <unistd.h>
#include <string.h>
#include <stdint.h>
#include <poll.h>
#include <errno.h>
#include <limits.h>

int savvy_ipc_poll_with_deadline(int fd, short events, uint32_t timeout_ms, short *out_revents)
{
    int clamped = (timeout_ms > (uint32_t)INT_MAX) ? INT_MAX : (int)timeout_ms;

    for (;;) {
        struct pollfd pfd;
        pfd.fd = fd;
        pfd.events = events;
        pfd.revents = 0;
        int pr = poll(&pfd, 1, clamped);
        if (pr < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (pr == 0) {
            return 0;
        }
        if (out_revents != NULL) {
            *out_revents = pfd.revents;
        }
        return 1;
    }
}

savvy_status_t savvy_ipc_send_capped(int fd, const void *buf, size_t len, uint32_t timeout_ms)
{
    if (len > SAVVY_IPC_MAX_MESSAGE) {
        return SAVVY_ERR_OVERFLOW;
    }

    short revents = 0;
    int pr = savvy_ipc_poll_with_deadline(fd, POLLOUT, timeout_ms, &revents);
    if (pr == 0) {
        return SAVVY_ERR_TIMEOUT;
    }
    if (pr < 0) {
        return SAVVY_ERR_IO;
    }
    if (revents & (POLLERR | POLLHUP)) {
        return SAVVY_ERR_IO;
    }

    /* MSG_NOSIGNAL: a peer that already disconnected must surface as
     * SAVVY_ERR_IO (EPIPE) here, not kill the whole process via SIGPIPE. */
    ssize_t n = send(fd, buf, len, MSG_NOSIGNAL);
    if (n < 0) {
        return SAVVY_ERR_IO;
    }
    if ((size_t)n != len) {
        /* SOCK_SEQPACKET delivers a record atomically; a short write here
         * means something is wrong with the socket, not a partial-send
         * case to retry. */
        return SAVVY_ERR_IO;
    }
    return SAVVY_OK;
}

savvy_status_t savvy_ipc_recv_capped(int fd, void *buf, size_t cap, size_t *out_len, uint32_t timeout_ms)
{
    short revents = 0;
    int pr = savvy_ipc_poll_with_deadline(fd, POLLIN, timeout_ms, &revents);
    if (pr == 0) {
        return SAVVY_ERR_TIMEOUT;
    }
    if (pr < 0) {
        return SAVVY_ERR_IO;
    }
    if (revents & POLLERR) {
        return SAVVY_ERR_IO;
    }
    /* POLLHUP is expected on a cleanly-closed peer and must still fall
     * through to recvmsg() (which will return 0), not be treated as an
     * error here. */

    struct msghdr msg;
    struct iovec iov;
    memset(&msg, 0, sizeof(msg));
    iov.iov_base = buf;
    iov.iov_len = cap;
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;

    ssize_t n = recvmsg(fd, &msg, 0);
    if (n < 0) {
        return SAVVY_ERR_IO;
    }
    if (n == 0) {
        *out_len = 0;
        return SAVVY_OK;
    }
    /* Independently enforce the 64 KiB application cap regardless of the
     * caller's buffer size: MSG_TRUNC alone only tells us the record
     * exceeded `cap`, not that it respected SAVVY_IPC_MAX_MESSAGE - a
     * caller with a larger buffer could otherwise let an oversized record
     * through as if it were a normal one. */
    if ((msg.msg_flags & MSG_TRUNC) || (size_t)n > SAVVY_IPC_MAX_MESSAGE) {
        return SAVVY_ERR_OVERFLOW;
    }
    *out_len = (size_t)n;
    return SAVVY_OK;
}

static savvy_status_t fd_transport_send(savvy_ipc_transport_t *self, const void *buf, size_t len, uint32_t timeout_ms)
{
    int fd = (int)(intptr_t)self->impl;
    if (fd < 0) {
        return SAVVY_ERR_CLOSED;
    }
    return savvy_ipc_send_capped(fd, buf, len, timeout_ms);
}

static savvy_status_t fd_transport_recv(savvy_ipc_transport_t *self, void *buf, size_t cap, size_t *out_len, uint32_t timeout_ms)
{
    int fd = (int)(intptr_t)self->impl;
    if (fd < 0) {
        return SAVVY_ERR_CLOSED;
    }
    return savvy_ipc_recv_capped(fd, buf, cap, out_len, timeout_ms);
}

static void fd_transport_close(savvy_ipc_transport_t *self)
{
    int fd = (int)(intptr_t)self->impl;
    if (fd >= 0) {
        close(fd);
    }
    /* Sentinel: a repeated close() becomes a safe no-op instead of a
     * double-close of a possibly-OS-reused fd number, and send()/recv()
     * after close() return SAVVY_ERR_CLOSED instead of operating on a
     * stale fd. */
    self->impl = (void *)(intptr_t)-1;
}

void savvy_ipc_fd_transport_init(savvy_ipc_transport_t *t, int fd)
{
    t->send = fd_transport_send;
    t->recv = fd_transport_recv;
    t->close = fd_transport_close;
    t->impl = (void *)(intptr_t)fd;
}
