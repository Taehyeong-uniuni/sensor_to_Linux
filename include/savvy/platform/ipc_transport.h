#ifndef SAVVY_PLATFORM_IPC_TRANSPORT_H
#define SAVVY_PLATFORM_IPC_TRANSPORT_H

#include <stddef.h>
#include <stdint.h>
#include "savvy/core/error.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Abstract MGR<->Sensor IPC transport (FND-04 "platform adapters injected
 * through interfaces"). Higher layers (envelope codec, action dispatch)
 * run against this interface so they can be exercised against either the
 * real Linux AF_UNIX SOCK_SEQPACKET transport (FND-03) or a mock/in-memory
 * transport (macOS host tests - see CC-FOUNDATION.md macOS test scope).
 *
 * Every operation is bounded by an explicit timeout_ms so a stalled peer
 * can never hang a caller forever: pass 0 for "check once, don't block",
 * or a bounded wait in milliseconds. There is no "wait forever" option by
 * design - callers needing a very long wait pass a large timeout_ms
 * (internally clamped to a safe maximum, never silently becoming
 * unbounded). */
typedef struct savvy_ipc_transport {
    /* Returns SAVVY_ERR_TIMEOUT if the peer isn't ready to receive within
     * timeout_ms. SAVVY_ERR_CLOSED if this transport has already been
     * close()d. */
    savvy_status_t (*send)(struct savvy_ipc_transport *self, const void *buf, size_t len, uint32_t timeout_ms);
    /* On success, *out_len is the received record length. A closed/EOF
     * peer is reported as SAVVY_OK with *out_len == 0 (mirrors recv()==0).
     * SAVVY_ERR_OVERFLOW means the record exceeded the transport's
     * application-level size cap (enforced independently of `cap`) and
     * was discarded whole, not partially parsed. SAVVY_ERR_TIMEOUT if no
     * record arrives within timeout_ms. SAVVY_ERR_CLOSED if this
     * transport has already been close()d. */
    savvy_status_t (*recv)(struct savvy_ipc_transport *self, void *buf, size_t cap, size_t *out_len, uint32_t timeout_ms);
    /* Idempotent: safe to call more than once on the same transport (a
     * second call is a no-op, not a double-close of a possibly-reused fd
     * number). After close(), send()/recv() return SAVVY_ERR_CLOSED. */
    void (*close)(struct savvy_ipc_transport *self);
    void *impl;
} savvy_ipc_transport_t;

#ifdef __cplusplus
}
#endif

#endif
