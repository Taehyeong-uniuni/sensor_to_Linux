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
 * no payload, no multiplexing.
 *
 * CONCURRENCY CONTRACT (TC-M-01) - read this before using savvy_ipc_
 * cancel_source_destroy() from more than one call site:
 *
 * SUPPORTED, safe to rely on:
 *   - Multiple threads calling savvy_ipc_cancel_source_cancel() on the
 *     same source concurrently with each other.
 *   - savvy_ipc_cancel_source_cancel() running concurrently with the
 *     accept_cancelable()/connect_cancelable() waiter that is blocked on
 *     the same source (this is the whole point of the primitive).
 *   - Calling savvy_ipc_cancel_source_cancel() repeatedly, before or
 *     after a waiter has already observed cancellation.
 *   - Calling savvy_ipc_cancel_source_destroy() a second time
 *     SEQUENTIALLY (not concurrently with anything else) after it has
 *     already fully completed once - this is a safe no-op (both fds are
 *     already -1, so nothing is closed twice).
 *
 * NOT SUPPORTED - a precondition violation, not a race this API detects
 * or defends against:
 *   - Calling savvy_ipc_cancel_source_destroy() while ANY call to
 *     savvy_ipc_cancel_source_cancel() on the same source is still in
 *     flight (in any thread), or while any accept_cancelable()/
 *     connect_cancelable() waiter is still using the source. destroy()
 *     closes both pipe fds; the OS is free to reuse those exact fd
 *     numbers for a completely unrelated object immediately afterward,
 *     and an in-flight cancel() could then write its cancel byte into
 *     that unrelated fd. This Foundation layer deliberately does NOT add
 *     a refcount or generic lifetime/ownership framework to prevent
 *     this - it stays a minimal self-pipe. Instead, the CALLER (a Wave 1
 *     daemon or coordinator) must establish, itself, before calling
 *     destroy():
 *       1. No new savvy_ipc_cancel_source_cancel() calls will be issued
 *          against this source anymore (e.g. the owning object has
 *          already been removed from wherever other threads would find
 *          it to cancel).
 *       2. Every thread that might still be executing a
 *          savvy_ipc_cancel_source_cancel() call against this source has
 *          been joined (or otherwise definitively confirmed stopped).
 *       3. Every accept_cancelable()/connect_cancelable() waiter that was
 *          passed this source has already returned and been joined.
 *     Only once all three hold may destroy() be called. Violating this
 *     ordering is undefined at the OS level (a real fd-reuse race); nothing
 *     in this header attempts to detect or diagnose the violation at
 *     runtime - the supported lifecycle is: waiter starts -> cancel() ->
 *     waiter joins -> every cancel()-calling thread joins -> destroy(). */
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
 * blocked, and concurrently with other cancel() calls on the same source
 * (see the concurrency contract above for what is NOT safe - notably,
 * racing this against savvy_ipc_cancel_source_destroy()).
 *
 * Safe to call more than once. TC-L-01: each call - including the second,
 * third, etc. - attempts a REAL write() of one byte; it does NOT detect
 * "a byte is already pending" and skip the write (a prior revision of
 * this comment incorrectly claimed repeated calls are no-ops that skip
 * the write - they are not, and never have been). In practice this means
 * the pipe's kernel buffer (commonly tens of KB) can accumulate several
 * pending bytes if cancel() is called many times before anything drains
 * it - which, see below, is normally never, for the lifetime of one
 * source. EAGAIN/EWOULDBLOCK (the buffer is currently completely full) is
 * treated as "a notification is already pending," i.e. success, not
 * failure - this function's contract is "at least one byte is/will be
 * pending" (so a waiter is guaranteed to see cs->read_fd become
 * readable), not "exactly one byte total, ever." A write() interrupted
 * by a signal (EINTR) is retried (bounded - this does not loop forever)
 * rather than silently dropping the cancel byte, which would otherwise
 * leave a waiter blocked with no way to know cancellation was ever
 * requested.
 *
 * Nothing in this API ever reads (drains) the pipe -
 * accept_cancelable()/connect_cancelable() and savvy_ipc_cancel_source_
 * is_cancelled() only poll() cs->read_fd for readability, they never
 * consume the byte(s) sitting in it. Consequently, once a source has been
 * cancelled, it must be destroyed rather than reused for a later,
 * unrelated accept/connect attempt: a leftover pending byte would make
 * that later attempt observe "cancelled" immediately, even though nobody
 * asked to cancel it specifically. This API provides no drain/reset
 * function - one savvy_ipc_cancel_source_t is single-use per
 * cancellation event, by design (TC-L-01: not redesigned to add draining,
 * only documented accurately here).
 *
 * Returns SAVVY_OK once the cancel byte is confirmed pending (written by
 * this call or an earlier one) - a caller that gets SAVVY_OK back is
 * guaranteed a waiter blocked on `cs` will wake promptly. Returns
 * SAVVY_ERR_INVALID_ARGUMENT if `cs` is NULL or already destroyed, or
 * SAVVY_ERR_IO on an unexpected write() failure or after exhausting the
 * bounded EINTR retry budget. Never blocks. */
savvy_status_t savvy_ipc_cancel_source_cancel(const savvy_ipc_cancel_source_t *cs);

/* Closes both pipe fds. See the concurrency contract above for the full,
 * required precondition ordering - in short: only after every
 * savvy_ipc_cancel_source_cancel() call against `cs` has returned and
 * every thread that might still call it has been joined, and every
 * accept_cancelable()/connect_cancelable() waiter using `cs` has already
 * returned and been joined. Calling this concurrently with any of those
 * is a precondition violation (a real OS-level fd-reuse race), not a
 * supported/defended-against race. A second, purely SEQUENTIAL call after
 * the first has fully completed is a safe no-op (both fds are already
 * -1) - this is different from calling it concurrently with something
 * still using `cs`, which remains unsupported regardless. */
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
