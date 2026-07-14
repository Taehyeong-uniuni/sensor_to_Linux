#ifndef SAVVY_IPC_TRANSPORT_COMMON_H
#define SAVVY_IPC_TRANSPORT_COMMON_H

#include <stddef.h>
#include <stdint.h>
#include "savvy/core/error.h"
#include "savvy/platform/ipc_transport.h"

/* Shared, role-agnostic AF_UNIX SOCK_SEQPACKET primitives used by both the
 * MGR server role (ipc_server.c) and the Sensor client role
 * (ipc_client.c). Private to src/platform/linux/ipc/ - not a public header. */

/* Polls `fd` for `events` (POLLIN/POLLOUT) within `timeout_ms`, clamping
 * a huge timeout to INT_MAX (safe/bounded) instead of letting the cast to
 * poll()'s int parameter wrap into a negative value (which POSIX treats
 * as "wait forever" - the opposite of what a huge-but-still-bounded
 * caller value should mean). Returns 1 if `fd` became ready (with
 * *out_revents set, if non-NULL, so the caller can distinguish
 * POLLERR/POLLHUP from a plain readiness signal), 0 on genuine timeout,
 * -1 on poll() failure (errno set). EINTR is retried with the same
 * (already-clamped) timeout rather than precisely recomputing a shrinking
 * deadline - simpler, and still never blocks unboundedly; an EINTR-heavy
 * environment may see a slightly longer total wait than exactly
 * timeout_ms, but never an infinite one. */
int savvy_ipc_poll_with_deadline(int fd, short events, uint32_t timeout_ms, short *out_revents);

/* Rejects with SAVVY_ERR_OVERFLOW (never calls send()) if len exceeds
 * SAVVY_IPC_MAX_MESSAGE - the kernel does not enforce this cap for
 * SOCK_SEQPACKET (confirmed by poc/ipc_seqpacket/result.md), so the
 * application layer must. Returns SAVVY_ERR_TIMEOUT if the socket isn't
 * writable within timeout_ms. Uses MSG_NOSIGNAL so a peer that has
 * already disconnected yields SAVVY_ERR_IO (EPIPE) instead of raising
 * SIGPIPE and killing the whole process. */
savvy_status_t savvy_ipc_send_capped(int fd, const void *buf, size_t len, uint32_t timeout_ms);

/* Reads one record via recvmsg() once `fd` is readable (waits up to
 * timeout_ms, returning SAVVY_ERR_TIMEOUT otherwise). *out_len == 0 with
 * SAVVY_OK means the peer closed the connection (mirrors recv() == 0).
 * SAVVY_ERR_OVERFLOW means either MSG_TRUNC was set (the record exceeded
 * `cap`) or the record exceeded SAVVY_IPC_MAX_MESSAGE outright (checked
 * independently of `cap`, so a caller-provided buffer larger than 64 KiB
 * cannot let an oversized record through) - either way the record must be
 * discarded, not partially parsed. */
savvy_status_t savvy_ipc_recv_capped(int fd, void *buf, size_t cap, size_t *out_len, uint32_t timeout_ms);

/* Wires `t`'s send/recv/close function pointers to the shared fd-based
 * implementation above, with `fd` as the opaque impl. */
void savvy_ipc_fd_transport_init(savvy_ipc_transport_t *t, int fd);

#endif
