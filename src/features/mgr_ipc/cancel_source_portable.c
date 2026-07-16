/* Portable implementation of include/savvy/platform/ipc_cancel.h's
 * declared contract (self-pipe cancellation - pipe()/fcntl()/poll()/
 * close() only, no socket calls), for use when
 * SENSOR_MGR_IPC_REAL_TRANSPORT is OFF (macOS). Foundation's own
 * implementation of these same functions lives in
 * src/platform/linux/ipc/ipc_transport_common.c, part of the
 * savvy_platform_ipc target - built only alongside
 * src/platform/linux/ipc/ipc_client.c, which uses the Linux-only
 * SOCK_CLOEXEC socket() flag (confirmed: `cc -c ipc_client.c` on this
 * macOS toolchain fails with "use of undeclared identifier
 * 'SOCK_CLOEXEC'" at the socket() call). Since this session keeps the
 * entire src/platform/linux/ipc/ directory tree behind one build-time gate
 * (matching Foundation's own SAVVY_IPC_REAL_TRANSPORT convention and the
 * "linux" in its own path), rather than cherry-picking which of its two
 * files happens to compile on a given macOS toolchain by coincidence,
 * this client still needs a real cancel source on the portable path
 * (used for both the connector wait and, via a fake connector, its own
 * tests) - so just those 4 pipe()-based functions are reimplemented
 * here. Never compiled alongside the real savvy_platform_ipc (see
 * CMakeLists.txt) - that already defines these exact symbols, and this
 * file must never duplicate them into the same link. Behavior mirrors
 * Foundation's own implementation exactly (same EINTR-bounded retry,
 * same EAGAIN-is-already-pending success, same non-blocking+close-on-exec
 * setup, same single-use-per-cancellation-event contract) - this is a
 * portability fork of an already-portable algorithm, not a behavioral
 * reinterpretation. */
#include "savvy/platform/ipc_cancel.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

savvy_status_t savvy_ipc_cancel_source_init(savvy_ipc_cancel_source_t *cs) {
    int fds[2];
    if (pipe(fds) != 0) {
        cs->read_fd = -1;
        cs->write_fd = -1;
        return SAVVY_ERR_IO;
    }
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

#define SAVVY_CANCEL_WRITE_MAX_ATTEMPTS 16

savvy_status_t savvy_ipc_cancel_source_cancel(const savvy_ipc_cancel_source_t *cs) {
    if (cs == NULL || cs->write_fd < 0) {
        return SAVVY_ERR_INVALID_ARGUMENT;
    }
    for (int attempt = 0; attempt < SAVVY_CANCEL_WRITE_MAX_ATTEMPTS; attempt++) {
        ssize_t n = write(cs->write_fd, "x", 1);
        if (n == 1) {
            return SAVVY_OK;
        }
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            return SAVVY_OK;
        }
        return SAVVY_ERR_IO;
    }
    return SAVVY_ERR_IO;
}

void savvy_ipc_cancel_source_destroy(savvy_ipc_cancel_source_t *cs) {
    if (cs->read_fd >= 0) {
        close(cs->read_fd);
        cs->read_fd = -1;
    }
    if (cs->write_fd >= 0) {
        close(cs->write_fd);
        cs->write_fd = -1;
    }
}

bool savvy_ipc_cancel_source_is_cancelled(const savvy_ipc_cancel_source_t *cs) {
    if (cs == NULL || cs->read_fd < 0) {
        return false;
    }
    struct pollfd pfd;
    pfd.fd = cs->read_fd;
    pfd.events = POLLIN;
    pfd.revents = 0;
    return poll(&pfd, 1, 0) > 0 && (pfd.revents & POLLIN) != 0;
}
