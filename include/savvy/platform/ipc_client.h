#ifndef SAVVY_PLATFORM_IPC_CLIENT_H
#define SAVVY_PLATFORM_IPC_CLIENT_H

#include <stdint.h>
#include "savvy/core/error.h"
#include "savvy/platform/ipc_transport.h"
#include "savvy/platform/ipc_cancel.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Sensor-side AF_UNIX SOCK_SEQPACKET client role (08_BLOCKERS.md
 * DEC-20260714-02: MGR=server, Sensor=client). */

/* Connects to the server at `socket_path` (caller-injected - never
 * hardcode a path; see contracts/ for dev vs. production candidate
 * paths), waiting up to timeout_ms for the connection to complete
 * (SAVVY_ERR_TIMEOUT otherwise - a full accept backlog on the server can
 * otherwise make a plain blocking connect() hang; this never does,
 * regardless of server state). On success, *out_transport is ready to
 * use (also close-on-exec, so it is not inherited if this process later
 * exec()s a helper/updater). Reconnect loop/backoff policy and
 * requesting/expecting a Config/Device resync after a fresh connect are
 * the caller's responsibility (e.g. CC-SENSOR-CORE SNC-03), not this
 * transport's. */
savvy_status_t savvy_ipc_client_connect(const char *socket_path, uint32_t timeout_ms,
                                         savvy_ipc_transport_t *out_transport);

/* Like savvy_ipc_client_connect, but also returns SAVVY_ERR_CANCELLED
 * promptly (V0B-H-02) if `cancel` is signaled via
 * savvy_ipc_cancel_source_cancel() - from any thread, including one
 * different from whichever thread called this function - while still
 * waiting for the connection to complete, or if `cancel` was already
 * signaled before this was even called (checked once, upfront, before
 * anything else - a healthy listener with backlog room can otherwise
 * complete a nonblocking connect() synchronously, without ever returning
 * EINPROGRESS, which would skip the poll-based cancel check entirely and
 * let an already-cancelled attempt "succeed" anyway). `cancel` may be
 * NULL, in which case this behaves identically to savvy_ipc_client_connect (which is in
 * fact implemented as a thin wrapper calling this with cancel=NULL). A
 * caller that wants to be able to abort an in-flight (re)connect attempt
 * from a shutdown/control thread should create one
 * savvy_ipc_cancel_source_t, pass it here, and call
 * savvy_ipc_cancel_source_cancel() on it when stopping - there is no
 * transport/fd yet for a plain close() to interrupt while connect() is
 * still pending, which is exactly why this cancel path exists. */
savvy_status_t savvy_ipc_client_connect_cancelable(const char *socket_path, uint32_t timeout_ms,
                                                    const savvy_ipc_cancel_source_t *cancel,
                                                    savvy_ipc_transport_t *out_transport);

#ifdef __cplusplus
}
#endif

#endif
