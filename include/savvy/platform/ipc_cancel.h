#ifndef SAVVY_PLATFORM_IPC_CANCEL_H
#define SAVVY_PLATFORM_IPC_CANCEL_H

#include "savvy/core/error.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Self-pipe cancellation source (Codex V0B-H-02). A bare close() on a
 * connected transport's fd already reliably interrupts another thread
 * blocked in that transport's send()/recv() (see ipc_transport_common.c's
 * fd_transport_close(), which shuts the socket down before closing it -
 * a POSIX-documented, race-free cross-thread wakeup). But a pending
 * savvy_ipc_server_accept_cancelable()/savvy_ipc_client_connect_
 * cancelable() call has no transport/fd handed back to the caller YET to
 * close - there is nothing else to shut down. This self-pipe is the
 * minimal, allowed alternative for exactly that case: one dedicated
 * cancel source per in-flight accept/connect attempt that needs to be
 * externally cancellable from another thread. Not a general-purpose
 * event/notification framework - it has exactly one signal ("cancel"),
 * no payload, no multiplexing. */
typedef struct savvy_ipc_cancel_source {
    int read_fd;
    int write_fd;
} savvy_ipc_cancel_source_t;

/* Creates the underlying pipe (non-blocking, close-on-exec both ends).
 * *cs is left zeroed/unusable on failure. Returns SAVVY_ERR_IO if the
 * underlying pipe()/fcntl() setup fails. */
savvy_status_t savvy_ipc_cancel_source_init(savvy_ipc_cancel_source_t *cs);

/* Requests cancellation of whatever accept_cancelable()/connect_
 * cancelable() call is (or next becomes) blocked on this source - safe to
 * call from any thread, including one different from whichever thread is
 * blocked. Safe to call more than once; calls after the first observe the
 * pipe already has a pending byte and report success without writing
 * another one (FINAL-M-02: EAGAIN/EWOULDBLOCK from the underlying
 * non-blocking write() is treated as "already pending," not a failure).
 * A write() interrupted by a signal (EINTR) is retried (bounded - this
 * does not loop forever) rather than silently dropping the cancel byte,
 * which would otherwise leave a waiter blocked with no way to know
 * cancellation was ever requested. Returns SAVVY_OK once the cancel byte
 * is confirmed pending (written by this call or an earlier one) - a
 * caller that gets SAVVY_OK back is guaranteed a waiter blocked on `cs`
 * will wake promptly. Returns SAVVY_ERR_INVALID_ARGUMENT if `cs` is NULL
 * or already destroyed, or SAVVY_ERR_IO on an unexpected write() failure
 * or after exhausting the bounded EINTR retry budget. Never blocks. */
savvy_status_t savvy_ipc_cancel_source_cancel(const savvy_ipc_cancel_source_t *cs);

/* Closes both pipe fds. Call exactly once, only after no
 * accept_cancelable()/connect_cancelable() call is still using *cs. */
void savvy_ipc_cancel_source_destroy(savvy_ipc_cancel_source_t *cs);

/* Returns true if savvy_ipc_cancel_source_cancel() has already been
 * called on *cs (checked via a non-blocking poll of the underlying pipe -
 * does not consume/drain the pending byte, so this may be checked any
 * number of times without affecting later behavior). `cs` may be NULL
 * (returns false). accept_cancelable()/connect_cancelable() check this
 * once, upfront, before doing any other work - some accept()/connect()
 * attempts complete synchronously (e.g. a healthy AF_UNIX listener with
 * backlog room accepts a nonblocking connect() immediately, without ever
 * returning EINPROGRESS), which would otherwise skip the poll-based
 * cancel check entirely and let an already-cancelled attempt "succeed"
 * anyway. */
bool savvy_ipc_cancel_source_is_cancelled(const savvy_ipc_cancel_source_t *cs);

#ifdef __cplusplus
}
#endif

#endif
