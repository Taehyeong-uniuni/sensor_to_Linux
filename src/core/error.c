#include "savvy/core/error.h"

const char *savvy_status_str(savvy_status_t status)
{
    switch (status) {
    case SAVVY_OK: return "OK";
    case SAVVY_ERR_INVALID_ARGUMENT: return "INVALID_ARGUMENT";
    case SAVVY_ERR_OUT_OF_MEMORY: return "OUT_OF_MEMORY";
    case SAVVY_ERR_IO: return "IO";
    case SAVVY_ERR_PROTOCOL: return "PROTOCOL";
    case SAVVY_ERR_TIMEOUT: return "TIMEOUT";
    case SAVVY_ERR_NOT_CONNECTED: return "NOT_CONNECTED";
    case SAVVY_ERR_ALREADY_STARTED: return "ALREADY_STARTED";
    case SAVVY_ERR_NOT_STARTED: return "NOT_STARTED";
    case SAVVY_ERR_CLOSED: return "CLOSED";
    case SAVVY_ERR_OVERFLOW: return "OVERFLOW";
    case SAVVY_ERR_UNKNOWN:
    default: return "UNKNOWN";
    }
}
