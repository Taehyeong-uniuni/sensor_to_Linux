#ifndef SENSOR_CORE_MGR_IPC_REAL_CONNECTOR_H
#define SENSOR_CORE_MGR_IPC_REAL_CONNECTOR_H

#include "mgr_ipc_client.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Only declared/built when this feature's SENSOR_MGR_IPC_REAL_TRANSPORT
 * CMake option is ON (mirrors Foundation's own SAVVY_IPC_REAL_TRANSPORT -
 * real AF_UNIX SOCK_SEQPACKET is Linux-only in this repo; the host-mac
 * preset never builds it). The production connector any real integration
 * should use - a thin wrapper over Foundation's real
 * savvy_ipc_client_connect_cancelable(). */
typedef struct sensor_mgr_ipc_real_connector_ctx {
    const char *socket_path; /* caller-owned, must outlive the client (B-011: injectable, never hardcoded) */
} sensor_mgr_ipc_real_connector_ctx_t;

savvy_status_t sensor_mgr_ipc_real_connector(void *connector_ctx, uint32_t timeout_ms,
                                              const savvy_ipc_cancel_source_t *cancel,
                                              savvy_ipc_transport_t *out_transport);

#ifdef __cplusplus
}
#endif

#endif
