# CC-SENSOR-CORE Session Result

## Provenance

- Session: `CC-SENSOR-CORE`
- Branch: `feature/sensor-core`
- Foundation base: `07809cb1f3f2b86a8e92ade661c48cb3adb97b52` (`contract-v1` annotated tag)
- Original implementation SHA: `800207949dc28a6e18a3eafe4399f8cb0eb3d811`
- Codex finding fix SHA: `PENDING_FIX_COMMIT`
- Fix-result report SHA: `PENDING_FIX_REPORT_COMMIT`
- Original implementation diff (`contract-v1...8002079`): 37 files, 37 added / 0 modified / 0 deleted. The prior `35 files` statement was incorrect.

## Android Traceability Correction

Pinned `MainActivity.onCreate()` starts `ThreadPirOut` at lines 211-212 and then loads cached Config/Device at lines 214-215. It is therefore incorrect to say cached load occurs before every module initialization. The MGR bind remains later in startup. Runtime Config/Device handling stores the received JSON and replaces the DTO with a newly parsed value; it does not merge present fields into the previous DTO.

## Finding-Fix Implementation

- Mgr IPC client teardown serializes stop/destroy with an API-entry pin registry. Worker-callback stop/destroy avoids self-join; external teardown joins before cancel source, transport, mutex, condition variable, or storage destruction.
- Send/receive/close retain the original transport under documented `state_lock -> io_lock` ordering; a copied fd is not used after unlock.
- A successful CONNECT send is now required before `on_connected` and receive polling. Timeout, closed, and I/O handshake failures close and retry without exposing connected state.
- Config and Device runtime applies parse into fresh Foundation-default values. Typed snapshot and raw JSON are published together only after successful parse; failed/duplicate input preserves both previous values. Startup Device's nine stateful-field reset remains startup-only.
- Lifecycle fan-out snapshots hooks while locked and invokes them unlocked. Callback registration applies to the next fan-out; callback-time destroy is safely refused by the void API until the fan-out returns.
- The real mock MGR asserts CONNECT action/payload, Config→Device ordering, absence of unexpected Sensor replay, and child exit status. IPC tests cover size 65,536 acceptance / 65,537 pre-send rejection, malformed, wrong-direction, invalid-payload, and oversized-record-then-valid-record handling.

## Test Results

| Environment / target | Result |
|---|---|
| macOS arm64, six feature-local builds | 15/15 CTest PASS |
| Ubuntu 22.04 arm64 Docker, six feature-local builds | 15/15 CTest PASS |
| Ubuntu Docker, real AF_UNIX `SOCK_SEQPACKET` Mgr IPC variant | 6/6 CTest PASS |
| Ubuntu Docker, Mgr IPC ASan | 5/5 CTest PASS |
| Ubuntu Docker, Mgr IPC UBSan | 5/5 CTest PASS |
| Ubuntu Docker, Mgr IPC TSan | NOT_PERFORMED: runtime initialization fails before tests with `ThreadSanitizer CHECK failed ... personality(old_personality | ADDR_NO_RANDOMIZE) ... -1` |

The default feature-local CTest configuration contains 15 tests, not 18. The real-transport Mgr IPC configuration contains 6 tests, not a consolidated total of 19.

## Diagnostics and Boundaries

- The earlier reference to `10 diagnostics in 1 file` has no identifiable file or diagnostic text in committed evidence; status: `NOT_REPRODUCED`.
- RV1106 cross-build, board runtime, and hardware QA: `NOT_PERFORMED`.
- macOS sanitizer status is not generalized from this remediation run; Docker ASan/UBSan results above are the recorded sanitizer evidence. TSan is explicitly not a pass.

## Scope

Fix changes are confined to the allowed Config, Health, Mgr IPC, unit/integration test, mock-manager, and session-report paths. No Foundation, contract, root CMake, or other-session production path is changed by this fix.

## State

- `FIX_IMPLEMENTATION_FINISHED` pending exact commit identifiers
- `AWAITING_CODEX_REVERIFY`
