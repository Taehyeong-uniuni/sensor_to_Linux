#ifndef SAVVY_CORE_ERROR_H
#define SAVVY_CORE_ERROR_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum savvy_status {
    SAVVY_OK = 0,
    SAVVY_ERR_INVALID_ARGUMENT,
    SAVVY_ERR_OUT_OF_MEMORY,
    SAVVY_ERR_IO,
    SAVVY_ERR_PROTOCOL,
    SAVVY_ERR_TIMEOUT,
    SAVVY_ERR_NOT_CONNECTED,
    SAVVY_ERR_ALREADY_STARTED,
    SAVVY_ERR_NOT_STARTED,
    SAVVY_ERR_CLOSED,
    SAVVY_ERR_OVERFLOW,
    /* A caller-requested cancellation interrupted a blocked wait (V0B-H-02,
     * e.g. savvy_ipc_server_accept_cancelable/savvy_ipc_client_connect_
     * cancelable's `cancel` source was signaled) - distinct from
     * SAVVY_ERR_TIMEOUT (deadline elapsed) and SAVVY_ERR_IO (a real
     * transport/socket failure). */
    SAVVY_ERR_CANCELLED,
    SAVVY_ERR_UNKNOWN
} savvy_status_t;

/* Returns a static, process-owned string; caller must not free it. */
const char *savvy_status_str(savvy_status_t status);

#ifdef __cplusplus
}
#endif

#endif
