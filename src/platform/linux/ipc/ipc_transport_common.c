#include "ipc_transport_common.h"
#include "savvy/protocol/ipc_envelope.h" /* SAVVY_IPC_MAX_MESSAGE */
#include <sys/socket.h>
#include <unistd.h>
#include <string.h>
#include <stdint.h>

savvy_status_t savvy_ipc_send_capped(int fd, const void *buf, size_t len)
{
    if (len > SAVVY_IPC_MAX_MESSAGE) {
        return SAVVY_ERR_OVERFLOW;
    }
    ssize_t n = send(fd, buf, len, 0);
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

savvy_status_t savvy_ipc_recv_capped(int fd, void *buf, size_t cap, size_t *out_len)
{
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
    if (msg.msg_flags & MSG_TRUNC) {
        return SAVVY_ERR_OVERFLOW;
    }
    *out_len = (size_t)n;
    return SAVVY_OK;
}

static savvy_status_t fd_transport_send(savvy_ipc_transport_t *self, const void *buf, size_t len)
{
    return savvy_ipc_send_capped((int)(intptr_t)self->impl, buf, len);
}

static savvy_status_t fd_transport_recv(savvy_ipc_transport_t *self, void *buf, size_t cap, size_t *out_len)
{
    return savvy_ipc_recv_capped((int)(intptr_t)self->impl, buf, cap, out_len);
}

static void fd_transport_close(savvy_ipc_transport_t *self)
{
    int fd = (int)(intptr_t)self->impl;
    if (fd >= 0) {
        close(fd);
    }
}

void savvy_ipc_fd_transport_init(savvy_ipc_transport_t *t, int fd)
{
    t->send = fd_transport_send;
    t->recv = fd_transport_recv;
    t->close = fd_transport_close;
    t->impl = (void *)(intptr_t)fd;
}
