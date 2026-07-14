#include "savvy/platform/ipc_client.h"
#include "ipc_transport_common.h"
#include <sys/socket.h>
#include <sys/un.h>
#include <string.h>
#include <unistd.h>

savvy_status_t savvy_ipc_client_connect(const char *socket_path, savvy_ipc_transport_t *out_transport)
{
    if (socket_path == NULL || out_transport == NULL) {
        return SAVVY_ERR_INVALID_ARGUMENT;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    if (strlen(socket_path) >= sizeof(addr.sun_path)) {
        return SAVVY_ERR_INVALID_ARGUMENT;
    }
    strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);

    int fd = socket(AF_UNIX, SOCK_SEQPACKET, 0);
    if (fd < 0) {
        return SAVVY_ERR_IO;
    }

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        close(fd);
        return SAVVY_ERR_NOT_CONNECTED;
    }

    savvy_ipc_fd_transport_init(out_transport, fd);
    return SAVVY_OK;
}
