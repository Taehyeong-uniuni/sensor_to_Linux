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
    SAVVY_ERR_UNKNOWN
} savvy_status_t;

/* Returns a static, process-owned string; caller must not free it. */
const char *savvy_status_str(savvy_status_t status);

#ifdef __cplusplus
}
#endif

#endif
