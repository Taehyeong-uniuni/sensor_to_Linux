#ifndef SAVVY_PLATFORM_IPC_CLIENT_H
#define SAVVY_PLATFORM_IPC_CLIENT_H

#include "savvy/core/error.h"
#include "savvy/platform/ipc_transport.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Sensor-side AF_UNIX SOCK_SEQPACKET client role (08_BLOCKERS.md
 * DEC-20260714-02: MGR=server, Sensor=client). */

/* Connects to the server at `socket_path` (caller-injected - never
 * hardcode a path; see contracts/ for dev vs. production candidate
 * paths). On success, *out_transport is ready to use. Reconnect
 * loop/backoff policy and requesting/expecting a Config/Device resync
 * after a fresh connect are the caller's responsibility (e.g. CC-SENSOR-
 * CORE SNC-03), not this transport's. */
savvy_status_t savvy_ipc_client_connect(const char *socket_path, savvy_ipc_transport_t *out_transport);

#ifdef __cplusplus
}
#endif

#endif
