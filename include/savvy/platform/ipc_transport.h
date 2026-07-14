#ifndef SAVVY_PLATFORM_IPC_TRANSPORT_H
#define SAVVY_PLATFORM_IPC_TRANSPORT_H

#include <stddef.h>
#include "savvy/core/error.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Abstract MGR<->Sensor IPC transport (FND-04 "platform adapters injected
 * through interfaces"). Higher layers (envelope codec, action dispatch)
 * run against this interface so they can be exercised against either the
 * real Linux AF_UNIX SOCK_SEQPACKET transport (FND-03) or a mock/in-memory
 * transport (macOS host tests - see CC-FOUNDATION.md macOS test scope). */
typedef struct savvy_ipc_transport {
    savvy_status_t (*send)(struct savvy_ipc_transport *self, const void *buf, size_t len);
    /* On success, *out_len is the received record length. A closed/EOF
     * peer is reported as SAVVY_OK with *out_len == 0 (mirrors recv()==0).
     * SAVVY_ERR_OVERFLOW means the record was truncated (MSG_TRUNC)
     * because it exceeded `cap`; the record must be discarded, not parsed. */
    savvy_status_t (*recv)(struct savvy_ipc_transport *self, void *buf, size_t cap, size_t *out_len);
    void (*close)(struct savvy_ipc_transport *self);
    void *impl;
} savvy_ipc_transport_t;

#ifdef __cplusplus
}
#endif

#endif
