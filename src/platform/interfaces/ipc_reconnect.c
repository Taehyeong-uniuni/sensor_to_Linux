#include "savvy/platform/ipc_reconnect.h"
#include <stddef.h>

void savvy_ipc_reconnect_tracker_init(savvy_ipc_reconnect_tracker_t *t)
{
    t->has_connected_before = false;
}

void savvy_ipc_reconnect_tracker_on_connected(savvy_ipc_reconnect_tracker_t *t,
                                              const savvy_ipc_reconnect_hooks_t *hooks)
{
    if (!t->has_connected_before) {
        t->has_connected_before = true;
        return;
    }

    if (hooks == NULL) {
        return;
    }
    if (hooks->request_config != NULL) {
        hooks->request_config(hooks->user_data);
    }
    if (hooks->request_device != NULL) {
        hooks->request_device(hooks->user_data);
    }
}
