# CODEX CC-SENSOR-CORE FIX RESULT

## 1. Target

- Starting implementation SHA: `800207949dc28a6e18a3eafe4399f8cb0eb3d811`
- Branch: `feature/sensor-core`
- Governing review: `session_results/wave1/review/CC-SENSOR-CORE_CODEX_REVIEW_1.md`

## 2. Fix Commit

- Implementation fix SHA: `0f01c25f3c7ae55ca0e5650ce69015006ab1ef6e`
- Report SHA: `PENDING_FIX_REPORT_COMMIT`

## 3. Finding Status

| Finding ID | Before | After | Files | Tests |
|---|---|---|---|---|
| CDX-W1-SENSOR-CORE-001 | concurrent stop/destroy UAF | FIXED | `mgr_ipc_client.*`, Mgr IPC tests | 100-cycle stop/stop, stop/destroy, destroy/destroy; blocked connect/recv; callback stop/destroy; ASan |
| CDX-W1-SENSOR-CORE-002 | Config/Device partial merge, no raw JSON | FIXED | Config/Device stores and tests | full→partial default replacement, raw JSON, invalid preservation, duplicate/unknown cases |
| CDX-W1-SENSOR-CORE-003 | copied transport/fd send-close race | FIXED | `mgr_ipc_client.*`, fake transport/tests | 250-cycle send+stop/destroy race; ASan/UBSan |
| CDX-W1-SENSOR-CORE-004 | CONNECT failure exposed connected state | FIXED | Mgr IPC client/tests | timeout, closed, I/O failure, retry, callback, close-count checks |
| CDX-W1-SENSOR-CORE-005 | insufficient IPC/lifecycle assertions | FIXED | Mgr IPC unit/integration tests, mock MGR | CONNECT/payload, Config→Device, reconnect, no unexpected replay, child status, 64 KiB and invalid/oversized input |
| CDX-W1-SENSOR-CORE-006 | lifecycle callback re-entry deadlock | FIXED | lifecycle implementation/tests | start/config/shutdown re-entry and callback-time registration tests |
| CDX-W1-SENSOR-CORE-007 | inaccurate session provenance/results | FIXED | `CC-SENSOR-CORE.md` | SHA, 37-file original diff, 15-test CTest count, sanitizer, Android-order audit |

## 4. Code Changes

- `001`: API call entry pins plus destroy claim/wait sequencing retain storage until active callers and the worker have finished; worker callbacks use detached terminal cleanup to avoid self-join.
- `002`: Config/Device typed value and raw JSON are one snapshot allocation. Runtime parse starts from defaults; parse failure publishes nothing.
- `003`: all send/receive/close accesses use the original transport while holding `state_lock -> io_lock`.
- `004`: CONNECT send failure closes the transport and retries before connected notification or receive.
- `005`: mock MGR validates the CONNECT envelope and rejects any subsequent Sensor outbound record; integration verifies action order and child success. Unit coverage includes 65,536/65,537 and bad/oversized input recovery.
- `006`: hook lists are copied under the registry mutex and called after unlock. Register-during-callback begins at the next fan-out; destroy during a callback returns without invalidating its stack snapshot.
- `007`: the original report now carries exact original/fix provenance, actual CTest counts, corrected Android ordering, and explicit verification limits.

## 5. Test Results

| Test | macOS arm64 | Docker arm64 | Sanitizer |
|---|---:|---:|---|
| six feature-local CTest configurations | 15/15 PASS | 15/15 PASS | Mgr IPC ASan 5/5 PASS; UBSan 5/5 PASS |
| Mgr IPC real AF_UNIX `SOCK_SEQPACKET` | not applicable | 6/6 PASS | non-sanitized real transport |
| Mgr IPC concurrency and input-boundary coverage | included in 5/5 | included in 5/5/6/6 variants | ASan/UBSan PASS |

## 6. Concurrency

- stop/destroy: concurrent stop/stop, stop/destroy, and destroy/destroy execute through pinned API entries; teardown happens only after worker join and active-entry drain.
- send/close: send holds the I/O lifetime lock through transport use; close follows the same lock order. A 250-cycle send/shutdown stress test covers stop and destroy paths.
- callback reentrancy: Mgr callbacks can call stop/destroy without self-join; lifecycle hooks run with no registry mutex held.
- fd/thread lifecycle: 60 connect/disconnect cycles check in-cycle bounded fd use and post-stop return to baseline tolerance. Worker join is required before external teardown.

## 7. Android Parity

- Config and Device runtime values are full replacement from defaults, and the successful input JSON is readable only while its snapshot handle is retained.
- Startup Device preserves its nine-field reset; runtime Device does not apply that startup reset.
- The corrected trace says `ThreadPirOut.start()` precedes cached Config/Device load; it does not claim cache load precedes every module initialization.
- Reconnect sends CONNECT before MGR Config→Device delivery; Sensor does not replay cached Config/Device or pre-connect dropped outbound messages.

## 8. Scope Verification

- Fix-commit allowed-path violations: `0`.
- Foundation/contract changes: `0`.
- Root CMake changes: `0`.
- Other-session production changes: `0`.
- The governing review file was not modified by either fix commit. It already exists in the branch as user-owned commit `2b1c3d5`; that pre-existing commit is not part of this fix scope.

## 9. Verification Boundary

- RV1106 cross-build: `NOT_PERFORMED`.
- RV1106 board runtime: `NOT_PERFORMED`.
- Hardware QA: `NOT_PERFORMED`.
- Docker arm64 TSan: `NOT_PERFORMED`. Runtime fails before test execution with `ThreadSanitizer CHECK failed ... personality(old_personality | ADDR_NO_RANDOMIZE) ... -1`. ASan/UBSan plus stress and real transport provide the recorded alternative evidence; this is not a TSan pass.

## 10. Final State

- `FIX_IMPLEMENTATION_FINISHED`
- `AWAITING_CODEX_REVERIFY`
