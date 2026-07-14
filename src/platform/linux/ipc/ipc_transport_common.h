#ifndef SAVVY_IPC_TRANSPORT_COMMON_H
#define SAVVY_IPC_TRANSPORT_COMMON_H

#include <stddef.h>
#include "savvy/core/error.h"
#include "savvy/platform/ipc_transport.h"

/* Shared, role-agnostic AF_UNIX SOCK_SEQPACKET primitives used by both the
 * MGR server role (ipc_server.c) and the Sensor client role
 * (ipc_client.c). Private to src/platform/linux/ipc/ - not a public header. */

/* Rejects with SAVVY_ERR_OVERFLOW (never calls send()) if len exceeds
 * SAVVY_IPC_MAX_MESSAGE - the kernel does not enforce this cap for
 * SOCK_SEQPACKET (confirmed by poc/ipc_seqpacket/result.md), so the
 * application layer must. */
savvy_status_t savvy_ipc_send_capped(int fd, const void *buf, size_t len);

/* Reads one record via recvmsg(). *out_len == 0 with SAVVY_OK means the
 * peer closed the connection (mirrors recv() == 0). SAVVY_ERR_OVERFLOW
 * means MSG_TRUNC was set - the record exceeded `cap` and must be
 * discarded whole, never partially parsed. */
savvy_status_t savvy_ipc_recv_capped(int fd, void *buf, size_t cap, size_t *out_len);

/* Wires `t`'s send/recv/close function pointers to the shared fd-based
 * implementation above, with `fd` as the opaque impl. */
void savvy_ipc_fd_transport_init(savvy_ipc_transport_t *t, int fd);

#endif
