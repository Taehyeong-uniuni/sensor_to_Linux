#include "real_connector.h"

#include "savvy/platform/ipc_client.h"

savvy_status_t sensor_mgr_ipc_real_connector(void *connector_ctx, uint32_t timeout_ms,
                                              const savvy_ipc_cancel_source_t *cancel,
                                              savvy_ipc_transport_t *out_transport) {
    sensor_mgr_ipc_real_connector_ctx_t *ctx = (sensor_mgr_ipc_real_connector_ctx_t *)connector_ctx;
    if (ctx == NULL || ctx->socket_path == NULL) {
        return SAVVY_ERR_INVALID_ARGUMENT;
    }
    return savvy_ipc_client_connect_cancelable(ctx->socket_path, timeout_ms, cancel, out_transport);
}
