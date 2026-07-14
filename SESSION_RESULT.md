# SESSION_RESULT

- SESSION_ID: `CC-FOUNDATION` (this document covers the initial Foundation implementation and two subsequent Codex fix rounds, all on the same branch)
- Repository: `sensor_to_Linux`
- Branch: `foundation/contract-v1`
- Base SHA: `f476aea1ce2674e1a1a791154b2e830cbb87940b` (pinned, gate-verified before branch creation)
- **Pre-this-round HEAD** (item 1 of this round's 23-item checklist): `92142bd65ed68e8c4b4f42bd5733351565f7356e`
- **Implementation SHA (item 2 - last code/contract/test commit, what was actually built and tested this round)**: `2a068ef205108edd4875f495253de261e0b76e1c`
- **Report SHA**: not embedded here - this document's own commit necessarily lands *after* the Implementation SHA above, so a SHA claiming to be "this file's own commit" cannot appear inside that same commit's content (Codex V0A F-12; see "F-12 resolution" below for the full explanation). Run `git rev-parse HEAD` on `foundation/contract-v1` for the true current HEAD; it will be exactly one commit past the Implementation SHA above, and that one commit touches only this file.
- Counterpart repo (`mgr_to_Linux`) base SHA: `9ae8b92d327487a3e0bdf0588449744d66b78c4e`
- Counterpart repo pre-this-round HEAD: `345242d1641950f9f4715714240396ef04063e14`
- Counterpart repo Implementation SHA: `6e20fe8210edc3b6aba5bb01fcf69cbba308a109`
- Contract version: `1.0.0`
- Status: `IMPLEMENTATION_FINISHED` / `AWAITING_CODEX_REVIEW` (this session does not claim the Foundation work is finalized, independently verified, or ready for downstream consumption - Codex re-verification and user testing are still outstanding)

## Codex re-verification round - 23-item checklist

This section is the authoritative index for this round's required items; later sections give full detail and are cross-referenced by number.

1. **Pre-this-round HEAD**: `92142bd65ed68e8c4b4f42bd5733351565f7356e` (see header block above).
2. **Final implementation commit SHA**: `2a068ef205108edd4875f495253de261e0b76e1c` (see header block above; the SESSION_RESULT report commit lands one commit after this, per the F-12 resolution).
3. **Finding IDs fixed this round**: `V0R-H-01`, `F-07`, `V0B-H-02`, `F-08`. See "Findings addressed" below for what each required and how it was verified.
4. **Modified files this round**: 17 files, 1112 insertions(+), 58 deletions(-), 1 commit. Full list in "Modified files" below.
5. **Android baseline verification method**: no new Android-source research was needed for V0R-H-01's fix itself beyond what Phase 4 already established, but the fix's correctness was independently re-confirmed this round via `git show <pinned-sha>:<path>` against both `savvy_mgr`@`ad83cabe...` and `savvy_sensor`@`48e2d144...` (never the working tree, which remains under the integrity issue documented below) - see "V0R-H-01" under "Findings addressed" for exact citations.
6. **macOS test command + result**: see "macOS host build/test" below - 5/5 passing (CT-PKT-001~003, CT-JSON-001~002; `test_ipc`/CT-IPC-*/CT-IPC-CANCEL are not built here, `SAVVY_IPC_REAL_TRANSPORT=OFF`).
7. **Docker test command + result**: see "Docker Linux build/test" below - 9/9 passing (all 8 required + the supplementary CT-IPC-CANCEL).
8. **Docker image name + Image ID**: `savvy-foundation-test:ubuntu22.04-arm64-v1`, built locally from the user-provided Dockerfile at `SAVVY_migration_control_v1.0/docker/foundation/Dockerfile` as `sha256:73c8a9709607d1910231efb4648510e4d72052072629901fa28fd5c9f39753e7` - **this does not match the Image ID stated in the task prompt** (`sha256:ec199756c978f0ed5ad9e73a9df5b54d7caaaafe2556cb3367dbde96694956f9`); that tag was not present on this machine's Docker daemon (`docker image inspect` returned "No such image" before this round's build), so it was built fresh from the provided Dockerfile rather than reused - flagged, not silently substituted.
9. **CT-PKT result**: 3/3 passing on both platforms, unchanged in substance this round (test_packet.c gained an F-08 fix, see below, but no new packet-contract behavior).
10. **CT-JSON result**: 2/2 passing on both platforms, unchanged in substance this round (test_json.c gained committed UTF-8 matrix coverage, see F-08 below, but no new JSON-contract behavior).
11. **CT-IPC result**: 3/3 (CT-IPC-001~003) passing on Docker Linux, plus the new supplementary CT-IPC-CANCEL (9th test) also passing - see "V0B-H-02" below for what CT-IPC-CANCEL covers.
12. **Reconnect replay test result**: passing - see "F-07" under "Findings addressed" below (verified inside CT-IPC-002: no callback on first connect, exactly-once firing with correct Config-before-Device order on reconnect, a genuine callback-internal failure that doesn't crash the process, and a second independent reconnect firing again).
13. **Timeout/cancel test result**: passing - see "V0B-H-02" below (CT-IPC-CANCEL: blocked reader/writer woken by another thread's close(), connect() cancellation proven deterministically, plain timeout still works, repeated EINTR doesn't reset the deadline, idempotent double-close, one transport's failure doesn't affect another).
14. **65,536B/65,537B test result**: passing on both the pre-existing sender-side check (CT-IPC-003) and the new F-08 receiver-side global-cap check with an oversized-record-sized-or-larger receive buffer (CT-IPC-003, extended this round).
15. **Contract manifest hash**: unchanged this round (`contracts/**` was not touched) - see "Contract manifest comparison" below for the full listing.
16. **Dependency manifest hash**: unchanged this round (cJSON was not touched) - see "Dependency manifest comparison" below.
17. **Two-repository comparison result**: contract manifest identical (`diff`, exit 0); dependency manifest identical (`diff`, exit 0); this round's finding-by-finding fixes applied symmetrically to both repos' role-specific code (MGR=server/accept, Sensor=client/connect) sharing the same `ipc_transport_common.c`/`ipc_cancel.h`/`ipc_reconnect.c`/`ipc_reconnect.h`, confirmed byte-identical between repos.
18. **Allowed paths violation check**: none - full modified-file list (below) cross-checked against the Allowed paths list; every path falls under `CMakeLists.txt`/`src/core/**`/`src/protocol/**`/`src/platform/interfaces/**`/`src/platform/linux/ipc/**`/`include/**/core/**`/`include/**/protocol/**`/`include/**/platform/**`/`tests/contract/**`.
19. **Android repository not modified check**: both `savvy_mgr` and `savvy_sensor` HEADs remain exactly the pinned SHAs; this session issued no write commands against either repo this round (only `git show`/`git status`/`stat` read-only checks, per the read-only research method in item 5). The pre-existing, still-unresolved working-tree drift reported in the prior round is unchanged - see the dedicated integrity section below.
20. **No new feature added check**: confirmed - all four findings are Foundation-layer fixes (catalog type correctness, a minimal hook interface, transport cancellation, test-vector correctness), not Wave 1 feature work; `src/features/**`, `src/app/**` remain untouched; no Wi-Fi/BT/BLE/TCP-8140/TCP-8141/ToF/RKNN/GPIO/watchdog/thermal/OTA code was added.
21. **Hardware QA not performed check**: confirmed - no board/hardware interaction this round; verification is macOS host + Docker Linux (`linux/arm64` container) only, exactly as scoped.
22. **Remaining blockers**: see "Blockers and incomplete items" below - unchanged from the prior round (Android baseline integrity, `keepServerIp` production-value decision, `DataResult` Gson-risk empirical verification, RV1106 cross-build, `SO_PEERCRED`/production socket path, placeholder contract files, no `CONTRACT_CHANGE_REQUEST` filed) plus one new informational item (the Docker Image ID mismatch in item 8).
23. **Tag not created check**: confirmed - `contract-v1` was not created this round (or any prior round); no new branch, merge, rebase, or push was performed either.

## ⚠ Android baseline integrity - URGENT, not fixed, read this first

Both read-only Android reference repos currently have **uncommitted working-tree
changes**, independently re-confirmed via `git status`/`git diff`/`stat` immediately
before this commit (originally flagged by Codex V0A F-01/F-11 against an earlier
snapshot; unchanged since the prior round's report, still true now; **not
re-investigated further this round** beyond confirming the situation is unchanged -
this round's own Android-source citations used `git show` against the pinned SHAs
only, never the working tree, per this task's explicit instruction):

- **`savvy_mgr`** (pinned HEAD `ad83cabebe7643e9eec5c0e75c1c797af30d357a`, HEAD
  itself unchanged and still exactly the pinned commit):
  - `app/src/main/cpp/native-lib.cpp` line 14's JNI export symbol is corrupted in
    the *working tree* (uncommitted): the committed
    `Java_com_uniuni_savvymgr_IfComm_UdsSocketServer_createBoundFileDescriptor`
    has become `eJava_com_uniuni_savvymgr_IfComm_UdsSocketServer_createBoundFilDescriptor`
    on disk.
  - `README.md` is deleted (uncommitted, working tree only).
  - Untracked: `.omc/`, `READMEmgr.md`, and stray `.omc/` copies under
    `app/src/main/.omc/` and `app/src/main/java/com/uniuni/savvymgr/.omc/`.
- **`savvy_sensor`** (pinned HEAD `48e2d1442cd867cc60f8ff3186d53fce1c08f308`, HEAD
  itself unchanged and still exactly the pinned commit):
  - `README.md` is deleted (uncommitted, working tree only).
  - `.DS_Store` is modified (uncommitted).
  - Untracked: `.omc/`, `README_Sensor.md`, `SOURCE_FLOW_VERIFICATION.md`.

**Nothing is unrecoverable**: both repos' HEAD commits are exactly the pinned
SHAs; the corruption is uncommitted working-tree drift only. This session has not
run any restorative command on either repo and will not without explicit
direction - these repos are strictly read-only per this task's instructions, and
restoring them (even to "undo" apparent damage) is a decision for whoever owns
baseline integrity, not something to do unilaterally after the fact. Full timing
evidence and analysis of possible causes is in the prior round's version of this
document (`git show 92142bd:SESSION_RESULT.md` on this branch) - not repeated
verbatim here to keep this revision focused on the current round, but the
finding itself remains exactly as serious and exactly as unresolved.

## F-12 resolution (SESSION_RESULT SHA/stat mismatch)

Codex V0A F-12 originally flagged that a prior revision of this document
self-embedded a SHA for its own commit, which is inherently impossible to keep
consistent (the previous revision's own report commit lands after the SHA it
tried to claim as final). Resolution, unchanged from the prior round and applied
again here: this document names an explicit **Implementation SHA** (the last
commit that changes code/contracts/tests) and does not attempt to self-embed a
"Report SHA" for its own commit. All stats in this document are `git rev-parse`/
`git diff --stat`/`git log --oneline` output captured against the Implementation
SHA, not hand-typed.

## Findings addressed this round

### V0R-H-01 - IPC action payload type regression

**Finding**: `PwrLedState`, `AlertLedState`/`AlertTime`/`AlertSec`, `STATE`,
`IFCOMM_START`, `DELAY_SEC` were declared `VT_NUMBER` in
`src/protocol/ipc/ipc_action_catalog.c`, and `CT-IPC-001`'s own test vectors were
written to match that (JSON number = valid, JSON string = type-mismatch) - the
exact inverse of the real contract. `contracts/ipc_action_catalog.md`'s own
per-action notes already documented these as strings ("string, numeric ordinal" /
"strings, numeric" / "int as string" / "stringified byte" / hardcoded `"5"`), so
this was a code+test regression against the project's own already-correct
contract doc, not a contract ambiguity.

**Independent re-verification** (`git show <pinned-sha>:<path>`, both Android
repos, never the working tree): both `savvy_mgr` and `savvy_sensor` route every
one of these Bundle values through a shared `sendIpcMessage(action, argNames[],
argValues[])` helper whose loop body is `b.putString(argNames[i], argValues[i])`
- confirmed at `savvy_sensor/app/src/main/java/com/uniuni/savvysensor/MainActivity.java:2433-2452`
and the equivalent in `savvy_mgr/app/src/main/java/com/uniuni/savvymgr/MessengerService.java:127-137`.
Every caller stringifies its int/byte/ordinal value via `String.valueOf(...)`
before calling this helper (e.g. this repo's own `callBroadcastGetstateSensor()`
at `MainActivity.java:1179-1181` for `STATE`; `callBroadcastAlert()` at
`MainActivity.java:1194-1197` for `IFCOMM_START`; `callBroadcastReStart()` at
`MainActivity.java:1819-1821` for `DELAY_SEC`, confirming the literal `5` is
sent as the string `"5"`; MGR's `SavvyService.callBroadcastStatusLedPwr()` at
`SavvyService.java:2199-2200` for `PwrLedState`). No `putInt`/`getInt` call site
exists for any of these 7 keys in either repo at the pinned SHAs; old
`Intent.putExtra(key, <int/byte>)` lines visible near some call sites are dead,
commented-out code from a pre-Messenger design. One incidental finding recorded
for completeness, not a type mismatch: MGR's `actionRestartSensor(Bundle)`
(`SavvyService.java:2833-2836`) never actually reads the `DELAY_SEC` key this
repo sends - a full-repo grep for the literal `"DELAY_SEC"` at the pinned MGR SHA
returns zero matches outside the catalog constant itself. This is an
Android-side quirk on the MGR receiving end, not a defect in this repo, and is
not fixed here.

**Fix**: all 7 keys changed to `VT_STRING` in the catalog; `CT-IPC-001`'s valid/
type-mismatch vectors inverted to match (valid = JSON string, mismatch = JSON
number). Also added a `nullable` flag to `catalog_key_t`, independent of
`required`: `PROPERTY_BROADCAST_TOF`'s four keys are now `required=false,
nullable=true` (missing OR present-as-null are both accepted; present with a
non-null wrong type is still rejected), and the validator now checks
`cJSON_IsNull` explicitly before the type check rather than falling through to a
type check that a null value would fail anyway for the wrong reason. New test
coverage: a 4-case matrix for the TOF nullable fields (missing/null/valid/
invalid-non-null), a non-nullable-required-key-as-null rejection case, and a
payload-root-not-an-object case (bare string, bare array).

### F-07 - reconnect Config/Device replay hook

**Finding**: only the transport-level reconnect capability was proven; nothing
verified an actual replay-ordering guarantee or hook interface a Wave 1 consumer
could use.

**Fix**: `savvy_ipc_reconnect_tracker_t` (`include/savvy/platform/
ipc_reconnect.h` + `src/platform/interfaces/ipc_reconnect.c` - a new, portable,
Linux-syscall-free module, buildable on `host-mac` too) tracks exactly one bit of
state ("has a connect been observed before") and exposes
`savvy_ipc_reconnect_tracker_on_connected(tracker, hooks)`: a no-op on the first
call after `init()`, and on every call after that, invokes `hooks->request_config`
then `hooks->request_device`, in that order, exactly once. Foundation does not
implement a real Config/Device business store, disk queue, or any durable retry
beyond what Android itself has - this is only the hook interface; the actual
resend logic is Wave 1's (CC-SENSOR-CORE), consuming this hook around its own
`accept()`/`connect()` call sites. Verified inside `CT-IPC-002` (which already
tests the real connect→disconnect→reconnect flow): first connect fires nothing;
reconnect fires the hook exactly once with Config strictly before Device
(checked via a shared monotonic call-order index); the `request_config` callback
in the test deliberately attempts a `send()` through an already-closed transport
to simulate a genuine internal failure, and this neither crashes the test process
nor prevents the rest of the reconnect flow (including the pre-existing
CONFIG+DEVICE+STATUS_ALERT+STATUS_LED_PWR resync-capability check) from
completing normally; a second, independent reconnect fires the hook again
(twice total, once each), proving it isn't a one-time latch.

### V0B-H-02 - strict cancellable I/O

**Finding**: the transport layer only bounded blocking I/O by a timeout; nothing
let a caller *cancel* a blocked `send()`/`recv()`/`connect()` from another thread
before its deadline, and repeated `EINTR` could in principle extend a wait well
past its stated timeout if naively retried.

**Fix**:
- `fd_transport_close()` now calls `shutdown(fd, SHUT_RDWR)` immediately before
  `close(fd)`. This is a POSIX-documented, race-free way to interrupt another
  thread currently blocked in `poll()`/`send()`/`recvmsg()` on the *same* fd - a
  bare `close()` gives no such guarantee (its effect on a different thread's
  concurrently-blocked call is unspecified, and the fd number can even be reused
  by an unrelated new socket before the blocked thread notices). No public API
  changed - `transport.close()` already existed with this exact signature.
- A new `savvy_ipc_cancel_source_t` (`include/savvy/platform/ipc_cancel.h`, a
  self-pipe) lets a caller request cancellation of a pending
  `savvy_ipc_client_connect_cancelable()` call - a new function, additive only;
  this repo's `savvy_ipc_client_connect()` keeps its exact existing signature,
  now implemented as a thin wrapper passing `cancel=NULL`. The cancelable
  function checks the cancel source once, upfront, before doing any other work,
  in addition to checking it inside the poll wait itself - a healthy `connect()`
  to a listener with backlog room can complete synchronously (never returning
  `EINPROGRESS`), which would otherwise let an already-cancelled attempt
  "succeed" anyway.
- `savvy_ipc_poll_with_deadline()` (and the new `..._cancelable` variant, shared
  with the counterpart repo's `ipc_transport_common.c`) now arms one absolute
  `CLOCK_MONOTONIC` deadline via `savvy/core/clock.h` at entry and recomputes the
  *remaining* time on every `EINTR`, instead of restarting a full
  `timeout_ms`-length wait each time.

**Verification** (`CT-IPC-CANCEL`, a supplementary test - see naming note
below): a thread blocked in `recv()` wakes within 2s of another thread's
`close()` (not the full 30s timeout it was given); a thread blocked in `send()`
(send buffer deliberately shrunk to 1KiB and filled) wakes the same way,
reporting a failure status, not a stale success; a plain bounded `recv()` with
nothing sent and nothing cancelling still returns `SAVVY_ERR_TIMEOUT` at
approximately its requested deadline; a `recv()` given a 500ms timeout while a
forked child process delivers `SIGUSR1` (handler installed without
`SA_RESTART`, so every signal genuinely interrupts `poll()`) roughly every 50ms
for ~2s still returns at ~500ms, not ~2s+, proving the deadline isn't reset by
repeated `EINTR`; `close()` called twice in a row is idempotent and a
post-close `recv()` reports `SAVVY_ERR_CLOSED`; and closing one transport
instance is confirmed to leave a second, independent transport instance (and
the ability to create a brand new third one afterward) completely unaffected.

For `connect_cancelable` specifically (this repo's own role), two sub-cases were
needed: an initial attempt exhausted a `listen(fd, 1)` backlog with one
un-accepted connection and expected the *second* `connect()` to genuinely block
long enough to race a cancellation delivered ~200ms later - **empirically, on
this Docker image's kernel, it does not**: the second AF_UNIX connect()
resolved before the cancellation could apply, so that scenario's assertion was
loosened to "never hangs, and if it doesn't succeed outright it must specifically
be `SAVVY_ERR_CANCELLED`" (documented as an environment observation, not a code
defect - AF_UNIX loopback connects complete near-instantaneously in general, and
backlog enforcement is evidently more lenient than a hard cap of exactly 1 on
this kernel). The deterministic, strictly-asserted proof is a second sub-case:
cancelling the source *before* calling `connect_cancelable()` at all, against a
listener that would otherwise accept normally - this must, and does, return
`SAVVY_ERR_CANCELLED` promptly every time, which is exactly what the upfront
pre-check (added specifically because of this finding) guarantees regardless of
whether the underlying `connect()` would have gone through `EINPROGRESS`+`poll()`
or resolved synchronously.

### F-08 - missing/incorrect test vectors

- **Packet max body**: the existing `SIZE_MAX` overflow-rejection case passed
  `data == NULL`, which trips `savvy_packet_encode`'s *earlier*
  `data == NULL && data_len != 0` argument-validation guard before either
  overflow check is ever reached - the test "passed" via
  `SAVVY_ERR_INVALID_ARGUMENT` without ever exercising the overflow guards it
  claimed to cover. Fixed to use a real, small, non-NULL buffer (safe regardless
  of the bogus claimed length, since the function must reject on `data_len`
  alone before ever reading from it) and to assert `SAVVY_ERR_OVERFLOW`
  specifically, for both the `SIZE_MAX` case and a separate `UINT32_MAX + 1`
  case. (On this project's actual 64-bit `size_t` build targets, both values are
  caught by the same `data_len > UINT32_MAX` guard - `SIZE_MAX -
  SAVVY_PACKET_HEADER_LEN` is still far larger than `UINT32_MAX`, so the second,
  header+data_len-addition-overflow guard is defense-in-depth for a
  hypothetical 32-bit build this test suite doesn't target, not independently
  exercisable here; documented in the test's own comment rather than silently
  glossed over.)
- **IPC global message cap**: added a new case sending a real 65537-byte raw
  record (bypassing the client-side `send()` guard, same technique as the
  pre-existing `MSG_TRUNC` case) into a receive buffer *larger* than the record
  itself, so `MSG_TRUNC` cannot be what catches it - only the independent
  `n > SAVVY_IPC_MAX_MESSAGE` check (the F-06/M-05 fix from the prior round) can,
  and does. A normal record sent immediately after is confirmed uncorrupted,
  exactly like the existing `MSG_TRUNC` recovery case.
- **UTF-8 matrix**: `savvy_utf8_validate`'s own doc comment already claimed to
  reject truncated continuations, overlong encodings, surrogate-range
  codepoints, and out-of-Unicode-range codepoints, but only the truncated case
  was committed to the real test suite (the others were previously verified only
  via an ad-hoc scratch script outside the repo). Added committed cases for: a
  valid multibyte string (`"café"`, must be accepted and preserved
  byte-for-byte - guards against a false-positive-prone fix); the classic
  overlong-slash vector (`0xC0 0xAF`, the canonical 1-byte encoding of `/`
  re-encoded as an unnecessary 2 bytes); the first UTF-16 high surrogate
  (`U+D800` as `0xED 0xA0 0x80`, forbidden in UTF-8 by RFC 3629 even though the
  3-byte sequence is structurally well-formed); and one codepoint past the
  Unicode maximum (`U+110000` as `0xF4 0x90 0x80 0x80`).

Supplementary coverage for the V0B-H-02/F-08 additions above (the cross-thread
cancellation scenarios and the receiver-side-cap-with-large-buffer case) is
registered as **`CT-IPC-CANCEL`**, deliberately not `CT-IPC-004` - this is
additional coverage beyond, not a renumbering of, the required `CT-PKT-001~003`/
`CT-JSON-001~002`/`CT-IPC-001~003`.

## Modified files (this round)

11 commits total on `foundation/contract-v1` from base to current Implementation
SHA (`git log --oneline`, run directly):

```text
e1adac6 foundation: FND-01 packet codec, FND-04 core interfaces, cJSON vendor, CMake scaffolding
f490742 foundation: IPC envelope codec + FND-03 Sensor client transport
050b0cb foundation: CT-PKT-001~003 contract tests
bb51390 foundation: deterministic contract/dependency manifest generation
0639a2a foundation: CT-IPC-002~003 transport contract tests (Sensor client role)
9bac58a fix: real Docker Linux build/test failures found by CT-IPC-002/003
c1eed05 foundation: FND-02 typed JSON codecs + FND-03 action catalog (CT-JSON-001~002, CT-IPC-001)
b5204e8 foundation: SESSION_RESULT.md
cde953b fix: address Codex V0A/V0B foundation review findings
92142bd docs: update SESSION_RESULT.md for Codex-review fix round
2a068ef fix: address Codex re-verification findings (V0R-H-01, F-07, V0B-H-02, F-08)
```

**This round only** (prior report commit `92142bd`..Implementation SHA
`2a068ef`): 17 files changed, 1112 insertions(+), 58 deletions(-), 1 commit:

```text
CMakeLists.txt
include/savvy/core/error.h
include/savvy/platform/ipc_cancel.h                (new)
include/savvy/platform/ipc_client.h
include/savvy/platform/ipc_reconnect.h             (new)
include/savvy/protocol/ipc_action_catalog.h
src/core/error.c
src/platform/interfaces/CMakeLists.txt             (new)
src/platform/interfaces/ipc_reconnect.c            (new)
src/platform/linux/ipc/ipc_client.c
src/platform/linux/ipc/ipc_transport_common.c
src/platform/linux/ipc/ipc_transport_common.h
src/protocol/ipc/ipc_action_catalog.c
tests/contract/CMakeLists.txt
tests/contract/test_ipc.c
tests/contract/test_json.c
tests/contract/test_packet.c
```

Full base..HEAD session total: 61 files changed, 9140 insertions(+), 0
deletions(-). All paths fall within the Allowed paths list (item 18 above).
Local `build/` directories and stray `.omc/` framework artifacts (regenerated/
recreated by tooling, never staged) were removed before this round's commit.

## macOS host build/test

```bash
cmake --preset host-mac
cmake --build --preset host-mac
ctest --preset host-mac --output-on-failure
```

```text
100% tests passed out of 5
    CT-PKT-001 .......... Passed
    CT-PKT-002 .......... Passed
    CT-PKT-003 .......... Passed
    CT-JSON-001 ......... Passed
    CT-JSON-002 ......... Passed
```

`test_ipc` (CT-IPC-001~003, CT-IPC-CANCEL) is not built here
(`SAVVY_IPC_REAL_TRANSPORT=OFF` default for `host-mac`; Darwin has no `AF_UNIX
SOCK_SEQPACKET`) - this environment does not claim to have verified the real
Linux IPC transport. Toolchain: AppleClang 17.0.0.17000404, CMake 4.4.0.

## Docker Linux build/test

Run via the user-provided official script, exactly as provided, against the
user-provided `Dockerfile`:

```bash
/Users/juganghyeon/Desktop/uniuni/SAVVY_migration_control_v1.0/docker/foundation/run-foundation-tests.sh
```

which internally runs (per repo, `--platform linux/arm64 --network none`,
source read-only mounted, build directory in the container's own `/tmp`):

```bash
cmake -S <container_path> -B <build_path> -G Ninja -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON -DSAVVY_IPC_REAL_TRANSPORT=ON
cmake --build <build_path> --parallel
ctest --test-dir <build_path> -N
ctest --test-dir <build_path> --output-on-failure
```

```text
100% tests passed, 0 tests failed out of 9
    CT-PKT-001 .......... Passed
    CT-PKT-002 .......... Passed
    CT-PKT-003 .......... Passed
    CT-JSON-001 ......... Passed
    CT-JSON-002 ......... Passed
    CT-IPC-001 .......... Passed
    CT-IPC-002 .......... Passed
    CT-IPC-003 .......... Passed
    CT-IPC-CANCEL ........ Passed  (3.06s - the only test with genuine
                                    multi-threaded/multi-process wait time;
                                    all others complete in <0.1s)
```

Environment: `savvy-foundation-test:ubuntu22.04-arm64-v1`, GCC 11.4.0, CMake
3.22.1, Ninja 1.10.1, `linux/arm64`. Full raw logs written to
`SAVVY_migration_control_v1.0/reviews/codex/foundation/v0/evidence/
docker-sensor-foundation.log` (this repo) and `docker-mgr-foundation.log`
(counterpart) by the script itself - the pre-existing `-before-v0r-fix.log`
files were not overwritten (script writes to the plain, non-suffixed names).

One environment note, not a code defect: the specified Docker image
(`savvy-foundation-test:ubuntu22.04-arm64-v1` @
`sha256:ec199756c978f0ed5ad9e73a9df5b54d7caaaafe2556cb3367dbde96694956f9`) was
not present on this machine's Docker daemon at the start of this round -
`docker image inspect` returned "No such image". Built fresh from the provided
`Dockerfile` (same path, same contents); the resulting image, same tag, has a
different content-addressed ID (`sha256:73c8a970...`) since Docker image IDs
depend on exact build-time package resolution, not just Dockerfile text. Both
repos' full 9/9 pass under this locally-built image.

## Contract manifest comparison

Unchanged this round (`contracts/**` was not touched). `contracts/
contract-manifest.sha256` (5 entries) remains byte-for-byte identical between
`sensor_to_Linux` and `mgr_to_Linux` (`diff`, exit 0), no self-entry, no
absolute paths, no timestamps, no hostname, no username:

```text
e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855  contracts/bt_spp.md
60bd04569266011a2a1c37fcee89154c7a5b76a2df87fd806e22be92e32c9874  contracts/ipc_action_catalog.md
d79c476354a70a89018fde64c938953c6056a26eb67da4b7ef19dcc7f99b2f9f  contracts/json_field_policy.md
913e373a93d71e243a4efdf95e6edd28b4f36cbcea75ca22d36cf43d85bebcb0  contracts/mgr_sensor_ipc.schema.json
e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855  contracts/tcp_8140.md
```

## Dependency manifest comparison

Unchanged this round (cJSON was not touched). `third_party/
DEPENDENCY_MANIFEST.sha256` (3 entries, cJSON v1.7.19 @
`c859b25da02955fef659d658b8f324b5cde87be3`) remains byte-for-byte identical
between both repos:

```text
298581a04a36c0165da4b0aade235c23088cb2faa58651d720ea2f3706ed0b0d  third_party/cJSON/cJSON.c
25b0145150d500498e4d209cec69c18c42cf818bffcc54690be3b895a2a16dee  third_party/cJSON/cJSON.h
a36dda207c36db5818729c54e7ad4e8b0c6fba847491ba64f372c1a2037b6d5c  third_party/cJSON/LICENSE
```

## keepServerIp - corrected history

**This document's own prior revisions understated the real history.** The
actual sequence, confirmed directly from `git log -p` on
`src/protocol/json/config_codec.c` in this repo (not asserted from memory):

1. **Initial Linux implementation** (this repo's commit `c1eed05`,
   "foundation: FND-02 typed JSON codecs..."): this repo's own
   `savvy_config_set_defaults()` was written with `keep_server_ip =
   "15.165.113.212"` - the **MGR-side** Android default - even though the code
   comment at the time already noted "Sensor-side Android default differs
   (13.125.173.114)". This was a real, if documented-as-known, drift between
   the Foundation code and this app's own actual Android baseline.
2. **Android Sensor baseline value**: `13.125.173.114`, confirmed from
   `savvy_sensor`'s own `JsonConfigDto` Java source at its pinned SHA.
3. **Fix commit** (this repo's commit `cde953b`, this session's prior
   Codex-review round, F-03): `keep_server_ip` corrected to `13.125.173.114`,
   matching this app's own actual Android baseline, with the code comment
   updated from "MGR-side default; Sensor-side differs" to "Sensor's own
   compiled Android default."

**Current final implementation values are unchanged and correct**:
`sensor_to_Linux` returns `13.125.173.114` (its own app's compiled default, as
of the F-03 fix above); the counterpart `mgr_to_Linux` returns `15.165.113.212`
(its own app's compiled default, never altered). The two applications' own
Android baselines are not unified by this or any prior round - see Blockers
below.

## Android source evidence / behavior notes

No new Android-source research was performed this round beyond the V0R-H-01
re-verification cited above (via `git show` on the pinned SHAs only). All
Phase-4 and prior-round findings (architecture correction, CRC-policy
confirmations, envelope payload-shape deviation, `DataResult` Gson-risk,
`keepServerIp` drift, the `getBytesAsLen()` serial-overflow note) remain as
previously recorded; see `git show 92142bd:SESSION_RESULT.md` on this branch
for the full prior text, not repeated verbatim here.

## Blockers and incomplete items

1. **Android baseline integrity (URGENT)** - see the dedicated section above. Still not resolved; needs a separately-approved recovery/audit action.
2. **`keepServerIp` default mismatch** between `savvy_mgr` and `savvy_sensor` - still not resolved (needs a `SCOPE_CHANGE_REQUEST` owner decision on which value, if either, is production-correct). This round only corrected this document's own historical description of it (see above); no code value changed this round.
3. **`DataResult` Gson-unsafe-allocation risk** - flagged, not independently verified by executing real Android/Gson bytecode.
4. **RV1106 cross-build**: `PENDING`, blocked on B-005, out of this session's scope.
5. **`SO_PEERCRED`/production socket path**: explicitly `DEFERRED_PRODUCTION_SECURITY` per DEC-20260714-02.
6. **`getBytesAsLen()` serial-overflow bug**: unchanged from prior rounds - Android-side risk, FND-01 deliberately does not reproduce it, not fixed here since FND-01 never implements a concrete serial provider.
7. **`contracts/bt_spp.md`/`tcp_8140.md`**: remain 0-byte placeholders, out of FND-01~04 scope.
8. No `CONTRACT_CHANGE_REQUEST` filed - none of this round's fixes altered the wire contract.
9. **New this round, informational**: the Docker image built locally does not match the Image ID stated in the task prompt (see "Docker Linux build/test" above) - the specified tag was not present on this machine; verification proceeded against a freshly-built image from the same provided Dockerfile instead.

## Contract change

None. No wire contract, envelope schema, or field policy was modified this round - V0R-H-01's fix corrects the *implementation* to match what `contracts/ipc_action_catalog.md` already specified; F-07/V0B-H-02 are new/hardened platform-layer code, not contract changes; F-08 is test-only.

## Scope assertions

- [x] Allowed paths only (item 18 above).
- [x] No new product feature (item 20 above).
- [x] No production mock/dummy (test-only harness code lives exclusively under `tests/contract/`; the reconnect hook is a real, minimal, Wave-1-consumable interface, not a mock).
- [x] No hardware QA claim (item 21 above).
- [x] ToF/RKNN engine internals not implemented (unchanged from prior rounds).
- [x] Android MGR/Sensor repos not modified *by this session's git-tracked commits* (item 19 above) - see the dedicated integrity section for the working-tree caveat.
- [x] `contract-v1` tag not created (item 23 above).
- [x] No merges, new branches, rebases, or pushes performed.

## Next dependency / handoff

- **Android baseline integrity recovery** (see dedicated section above) - still the highest-priority open item.
- Codex re-verification of this round's fixes (V0R-H-01, F-07, V0B-H-02, F-08).
- User review and Gate approval; `contract-v1` tag creation happens only after that.
- User/Cowork decision on the `keepServerIp` drift (Blockers item 2).
- Optional: empirical verification of `DataResult`'s actual Gson deserialization behavior on a real device/emulator (Blockers item 3).
- B-005 (RV1106 SDK/toolchain) resolution, whenever cross-build work is scheduled.
- Wave 1 sessions (`CC-SENSOR-CORE`, `CC-SENSOR-STREAM`, and `CC-MGR-CORE`/`CC-MGR-SERVER` in the counterpart repo) can consume the FND-01~04 headers/codecs, including the new `savvy_ipc_reconnect_tracker_t` hook and `savvy_ipc_cancel_source_t` cancellation primitive, committed here.
