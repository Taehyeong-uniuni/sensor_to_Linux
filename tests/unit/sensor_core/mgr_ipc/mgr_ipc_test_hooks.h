#ifndef SENSOR_CORE_MGR_IPC_TEST_HOOKS_H
#define SENSOR_CORE_MGR_IPC_TEST_HOOKS_H

#include "mgr_ipc_client.h"

/* This header is consumed only by the testable mgr_ipc target. The
 * production sensor_core_mgr_ipc archive is compiled without
 * SENSOR_MGR_IPC_TESTING and exposes none of these symbols. */
typedef enum sensor_mgr_ipc_test_event {
    SENSOR_MGR_IPC_TEST_START_AFTER_DESTROY_CHECK = 0,
    SENSOR_MGR_IPC_TEST_START_BEFORE_CANCEL_INIT,
    SENSOR_MGR_IPC_TEST_START_AFTER_WORKER_CREATE,
    SENSOR_MGR_IPC_TEST_WORKER_ENTERED,
    SENSOR_MGR_IPC_TEST_WORKER_FINISHED,
    SENSOR_MGR_IPC_TEST_DESTROY_WAITING_TRANSITION,
    SENSOR_MGR_IPC_TEST_DESTROY_CLAIMED,
    SENSOR_MGR_IPC_TEST_JOIN_CLAIMED,
    SENSOR_MGR_IPC_TEST_WAITING_JOIN,
    SENSOR_MGR_IPC_TEST_CANCEL_INITIALIZED,
    SENSOR_MGR_IPC_TEST_CANCEL_DESTROYED,
    SENSOR_MGR_IPC_TEST_EVENT_COUNT
} sensor_mgr_ipc_test_event_t;

typedef void (*sensor_mgr_ipc_test_event_hook_fn)(
    sensor_mgr_ipc_client_t *client,
    sensor_mgr_ipc_test_event_t event,
    void *user_data);

void sensor_mgr_ipc_test_set_event_hook(sensor_mgr_ipc_test_event_hook_fn hook,
                                        void *user_data);

#endif
