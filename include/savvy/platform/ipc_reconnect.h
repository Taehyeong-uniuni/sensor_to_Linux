#ifndef SAVVY_PLATFORM_IPC_RECONNECT_H
#define SAVVY_PLATFORM_IPC_RECONNECT_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Minimal reconnect/replay hook for MGR-Sensor IPC (FND-03, Codex F-07).
 * contracts/ipc_action_catalog.md §3 records that real Android replays a
 * fixed set of actions (Config/Device among them) whenever Sensor
 * reconnects. Foundation does not implement a real Config/Device business
 * store or actually resend anything itself - "no disk queue, no durable
 * retry beyond what Android itself has" - it only tracks the connect vs.
 * reconnect transition and invokes caller-supplied callbacks in the
 * documented order, exactly once per reconnect. A Wave 1 daemon (or, for
 * Foundation's own verification, a test) is expected to call
 * savvy_ipc_reconnect_tracker_on_connected() once per successful
 * accept()/connect(), and to supply callbacks that do whatever a real
 * resend requires. */
typedef struct savvy_ipc_reconnect_hooks {
    /* Invoked first. May be NULL to skip (no-op, not an error). */
    void (*request_config)(void *user_data);
    /* Invoked second, only after request_config returns. May be NULL to
     * skip (no-op, not an error). */
    void (*request_device)(void *user_data);
    void *user_data;
} savvy_ipc_reconnect_hooks_t;

typedef struct savvy_ipc_reconnect_tracker {
    bool has_connected_before;
} savvy_ipc_reconnect_tracker_t;

/* Resets *t to "no prior connect observed yet". */
void savvy_ipc_reconnect_tracker_init(savvy_ipc_reconnect_tracker_t *t);

/* Call exactly once per successful accept()/connect() (i.e. once per new
 * transport instance obtained), in the order those connects actually
 * happen - this tracker has no concept of time or concurrency beyond
 * "how many times has this been called."
 *
 * On the FIRST call after init(), this is a no-op: a first connection is
 * normal startup, not a reconnect, so hooks are deliberately NOT invoked.
 *
 * On every call after that, this is a reconnect: hooks->request_config is
 * invoked, then hooks->request_device, in that order, exactly once for
 * this call (calling on_connected() again later - e.g. after another
 * disconnect+reconnect - fires the pair again, once each). If `hooks` is
 * NULL, or either function pointer is NULL, the corresponding step is
 * skipped rather than crashing. This function does not catch what the
 * callbacks themselves do; a callback that fails (e.g. a send that
 * returns an error because the peer already dropped again) must report
 * that failure through its own return path/side channel - it must not
 * itself abort the process, and neither this function nor the caller's
 * surrounding accept/recv loop is disturbed by it either way. */
void savvy_ipc_reconnect_tracker_on_connected(savvy_ipc_reconnect_tracker_t *t,
                                              const savvy_ipc_reconnect_hooks_t *hooks);

#ifdef __cplusplus
}
#endif

#endif
