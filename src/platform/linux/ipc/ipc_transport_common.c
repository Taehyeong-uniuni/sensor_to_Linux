#include "ipc_transport_common.h"
#include "savvy/protocol/ipc_envelope.h" /* SAVVY_IPC_MAX_MESSAGE */
#include "savvy/core/clock.h"
#include <sys/socket.h>
#include <unistd.h>
#include <string.h>
#include <stdint.h>
#include <poll.h>
#include <errno.h>
#include <limits.h>
#include <fcntl.h>

savvy_status_t savvy_ipc_cancel_source_init(savvy_ipc_cancel_source_t *cs)
{
    int fds[2];
    if (pipe(fds) != 0) {
        cs->read_fd = -1;
        cs->write_fd = -1;
        return SAVVY_ERR_IO;
    }
    /* Non-blocking on both ends: cancel_source_cancel()'s write() must
     * never block (it may run on a thread that cannot afford to), and a
     * spuriously-repeated cancel() only needs "at least one byte
     * buffered", not "every byte accepted." Close-on-exec so a later
     * exec() of a helper/updater doesn't inherit these. */
    for (int i = 0; i < 2; i++) {
        int flags = fcntl(fds[i], F_GETFL, 0);
        if (flags == -1 || fcntl(fds[i], F_SETFL, flags | O_NONBLOCK) == -1 ||
            fcntl(fds[i], F_SETFD, FD_CLOEXEC) == -1) {
            close(fds[0]);
            close(fds[1]);
            cs->read_fd = -1;
            cs->write_fd = -1;
            return SAVVY_ERR_IO;
        }
    }
    cs->read_fd = fds[0];
    cs->write_fd = fds[1];
    return SAVVY_OK;
}

void savvy_ipc_cancel_source_cancel(const savvy_ipc_cancel_source_t *cs)
{
    if (cs == NULL || cs->write_fd < 0) {
        return;
    }
    /* Best-effort, single byte; EAGAIN (pipe already has a pending byte
     * from an earlier cancel() call) and EINTR are both fine to ignore -
     * either way, the read end already has (or will shortly have) at
     * least one byte available, which is all a poll() waiter needs. */
    ssize_t ignored = write(cs->write_fd, "x", 1);
    (void)ignored;
}

void savvy_ipc_cancel_source_destroy(savvy_ipc_cancel_source_t *cs)
{
    if (cs->read_fd >= 0) {
        close(cs->read_fd);
        cs->read_fd = -1;
    }
    if (cs->write_fd >= 0) {
        close(cs->write_fd);
        cs->write_fd = -1;
    }
}

bool savvy_ipc_cancel_source_is_cancelled(const savvy_ipc_cancel_source_t *cs)
{
    if (cs == NULL || cs->read_fd < 0) {
        return false;
    }
    struct pollfd pfd;
    pfd.fd = cs->read_fd;
    pfd.events = POLLIN;
    pfd.revents = 0;
    return poll(&pfd, 1, 0) > 0 && (pfd.revents & POLLIN) != 0;
}

int savvy_ipc_poll_with_deadline(int fd, short events, uint32_t timeout_ms, short *out_revents)
{
    return savvy_ipc_poll_with_deadline_cancelable(fd, events, timeout_ms, out_revents, NULL);
}

int savvy_ipc_poll_with_deadline_cancelable(int fd, short events, uint32_t timeout_ms,
                                             short *out_revents,
                                             const savvy_ipc_cancel_source_t *cancel)
{
    savvy_deadline_t deadline;
    savvy_deadline_arm(&deadline, timeout_ms);

    for (;;) {
        if (savvy_deadline_expired(&deadline)) {
            return 0;
        }
        uint32_t remaining = savvy_deadline_remaining_ms(&deadline);
        int clamped = (remaining > (uint32_t)INT_MAX) ? INT_MAX : (int)remaining;

        struct pollfd pfds[2];
        pfds[0].fd = fd;
        pfds[0].events = events;
        pfds[0].revents = 0;
        nfds_t nfds = 1;
        if (cancel != NULL && cancel->read_fd >= 0) {
            pfds[1].fd = cancel->read_fd;
            pfds[1].events = POLLIN;
            pfds[1].revents = 0;
            nfds = 2;
        }

        int pr = poll(pfds, nfds, clamped);
        if (pr < 0) {
            if (errno == EINTR) {
                /* Recompute the remaining time from the ORIGINAL absolute
                 * deadline on the next loop iteration - never restart a
                 * fresh `timeout_ms`-length wait (V0B-H-02). */
                continue;
            }
            return -1;
        }
        if (pr == 0) {
            return 0;
        }
        if (nfds == 2 && (pfds[1].revents & POLLIN)) {
            /* Cancellation takes priority even if `fd` also became ready
             * in the same poll() call. */
            return 2;
        }
        if (out_revents != NULL) {
            *out_revents = pfds[0].revents;
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
        /* shutdown() BEFORE close() (V0B-H-02): a bare close() gives no
         * guarantee that another thread currently blocked in poll()/
         * send()/recvmsg() on this exact fd wakes up promptly - close()'s
         * effect on a concurrent blocked call in a DIFFERENT thread is
         * unspecified, and the fd number can even be reused by an
         * unrelated new socket before the blocked thread notices,
         * racing it onto the wrong resource. shutdown(SHUT_RDWR) instead
         * marks the underlying socket itself as done, which the kernel
         * applies immediately to every thread with that socket open
         * (poll() wakes with POLLHUP/POLLERR, a blocked recvmsg() sees
         * EOF, a blocked send() fails) - a POSIX-documented, race-free
         * cross-thread wakeup that this codebase's fd_transport_send/recv
         * already handle correctly via their existing POLLERR/POLLHUP
         * checks. The actual close() still happens right after, on this
         * same call - shutdown() alone does not release the fd. */
        shutdown(fd, SHUT_RDWR);
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
