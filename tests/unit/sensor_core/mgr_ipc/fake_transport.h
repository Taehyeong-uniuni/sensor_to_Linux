#ifndef SENSOR_CORE_MGR_IPC_FAKE_TRANSPORT_H
#define SENSOR_CORE_MGR_IPC_FAKE_TRANSPORT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "mgr_ipc_client.h"
#include "savvy/core/queue.h"

/* Test-only double for Foundation's real savvy_ipc_client_connect_
 * cancelable(), which lives in savvy_platform_ipc (Linux-only, gated
 * behind SENSOR_MGR_IPC_REAL_TRANSPORT - not available on macOS since
 * AF_UNIX SOCK_SEQPACKET isn't supported on Darwin). This fake uses a
 * real AF_UNIX SOCK_STREAM socketpair() (portable to both macOS and
 * Linux) so mgr_ipc_client.c's full connect/send/recv/reconnect/cancel
 * logic runs against real fds and real blocking syscalls, not an
 * in-memory stub - only the SOCK_SEQPACKET-specific kernel behavior
 * (native message-boundary preservation, the 64KiB MSG_TRUNC path) is
 * left to the Linux Docker verification against tools/mock_mgr and the
 * real connector. Lives entirely under tests/ - never linked into any
 * production target. */

typedef struct fake_connector_ctx {
    savvy_queue_t mgr_fd_queue; /* int items; producer = fake_connector_connect(), consumer = the test's own "fake MGR" driver thread */
    bool fail_all;              /* true => every call simulates "server not reachable yet" (SAVVY_ERR_TIMEOUT), no socketpair created */
} fake_connector_ctx_t;

savvy_status_t fake_connector_init(fake_connector_ctx_t *ctx, size_t queue_capacity);
void fake_connector_destroy(fake_connector_ctx_t *ctx);

/* sensor_mgr_ipc_connector_fn-compatible. */
savvy_status_t fake_connector_connect(void *connector_ctx, uint32_t timeout_ms,
                                       const savvy_ipc_cancel_source_t *cancel,
                                       savvy_ipc_transport_t *out_transport);

/* Blocks until fake_connector_connect() has produced a new mgr-side fd;
 * returns -1 if the queue was closed with nothing left. */
int fake_mgr_dequeue_fd(fake_connector_ctx_t *ctx);

/* Framing for this test double only (4-byte big-endian length prefix +
 * that many bytes of envelope text) - real production code never uses
 * this; SOCK_SEQPACKET preserves message boundaries natively and needs no
 * such prefix. Both fake_mgr_send/recv (this file) and the transport
 * wrapped by fake_connector_connect (fake_transport.c) agree on it. */
bool fake_mgr_send(int mgr_fd, const char *text, size_t len);
/* Returns true and *out_len==0 to signal "peer closed" (never blocks
 * past that); returns false on a genuine I/O error. */
bool fake_mgr_recv(int mgr_fd, char *buf, size_t cap, size_t *out_len, uint32_t timeout_ms);
void fake_mgr_close(int mgr_fd);

#endif
