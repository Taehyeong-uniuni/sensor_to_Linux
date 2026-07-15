#ifndef SAVVY_PLATFORM_IPC_TEST_HOOKS_H
#define SAVVY_PLATFORM_IPC_TEST_HOOKS_H

#include <poll.h>

#ifdef __cplusplus
extern "C" {
#endif

/* TC-H-01 deterministic fault injection ONLY - not part of the production
 * API surface, and not something Wave 1 code should ever touch. Real
 * external signal delivery cannot reliably land inside the narrow window
 * needed to test "EINTR arriving exactly at/after an already-elapsed
 * absolute deadline": a single poll(..., 0) call is normally sub-
 * microsecond, far too short a target for an independent thread/process
 * sending real signals to hit reliably. This lets a test substitute a
 * scripted poll() implementation for savvy_ipc_poll_with_deadline_
 * cancelable()'s single internal call site instead, and observe exact
 * call counts/sequences deterministically.
 *
 * When NULL (the default, and the state EVERY test must leave this in
 * when it is done), the real poll() is used, with no other effect on
 * behavior or performance. Tests that set this MUST reset it to NULL
 * before returning - a left-over override would silently redirect every
 * later poll() call in the same process, including unrelated tests and
 * unrelated transports. */
typedef int (*savvy_poll_fn_t)(struct pollfd *fds, nfds_t nfds, int timeout);
extern savvy_poll_fn_t savvy_test_poll_override;

#ifdef __cplusplus
}
#endif

#endif
