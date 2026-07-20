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
 * unrelated transports.
 *
 * FG-M-01: this header intentionally lives here, NOT under include/ - it
 * is a PRIVATE, test-only seam, not a production API. src/platform/linux/
 * ipc/CMakeLists.txt only defines SAVVY_IPC_TEST_HOOKS (and therefore
 * only compiles savvy_test_poll_override/the code below that declares it
 * into ipc_transport_common.c) when SAVVY_BUILD_TESTS is ON; that compile
 * definition is PRIVATE to the savvy_platform_ipc target, so it never
 * leaks into any consumer's own compile flags. tests/contract/
 * CMakeLists.txt separately adds this directory as a PRIVATE include path
 * for the test_ipc target only. A production build
 * (SAVVY_BUILD_TESTS=OFF) never compiles or includes this header at all,
 * and libsavvy_platform_ipc.a exposes no savvy_test_poll_override symbol
 * - verify with `nm -g` on that build's static library. */
typedef int (*savvy_poll_fn_t)(struct pollfd *fds, nfds_t nfds, int timeout);
extern savvy_poll_fn_t savvy_test_poll_override;

#ifdef __cplusplus
}
#endif

#endif
