#include "savvy/platform/ipc_client.h"
#include "ipc_transport_common.h"
#include <sys/socket.h>
#include <sys/un.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <poll.h>

savvy_status_t savvy_ipc_client_connect(const char *socket_path, uint32_t timeout_ms,
                                         savvy_ipc_transport_t *out_transport)
{
    if (socket_path == NULL || out_transport == NULL) {
        return SAVVY_ERR_INVALID_ARGUMENT;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    if (strlen(socket_path) >= sizeof(addr.sun_path)) {
        return SAVVY_ERR_INVALID_ARGUMENT;
    }
    strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);

    int fd = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        return SAVVY_ERR_IO;
    }

    /* Non-blocking connect + poll-for-completion, so this can never hang
     * forever (e.g. a full accept backlog on the server). */
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        close(fd);
        return SAVVY_ERR_IO;
    }

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        if (errno != EINPROGRESS) {
            close(fd);
            return SAVVY_ERR_NOT_CONNECTED;
        }

        short revents = 0;
        int pr = savvy_ipc_poll_with_deadline(fd, POLLOUT, timeout_ms, &revents);
        if (pr == 0) {
            close(fd);
            return SAVVY_ERR_TIMEOUT;
        }
        if (pr < 0 || (revents & (POLLERR | POLLHUP))) {
            close(fd);
            return SAVVY_ERR_IO;
        }

        int so_error = 0;
        socklen_t so_error_len = sizeof(so_error);
        if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &so_error, &so_error_len) != 0 || so_error != 0) {
            close(fd);
            return SAVVY_ERR_NOT_CONNECTED;
        }
    }

    /* Restore blocking mode: subsequent send/recv apply their own
     * poll-based timeout (savvy_ipc_send_capped/recv_capped), so the fd
     * doesn't need to stay non-blocking, and blocking send()/recvmsg()
     * semantics after a successful poll() are simpler to reason about. */
    if (fcntl(fd, F_SETFL, flags) == -1) {
        close(fd);
        return SAVVY_ERR_IO;
    }

    savvy_ipc_fd_transport_init(out_transport, fd);
    return SAVVY_OK;
}
