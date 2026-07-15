# CC-SENSOR-CORE Session Result

## Metadata

- SESSION_ID: `CC-SENSOR-CORE`
- branch: `feature/sensor-core`
- base tag: `contract-v1`
- base SHA: `07809cb1f3f2b86a8e92ade661c48cb3adb97b52`
- Foundation implementation SHA: `aca143a7f4b76dc8cb6fff324ca21ea9f557622a`
- implementation SHA: self-referential — this result file is committed together with the implementation in a single commit on `feature/sensor-core`; the resulting commit SHA (reported verbatim in this session's final G011 summary immediately after `git commit`) **is** the implementation SHA. No separate/earlier SHA exists.
- contract manifest hash: `a69536c286839c97e05ed7f54b5834d843f94eae4a9221ad6213de93d268fa6e` (verified match at G000)
- dependency manifest hash: `9934277d3a8d1dabd1c2632d3501743f8d2a57218c6dd6f3635b2b3844296ad2` (verified match at G000)

## Preflight

- worktree: `/Users/juganghyeon/Desktop/uniuni/projects/worktrees/sensor-core`
- branch: `feature/sensor-core` (confirmed via `git branch --show-current`)
- HEAD at kickoff: `07809cb1f3f2b86a8e92ade661c48cb3adb97b52`
- `contract-v1^{commit}`: `07809cb1f3f2b86a8e92ade661c48cb3adb97b52` (== HEAD)
- `contract-v1` object type: `tag` (`git cat-file -t contract-v1`)
- working tree 상태: clean at kickoff (`git status --short` showed only untracked `.omc/`, an OMC operational-state directory outside this session's scope — no tracked-file modifications)
- Foundation ancestor check: `git merge-base --is-ancestor aca143a7f4b76dc8cb6fff324ca21ea9f557622a HEAD` → true (Foundation implementation commit is an ancestor of the base)
- Foundation API 확인: every header listed in the session brief (`ipc_client.h`, `ipc_transport.h`, `ipc_cancel.h`, `ipc_envelope.h`, `ipc_action_catalog.h`, `ipc_reconnect.h`, `config_codec.h`, `device_codec.h`, `snapshot.h`, `lifecycle.h`, `clock.h`, `queue.h`, `error.h`) was opened and read directly from `include/savvy/**` before use; exact signatures matched what was assumed. No Foundation header/type/function mismatch found.
- build entry point 확인: root `CMakeLists.txt` defines a single, fixed `add_subdirectory()` list (`third_party/cJSON`, `src/core`, `src/protocol`, `src/platform/interfaces`, optionally `src/platform/linux/ipc`, optionally `tests/contract`) with **no** feature-auto-registration mechanism (no `GLOB` over `src/features/*`, no `find_package`/installed-config path for `savvy_*` targets). Confirmed via direct inspection of `CMakeLists.txt`, every `src/*/CMakeLists.txt`, and `CMakePresets.json`. `src/features/`, `tests/unit/`, `tests/integration/`, `tools/` did not exist anywhere in the repo (this worktree, the `feature/sensor-stream` sibling worktree, or the primary checkout) before this session. Consequence: every one of the 6 required feature modules is built as a genuinely independent standalone CMake subproject (its own `project()` call), never touching the root `CMakeLists.txt`.

## Android Evidence

All evidence below is read directly from the pinned Git objects (not the Android working trees): Sensor `savvy_sensor` @ `48e2d1442cd867cc60f8ff3186d53fce1c08f308`, MGR `savvy_mgr` @ `ad83cabebe7643e9eec5c0e75c1c797af30d357a`.

### SNC-01 (Lifecycle/cache)

- pinned source 파일·method:
  - `app/src/main/java/com/uniuni/savvysensor/MainActivity.java` `onCreate()` (lines 156-335): cached Config/Device load (`setJsonConfigDto()`/`setJsonDeviceDto(true)`, lines 214-215) runs strictly before any module init; `doBindIpcService()` (MGR bind) runs near the end (line 320), after stream/voice channels are already started.
  - `onDestroy()` (lines 362-373): unregisters network check, clears one-second timer messages, `doUnbindIpcService()` — never blocks waiting on any worker.
  - `onStop()` (lines 382-394): `System.exit(0)` + `killProcess` — a hard, unconditional terminal shutdown; there is no "stopped but resumable" state to preserve.
  - `setJsonDeviceDto(boolean isInited)` (lines 1892-1914): `isInited=true` (only from `onCreate`) zeroes exactly 9 fields (see Android Parity below); `isInited=false` (every runtime `actionDevice` call) never resets them.
- caller: `onCreate()` (Activity lifecycle, Android framework), `onDestroy()`/`onStop()` (Activity lifecycle).
- consumer: this session's `src/features/health` module (daemon lifecycle registrar + shutdown fan-out) and `src/features/config` module (cached-load entry points), as the C-side analogs of the same startup/shutdown sequencing — no real Stream/Voice/RKNN/Mic/ToF worker is created or consumed here (out of scope; those belong to CC-SENSOR-STREAM/INPUT/TOF).

### SNC-02 (Config·Device)

- pinned source 파일·method:
  - `MainActivity.actionConfig(Bundle)` (lines 2028-2111): full JSON save (`mSettings.edit().putString("jsonConfigDto", jsonData).apply()`), snapshots ~12 old scalar fields, full struct replace via `setJsonConfigDto()`, then diffs old vs. new to decide reactions — serverIp change restarts stream/voice channels; useRknn change (`if(preUseRknn != new)`) sets `IS_USE_RKNN` and restarts the UDS client; decibel/pixelCount/fracture-frame fields react but are owned by other sessions (ToF/mic); `compress` has no reaction at all.
  - `MainActivity.actionDevice(Bundle)` (lines 2112-2131): full JSON save, full struct replace via `setJsonDeviceDto(false)` (no field reset), then **unconditionally** re-derives `IS_DATA_COLLECTION` every call (no diff gate): `dataCollection==0 -> false, else -> true`.
  - `onCreate()` lines 272-280: startup derivation — `if(useRknn==0) IS_USE_RKNN=false` (else stays at the static default `true`, line 87); `if(dataCollection==1) IS_DATA_COLLECTION=true` (else stays at the static default `false`, line 90).
- caller: `IpcHandler.handleMessage()` (`ACTION_BROADCAST_CONFIG`/`ACTION_BROADCAST_DEVICE` dispatch, lines 2357-2360) at runtime; `onCreate()` at startup.
- consumer: this session's `src/features/config` module (JSON storage/selective-apply/snapshot publish) and `src/features/mode_state` module (useRknn/dataCollection raw→runtime derivation, split out as its own standalone feature per the allowed-paths list).

### SNC-03 (MGR IPC)

- pinned Sensor/MGR source:
  - Sensor `MainActivity.doBindIpcService()`/`mIpcConnection`/`IpcHandler`/`sendIpcMessage()` (lines 2339-2453): binds to the explicit component `com.uniuni.savvymgr/.MessengerService`; `onServiceConnected` unconditionally sends `CONNECT_BROADCAST_IPC` with no payload; `sendIpcMessage()` is guarded by `if(mIsIpcBound)` only — if not bound, it does nothing at all (no queue, no error) — the exact "pre-connect drop" contract.
  - Sensor `callBroadcastGetstateSensor()` (lines 1156-1188): 5-slot `mSensorState[]` cache (one per `SENSOR_TYPE`: MIC/LED/BUZZER/TOF/PIR per the source comment), strict equality gate — sends only if the new value differs from the cached value, updating the cache **unconditionally before** the send is even attempted (not conditioned on delivery success).
  - MGR `MessengerService.IncomingHandler`/`onBind` (lines 46-125): MGR is the IPC **server** (`onBind` returns its own `Messenger`'s binder); `CONNECT_BROADCAST_IPC` is the one action that also captures `gReplyTo` before delegating to `SavvyService.actionConnectIpc()`.
  - `08_BLOCKERS.md` B-006/`DEC-20260714-02` (Foundation-approved contract, not directly evidenced in Android since Android's Sensor never rebinds on disconnect at all): AF_UNIX `SOCK_SEQPACKET`, max app message 65,536 bytes, reconnect explicitly required at the Linux-port level with MGR resending Config/Device — a deliberate, approved port-level enhancement over Android's own no-rebind behavior.
- producer: MGR (`MessengerService`, out-of-repo `mgr_to_Linux` counterpart at the Linux-port level) sends `CONFIG`/`DEVICE`/etc.; Sensor (`MainActivity`/this session's `mgr_ipc`) sends `CONNECT_BROADCAST_IPC`/`GETSTATE`/`ALERT`/`UPLOAD`/`PROPERTY`/threshold-result.
- consumer: this session's `src/features/mgr_ipc` (connect/disconnect/reconnect/cancel-destroy/send/recv-dispatch state machine) and `src/features/state_report` (same-state suppression tracker, split out per the allowed-paths list).
- replay/drop 근거: pre-connect drop is Android's own `if(mIsIpcBound)` guard in `sendIpcMessage()`, reproduced exactly. Sensor never replays cached Config/Device to MGR on reconnect anywhere in the pinned source (Android has no reconnect logic to evidence this from at all) — consistent with the explicit instruction that Sensor, as IPC client, must not do so; only MGR (server) pushes Config→Device, per B-006.

### SNC-04 (Update/health)

- pinned source 파일·method:
  - `MainActivity.actionApkUpdate(Bundle)` (lines 2170-2173): the complete handler — `mIsApkUpdate = true;` and nothing else. Grepped the entire 2475-line file for `mIsApkUpdate`: exactly two reference sites exist (this assignment and the read below) — no reset anywhere.
  - `MainActivity.callToF_PirIn()` (lines 1350-1374): `if(mIsApkUpdate){ return; }` is the very first statement — once tripped, every subsequent PIR-in-triggered call returns immediately for the rest of the process's life.
- caller: `IpcHandler.handleMessage()` on `ACTION_BROADCAST_APK_UPDATE` (line 2373-2374).
- consumer: this session's `src/features/update_guard` (guard state + `should_allow_pir_in` query) and `src/features/health` (`sensor_health_snapshot`, combining lifecycle state with a caller-supplied guard flag) — actual PIR-in judgment and ToF worker remain CC-SENSOR-TOF's responsibility, not implemented here.

## Implementation

Stage A scope throughout (per `02_SESSION_MATRIX.md` §3 and `03_FILE_OWNERSHIP.md`): every feature below is pure functions / state machines / feature-public-API / unit tests, independently buildable and testable, with **no compile-time or runtime cross-feature linkage** — the typed interfaces below are the shapes a future integration session (Stage B / CC-INTEGRATION) wires together; nothing in this session calls from one `src/features/*` module into another.

**Why no feature auto-registration was found (re-verified at kickoff), and what was done instead**: every `src/*/CMakeLists.txt` under Foundation's own tree hardcodes `target_include_directories(<tgt> PUBLIC ${CMAKE_SOURCE_DIR}/include)`. `CMAKE_SOURCE_DIR` is fixed for an entire configure run to whatever directory `cmake -S` was pointed at — it does not "snap back" to the repo root just because a nested `add_subdirectory()` re-enters `src/core`/`src/protocol`. Configuring a feature standalone via `cmake -S src/features/<name>` would therefore make Foundation's own `add_subdirectory()`-based wiring resolve `${CMAKE_SOURCE_DIR}/include` to a nonexistent path. Each feature's `CMakeLists.txt` instead **vendors** the exact Foundation `.c` sources it needs as directly-listed sources in its own `add_library()` calls (with `target_include_directories` pointed explicitly at the real repo root computed via `${CMAKE_CURRENT_SOURCE_DIR}/../../..`, which — unlike `CMAKE_SOURCE_DIR` — correctly reflects each `CMakeLists.txt`'s own location regardless of nesting). This reads Foundation's frozen source but never modifies it, and never touches the forbidden root `CMakeLists.txt`/`cmake/**`. `third_party/cJSON` is pulled in via `add_subdirectory()` instead, since its own `CMakeLists.txt` uses `CMAKE_CURRENT_SOURCE_DIR` (safe under any nesting).

### `src/features/config` (SNC-02 primary)

- Public typed interface: `sensor_config_store_t`/`sensor_device_store_t` (each wrapping a `savvy_snapshot_owner_t` + a mutable `working` copy guarded by its own `pthread_mutex_t`).
  - `sensor_config_store_load_cached()` / `sensor_device_store_load_cached()`: startup path — empty cache → Foundation `set_defaults()`; non-empty → parse onto defaults (full replace, mirrors `gson.fromJson` always producing a fresh object). `device_store`'s version additionally zeroes the 9 stateful fields unconditionally after parsing (mirrors `setJsonDeviceDto(true)`).
  - `sensor_config_store_apply_runtime()` / `sensor_device_store_apply_runtime()`: runtime path — parses onto a **scratch copy** of the current value (never mutates `working` until parse + snapshot publish both succeed, so a rejected/malformed call is fully atomic — stronger than Foundation's own parse contract, which doesn't promise this itself). Returns a typed diff result: `sensor_config_apply_result_t` (`server_ip_changed`, `use_rknn_raw_changed` — this one mirrors Android's `preUseRknn != new` diff-gate exactly) and `sensor_device_apply_result_t` (`data_collection_raw_old/new`, unconditionally populated every call — no diff-gate, matching Android's asymmetry).
  - `sensor_config_store_acquire/release()` / device equivalents: immutable snapshot handles via Foundation's `savvy_snapshot_*`.
- snapshot ownership: each store owns exactly one `savvy_snapshot_owner_t`; writers clone `working` into a fresh heap payload before every publish (never hand the snapshot owner a pointer a concurrent reader might still be using).
- serverIp/useRknn: typed diff-result fields only — no worker reconfigure event bus, no immediate TCP connect (per instruction). `compress`/`decibel`/etc. are parsed, stored, and round-tripped faithfully but have no CC-SENSOR-CORE-owned reaction (their consumers are other Wave-1 sessions).

### `src/features/mode_state` (SNC-02 secondary — useRknn/dataCollection)

- Public typed interface (pure functions, zero Foundation dependency, zero shared state):
  - `sensor_mode_use_rknn_apply_startup(int32_t raw)`: `raw==0 -> false, else -> true`.
  - `sensor_mode_use_rknn_apply_live(int32_t old_raw, int32_t new_raw) -> sensor_mode_transition_t{changed, runtime_value}`: `changed = old!=new`; `runtime_value = (new==1)` — a two-argument, diff-gated shape, matching Android's `if(preUseRknn != new)` guard.
  - `sensor_mode_data_collection_apply_startup(int32_t raw)`: `raw==1 -> true` (exactly 1), `else -> false`.
  - `sensor_mode_data_collection_apply_live(int32_t raw)`: `raw==0 -> false, else -> true` — a one-argument, **always-evaluated** shape (no diff-gate parameter exists at all), making the useRknn-vs-dataCollection asymmetry impossible to accidentally mix up at the API level.

### `src/features/mgr_ipc` (SNC-03 primary)

- Public typed interface: opaque `sensor_mgr_ipc_client_t`, created via `sensor_mgr_ipc_client_create(config)` where `config.connector`/`connector_ctx` inject "how a transport is obtained" (dependency injection, not a production mock — see below).
  - `sensor_mgr_ipc_client_start()` / `_stop()`: idempotent; `_stop()` performs the 7-step cancel/destroy sequence (see below); a client can be restarted after `_stop()` (fresh cancel source each `start()`).
  - `sensor_mgr_ipc_client_send(action, payload_json)`: validates the action against Foundation's real catalog (`savvy_ipc_action_known`/`_direction`/`_validate_payload`), builds the envelope via `savvy_ipc_envelope_build` (which itself rejects an oversized payload with `SAVVY_ERR_OVERFLOW` before anything is sent), and — only if connected — calls the transport's `send()`. If not connected: returns `SAVVY_ERR_NOT_CONNECTED` **without ever touching the transport** — the pre-connect-drop contract.
  - Callbacks: `on_envelope(action, payload_json)` (fired only for parsed + catalog-valid + correct-direction MGR→Sensor envelopes — anything else is dropped silently per S-003), `on_connected(was_reconnect)`, `on_disconnected()`.
  - Internally uses Foundation's `savvy_ipc_reconnect_tracker_t` for its intended purpose (distinguishing first-connect from reconnect) but passes hook callbacks that only set a local boolean flag — **no cached Config/Device is ever sent to MGR** through this mechanism or any other path in this module, honoring the explicit no-replay-on-reconnect instruction.
- Concurrency model: one persistent background worker thread per client. Outer loop: connector (cancelable) → on success, send `CONNECT_BROADCAST_IPC` → inner loop: bounded-timeout `recv()` → dispatch → on peer EOF (`recv()==0`)/error/`SAVVY_ERR_OVERFLOW`(discarded, connection stays up)/shutdown, exit inner loop → close transport → back to outer loop (reconnect) unless shutdown requested.
- lifecycle/cancel ordering — the 7 steps, exactly as implemented in `sensor_mgr_ipc_client_stop()`:
  1. Block new cancel callers: a `lifecycle_lock`-guarded `stopped` flag — only the first caller proceeds; every later call (concurrent or sequential) is a safe no-op.
  2. Join any thread that could itself call cancel: this design has exactly one such caller (whoever calls `stop()`) — nothing further to join at this step.
  3. Execute cancel: `savvy_ipc_cancel_source_cancel()`, after `shutdown_requested` has already been set and a condvar broadcast issued.
  4. This wakes a blocked connector wait immediately (Foundation's `connect_cancelable` genuinely polls the cancel fd); a blocked `recv()` wakes within one `recv_poll_timeout_ms` slice, since Foundation's transport `recv()` takes no cancel source of its own — bounded polling is the only interruption mechanism available, and this is documented as such (not claimed to be instant).
  5. `pthread_join()` the single worker thread — the only waiter/worker in this design — before touching anything it might still be using.
  6. Defensive, idempotent transport close if still marked connected (the worker thread already closes its own transport on every exit path, so this is normally a no-op by the time it runs).
  7. `savvy_ipc_cancel_source_destroy()`, only now that the sole waiter has fully joined (step 5) — matches Foundation's documented precondition that `destroy()` must never race a live waiter.
- 실제 worker 미생성 근거: no Stream/Voice/RKNN/Mic worker exists anywhere in this module or its tests; the client only sends/receives typed envelopes and fires callbacks — what a future integration does with `on_envelope`'s payload is entirely out of this session's scope.
- Portability seam (not a test hook in the FG-M-01 sense): `savvy_ipc_cancel_source_*`'s real implementation lives in `src/platform/linux/ipc/ipc_transport_common.c`, compiled only as part of `savvy_platform_ipc`, which also requires `ipc_client.c` — confirmed via direct build attempt that `ipc_client.c` fails to compile on this macOS toolchain (`error: use of undeclared identifier 'SOCK_CLOEXEC'`, a Linux-only `socket()` flag). Since this session keeps the entire `src/platform/linux/ipc/` tree behind one gate (`SENSOR_MGR_IPC_REAL_TRANSPORT`, mirroring Foundation's own `SAVVY_IPC_REAL_TRANSPORT`) rather than cherry-picking, `src/features/mgr_ipc/cancel_source_portable.c` provides a byte-for-byte-equivalent reimplementation of just the four `pipe()`/`fcntl()`/`poll()`-based cancel-source functions declared in Foundation's own `include/savvy/platform/ipc_cancel.h`, compiled **only** when `SENSOR_MGR_IPC_REAL_TRANSPORT` is OFF (never alongside the real `savvy_platform_ipc`, which already defines the same symbols — avoiding any duplicate-symbol link error).
- `src/features/mgr_ipc/real_connector.c` (built only when `SENSOR_MGR_IPC_REAL_TRANSPORT=ON`): a thin wrapper calling Foundation's real `savvy_ipc_client_connect_cancelable()` — the production connector any real integration should use.

### `src/features/state_report` (SNC-03 secondary — dedup + typed report shapes)

- `sensor_state_report_tracker_t` (5 independent per-`sensor_report_sensor_type_t` slots: `MIC/LED/BUZZER/TOF/PIR`, matching the Android source comment's exact order): `sensor_state_report_should_send(tracker, type, new_value)` returns true (and unconditionally updates the cache) iff this is the type's first-ever report or the value differs — reproducing Android's naive "update the cache before the send is even attempted, regardless of delivery success" behavior exactly, including through a drop.
- Typed payload shapes only (no logic, no dedup — no Android evidence supports suppressing Property/Alert/Upload/Threshold the way GETSTATE is suppressed): `sensor_property_report_t`, `sensor_alert_report_t`, `sensor_upload_report_t`, `sensor_threshold_result_t`.

### `src/features/update_guard` (SNC-04 primary)

- `sensor_update_guard_t` (mutex-guarded bool): `sensor_update_guard_on_apk_update()` unconditionally sets tripped=true (idempotent across repeated broadcasts, matching Android's bare assignment); `sensor_update_guard_should_allow_pir_in()` returns `!tripped`, mirroring `callToF_PirIn()`'s first guard check exactly. **No reset function exists anywhere in this module, by design** — Android's `mIsApkUpdate` has none either.

### `src/features/health` (SNC-01 primary, plus SNC-04's "health interface")

- `sensor_lifecycle_t` layers a boot-time module registry on top of Foundation's `savvy_lifecycle_t` (idempotent STOPPED/RUNNING primitive, reused directly rather than reimplemented). `sensor_lifecycle_register_module()` appends a `{module_id, on_start, on_config_applied, on_shutdown, user_data}` hook set (registration order = notification order for every hook kind) — matching the `lifecycle_hook_register(module_id, on_start, on_config_applied, on_shutdown)` shape referenced in `session_tasks/CC-SENSOR-CORE.md`.
- `sensor_lifecycle_start()`/`_stop()`: idempotent (fire hooks only on the real STOPPED↔RUNNING transition, using Foundation's own `out_transitioned` signal — a redundant call fires nothing). `_stop()`'s shutdown fan-out is an explicit **nonblocking notification boundary**: each `on_shutdown` call is one synchronous function call the callee must return from promptly — no timeout is added, and this module never joins/waits on any other session's worker thread, per the explicit instruction against building a generic wait-for-everyone mechanism.
- `sensor_health_snapshot(lc, update_guard_tripped)`: a minimal read-only combine of `sensor_lifecycle_get_state()` and a caller-supplied guard flag (`update_guard`'s own state, injected rather than linked, per Stage-A) — satisfies "health·shutdown interface" without inventing new policy.
- No boot-type parameter was added anywhere: Android's `mStartBootType` only affects ToF stand-frame file reading (out of scope), never the Device stateful-field reset, which is unconditional regardless of boot type — adding an unused/guessed boot-type enum without further Android evidence was deliberately avoided.

### `tools/mock_mgr`

- A standalone AF_UNIX `SOCK_SEQPACKET` **server** (Foundation ships no server-role helper in this repo — MGR-as-server is `mgr_to_Linux`'s own concern) used only by `tests/integration/sensor_core/mgr_ipc` and manual Docker verification; never linked into any `src/features/**` target. For `<cycles>` connection cycles: accept, drain/log incoming messages, push one `CONFIG` then one `DEVICE` envelope (built via Foundation's real `savvy_ipc_envelope_build`), hold open `<hold_open_ms>`, close (forcing the client to detect disconnect and reconnect). Compiles on macOS but fails fast at runtime (`socket(): Protocol not supported`, i.e. `EPROTONOSUPPORT`) since Darwin doesn't support `SOCK_SEQPACKET` for `AF_UNIX` — confirmed by direct execution, matching why `mgr_ipc`'s real transport is Linux-only.

## Changed Files

```
src/features/config/CMakeLists.txt
src/features/config/config_store.c
src/features/config/config_store.h
src/features/config/device_store.c
src/features/config/device_store.h
src/features/health/CMakeLists.txt
src/features/health/sensor_health.c
src/features/health/sensor_health.h
src/features/health/sensor_lifecycle.c
src/features/health/sensor_lifecycle.h
src/features/mgr_ipc/CMakeLists.txt
src/features/mgr_ipc/cancel_source_portable.c
src/features/mgr_ipc/mgr_ipc_client.c
src/features/mgr_ipc/mgr_ipc_client.h
src/features/mgr_ipc/real_connector.c
src/features/mgr_ipc/real_connector.h
src/features/mode_state/CMakeLists.txt
src/features/mode_state/mode_state.c
src/features/mode_state/mode_state.h
src/features/state_report/CMakeLists.txt
src/features/state_report/state_report.c
src/features/state_report/state_report.h
src/features/update_guard/CMakeLists.txt
src/features/update_guard/update_guard.c
src/features/update_guard/update_guard.h
session_results/wave1/CC-SENSOR-CORE.md
tests/integration/sensor_core/mgr_ipc/test_real_transport_integration.c
tests/unit/sensor_core/config/test_config_store.c
tests/unit/sensor_core/health/test_lifecycle.c
tests/unit/sensor_core/mgr_ipc/fake_transport.c
tests/unit/sensor_core/mgr_ipc/fake_transport.h
tests/unit/sensor_core/mgr_ipc/test_mgr_ipc_client.c
tests/unit/sensor_core/mode_state/test_mode_state.c
tests/unit/sensor_core/state_report/test_state_report.c
tests/unit/sensor_core/update_guard/test_update_guard.c
tools/mock_mgr/CMakeLists.txt
tools/mock_mgr/mock_mgr.c
```

35 files, all newly created (this session's directories did not exist before kickoff); zero modifications to any pre-existing file.

## Tests

Build/test commands (identical pattern for macOS and Linux; Linux additionally ran the two commands under `-DSENSOR_MGR_IPC_REAL_TRANSPORT=ON -DMOCK_MGR_BINARY=<path>` for `mgr_ipc`, and `-DMOCK_MGR_BINARY` requires `tools/mock_mgr` built first):

```
cmake -S src/features/<name> -B build/<name> [options]
cmake --build build/<name>
ctest --test-dir build/<name> --output-on-failure
```

| Test ID | 환경 | 실제 명령/빌드 | 결과 | assertion | sanitizer | fd/thread 검사 |
|---|---|---|---|---|---|---|
| SNS-CORE-001 | macOS + Linux (config) | `build/config`, subtest `001` | PASS both | cached Device: 9 stateful fields (blueTooth/mic/wifi/tof/led/buzzer/moveSensor/beacon/reboot) reset to 0 after `sensor_device_store_load_cached()`; non-stateful fields (deviceSerial, dataCollection) preserved | ASan+UBSan PASS (Linux) | n/a (no threads) |
| SNS-CORE-001 (state_report half) | macOS + Linux (state_report) | `build/state_report`, subtest `001` | PASS both | fresh tracker treats first report of all 5 sensor types as not-suppressed; immediate repeat is suppressed | ASan+UBSan PASS (Linux) | n/a |
| SNS-CORE-002 | macOS + Linux (config) | `build/config`, subtest `002` | PASS both | serverIp/useRknn selectively reflected in diff result (changed flags + old/new); decibel/compress stored and round-tripped in the snapshot but produce no reaction; unrecognized extra JSON key tolerated (ignore+log) | ASan+UBSan PASS (Linux) | n/a |
| SNS-CORE-002a | macOS + Linux (mode_state) | `build/mode_state`, subtest `002a` | PASS both | useRknn startup: 0→false, 1/2/-1→true; live: (0,1)→changed+true, (1,1)→unchanged, (0,2)→changed+false, (0,0)→unchanged | ASan+UBSan PASS (Linux) | n/a |
| SNS-CORE-003 | macOS + Linux (mode_state) | `build/mode_state`, subtest `003-mode-state` | PASS both | dataCollection startup: exactly 1→true, 0/2/-1→false; live: 0→false, 1/2/-1→true (always evaluated) | ASan+UBSan PASS (Linux) | n/a |
| SNS-CORE-003a | macOS + Linux (mgr_ipc) | `build/mgr_ipc`, subtest `003a` | PASS both | `sensor_mgr_ipc_client_send()` before any connect returns `SAVVY_ERR_NOT_CONNECTED`; transport `send()` never invoked (none was ever created) | ASan+UBSan PASS (Linux); TSan attempted, see Verification Boundary | n/a (no transport created) |
| SNS-CORE-003b | macOS + Linux (state_report) | `build/state_report`, subtest `003b` | PASS both | same-state suppression: identical repeated value suppressed; new value sends; independent per-sensor-type slots; 0 is a valid first value | ASan+UBSan PASS (Linux) | n/a |
| SNS-CORE-003b (mgr_ipc angle) | macOS + Linux (mgr_ipc) | `build/mgr_ipc`, subtest `003b` | PASS both | repeated identical send attempts while disconnected both return `SAVVY_ERR_NOT_CONNECTED` uniformly, no crash/state corruption | ASan+UBSan PASS (Linux); TSan attempted, see Verification Boundary | n/a |
| CT-IPC-002 | macOS + Linux (mgr_ipc, fake transport) | `build/mgr_ipc`, `ct-ipc-002` | PASS both | connect→send CONNECT handshake→receive CONFIG then DEVICE (in order)→peer close→`recv()==0` detected→reconnect (was_reconnect=true)→receive latest CONFIG/DEVICE again | ASan+UBSan PASS (Linux); TSan attempted, see Verification Boundary | n/a (fake transport, in-process socketpair) |
| CT-IPC-002-real | **Linux only** (mgr_ipc, `SENSOR_MGR_IPC_REAL_TRANSPORT=ON`) | `build/linux/mgr_ipc_real`, `CT-IPC-002-real` (real `savvy_ipc_client_connect_cancelable`, real AF_UNIX `SOCK_SEQPACKET`, real `tools/mock_mgr` subprocess) | **PASS** (5.48s) | identical scenario to CT-IPC-002 but end-to-end over a genuine kernel `SOCK_SEQPACKET` socket pair | not run under sanitizer (real-transport variant not reconfigured with sanitizer flags — see Verification Boundary) | not separately measured (covered by SNS-CORE-006's dedicated check) |
| SNS-CORE-004 | macOS + Linux (update_guard) | `build/update_guard`, subtest `004` | PASS both | guard false→true on `on_apk_update()`; `should_allow_pir_in()` false afterward across repeated calls; repeated `on_apk_update()` calls safe; no reset function exists to call | ASan+UBSan PASS (Linux) | n/a |
| SNS-CORE-005 | macOS + Linux (config) | `build/config`, subtest `005` | PASS both | Config+Device: missing keys succeed (partial apply, others untouched); syntax-malformed/wrong-type/null-for-known-field/duplicate-key all rejected with state fully unchanged; unknown extra key tolerated; no hang/crash in any case | ASan+UBSan PASS (Linux) | n/a |
| SNS-CORE-006 | macOS + Linux (mgr_ipc) | `build/mgr_ipc`, subtest `006` (60 connect/disconnect cycles) | PASS both | `/dev/fd` open-fd count after 60 cycles is within +4 of the count after cycle 5 (no growth); all 60 disconnects detected | ASan+UBSan PASS (Linux) | fd count checked via `/dev/fd` listing (portable macOS+Linux), no growth observed |
| SNS-CORE-007 | macOS + Linux (health) | `build/health`, subtest `007` | PASS both | all registered modules (including one with no `on_start`) receive exactly one `on_shutdown` call each, in registration order; idempotent stop fires no duplicate hooks | ASan+UBSan PASS (Linux) | n/a (no blocking I/O in this module) |
| SNS-CORE-007 (mgr_ipc angle) | macOS + Linux (mgr_ipc) | `build/mgr_ipc`, subtest `007` | PASS both | Case A: `stop()` while blocked in a repeatedly-failing `connect()` (5s configured timeout) returns in <1s via cancel; Case B: `stop()` while blocked in `recv()` (150ms poll slice) returns in <1s; second `stop()` call is idempotent and near-instant | ASan+UBSan PASS (Linux); TSan attempted, see Verification Boundary | thread joins verified to complete (test would hang/timeout otherwise); no leaked fd (transport closed before `stop()` returns) |

All 11 required Test IDs (`SNS-CORE-001, 002, 002a, 003, 003a, 003b, 004, 005, 006, 007`, `CT-IPC-002`) are covered; several are additionally exercised from a second feature's own build where the original task table listed dual execution targets (`SNS-CORE-001`: config + state_report; `SNS-CORE-003b`, `SNS-CORE-007`: mgr_ipc + state_report/health respectively) — both halves are listed above. `SNS-CORE-003`/`SNS-CORE-002a` are implemented and tested under `mode_state` rather than `config` (the task table's abbreviated label): this session's architecture splits Config/Device JSON storage (`config`) from the useRknn/dataCollection mode-derivation state machine (`mode_state`) since they are independently reusable and the allowed-paths list itself names `mode_state` as a separate directory.

Full consolidated run (all 6 feature builds, macOS, single session): 18/18 individual ctest cases passed, 0 failed.
Full consolidated run (all 6 feature builds + real-transport `mgr_ipc` variant, Linux Docker `savvy-foundation-test:ubuntu22.04-arm64-v1`, `aarch64`, gcc 11.4.0, cmake 3.22.1): 19/19 individual ctest cases passed (the real-transport build adds `CT-IPC-002-real` on top of the same 5 fake-transport cases), 0 failed.

## Android Parity

- cached Device reset: exactly 9 fields — `blue_tooth, mic, wifi, tof, led, buzzer, move_sensor, reboot, beacon` (Foundation's C field names; Android: `blueTooth, mic, wifi, tof, led, buzzer, moveSensor, reboot, beacon`) — zeroed only at startup load (`isInited`-equivalent path), never at runtime apply. Verified by `SNS-CORE-001`.
- Config selective apply: `serverIp` and `useRknn` are this session's two owned reactions (typed diff result only, no auto-connect); all other Config fields are stored/round-tripped but produce no CORE-owned reaction. Verified by `SNS-CORE-002`.
- useRknn startup/live: startup `0→false, else→true`; live is diff-gated (`old!=new`) and only then `1→true, else→false` — asymmetric from dataCollection by design, reproduced via a 2-argument vs. 1-argument function signature so the two cannot be conflated. Verified by `SNS-CORE-002a`.
- dataCollection startup/live: startup exactly `1→true, else→false`; live is **not** diff-gated — every apply re-evaluates `0→false, else→true`. Verified by `SNS-CORE-003`.
- pre-connect drop: `sensor_mgr_ipc_client_send()` returns `SAVVY_ERR_NOT_CONNECTED` without ever calling the transport's `send()` when not connected — no queue, no retry, no durable store. Verified by `SNS-CORE-003a`.
- 동일 상태 재보고 억제: per-sensor-type (5 slots) equality gate, cache updated unconditionally before/regardless of send outcome — verified independently in `state_report` (`SNS-CORE-003b`) and demonstrated as safe-to-repeat at the transport layer in `mgr_ipc` (`SNS-CORE-003b` mgr_ipc angle).
- reconnect Config→Device: `CT-IPC-002` (fake transport) and `CT-IPC-002-real` (genuine AF_UNIX `SOCK_SEQPACKET` + `tools/mock_mgr`) both verify connect → CONNECT handshake → receive CONFIG then DEVICE → peer close → `recv()==0` detected → automatic reconnect (`was_reconnect=true`) → receive latest CONFIG/DEVICE again — with no cached-Config/Device replay ever sent from Sensor to MGR (grepped: no such code path exists anywhere in `mgr_ipc`).
- update guard: `mIsApkUpdate`-equivalent flag, settable only to true, no reset function anywhere in the module (by design, matching the pinned source's own absence of a reset site). Verified by `SNS-CORE-004`.

## Scope Verification

- Allowed path violations: **0**. `git diff --name-only 07809cb1f3f2b86a8e92ade661c48cb3adb97b52...HEAD` (post-commit) and `git ls-files --others --exclude-standard` (pre-commit) both list only files under `src/features/config/**`, `src/features/health/**`, `src/features/mgr_ipc/**`, `src/features/mode_state/**`, `src/features/state_report/**`, `src/features/update_guard/**`, `tests/unit/sensor_core/**`, `tests/integration/sensor_core/**`, `tools/mock_mgr/**`, and `session_results/wave1/CC-SENSOR-CORE.md`.
- contract/Foundation changes: 0. `contracts/**`, `src/core/**`, `src/protocol/**`, `src/platform/interfaces/**`, `src/platform/linux/ipc/**`, `third_party/**` are all absent from the diff — every one of this session's 6 CMakeLists.txt vendors these files' *content* by reference (reading, never writing) as documented above.
- root CMake changes: 0. Root `CMakeLists.txt`, `CMakePresets.json`, `cmake/**` are absent from the diff.
- root SESSION_RESULT.md changes: 0. Absent from the diff (read-only reference, never opened for writing).
- other-session path changes: 0. `src/features/stream/**`, `src/features/compression/**`, `src/features/result_policy/**`, `src/features/wav/**`, `src/features/voice/**`, `src/features/smoke/**`, `src/features/tof_integration/**`, `src/features/baseline/**`, `src/features/rknn_bridge/**`, `src/platform/linux/tcp_8141/**`, `src/app/**`, `src/platform/linux/common/**`, `tests/verification/**`, `tools/verification/**`, `tests/integration/system/**`, `scripts/**`, `mgr_to_Linux/**` are all absent from the diff.
- new command/field/retry/timeout: 0 new IPC actions, 0 new JSON fields (only the pre-existing, Foundation-cataloged action set is used); no new retry policy (reconnect uses a plain fixed backoff already implied by the approved B-006 contract, no new Alert/notification policy invented); the only "timeouts" introduced (`connect_timeout_ms`, `recv_poll_timeout_ms`, `reconnect_backoff_ms`, `send_timeout_ms`) are plain function parameters of this session's own client object controlling I/O wait bounds, not a new product-level policy/command.
- production mock/dummy: 0. `tests/unit/sensor_core/mgr_ipc/fake_transport.*` and `tools/mock_mgr` are both strictly test/tool code — `fake_transport.*` lives under `tests/` and is never referenced by anything under `src/features/**`; `mock_mgr` is a standalone executable under `tools/`, never linked into any feature library. `src/features/mgr_ipc`'s dependency-injection connector seam always uses the real Foundation connector (`real_connector.c`) in any build with `SENSOR_MGR_IPC_REAL_TRANSPORT=ON`; the injection point itself contains no test-only branch.
- actual Stream/Voice/RKNN/Mic/ToF worker: 0. No such worker exists anywhere in this session's code; `src/features/health`'s lifecycle hooks and `src/features/mode_state`'s derived booleans are typed interfaces only, consumed by nothing in this session.

## Verification Boundary

- RV1106_CROSS_BUILD: `NOT_PERFORMED`
- RV1106_BOARD_RUNTIME: `NOT_PERFORMED`
- HARDWARE_QA: `NOT_PERFORMED`
- macOS sanitizer builds: **attempted, not obtained** — a minimal 2-line pthread `create`+`join`+`printf` reproduction (no code from this session) was tested directly on this host (macOS Darwin 25.5.0, Apple clang 17.0.0, arm64) under three configurations: `-fsanitize=thread` (segfaults immediately, zero sanitizer output, exit 139), `-fsanitize=address,undefined` (hangs indefinitely, killed after 5s of 100% CPU spin), `-fsanitize=address` alone (also hangs, killed after 5s). All three confirm a pre-existing host/toolchain-level incompatibility between this specific environment and sanitizer+pthread instrumentation, independent of anything written in this session — this is not written up as a pass or a code defect, simply not obtainable here. Non-sanitized macOS builds of all 6 features passed all their tests cleanly (see Tests table).
- Linux Docker sanitizer builds: ASan+UBSan **PASS** for all 6 features (`config`, `health`, `mgr_ipc` fake-transport, `state_report`, `update_guard`, `mode_state`) — 0 memory errors, 0 UB findings, all assertions still hold under instrumentation. ThreadSanitizer was attempted on `mgr_ipc` (the one feature with real threading) but failed at runtime with `FATAL: ThreadSanitizer CHECK failed ... personality(...) != -1` — a well-known Docker-vs-TSan interaction (TSan requires calling `personality(ADDR_NO_RANDOMIZE)`, which Docker's default seccomp profile blocks). The standard workaround (`docker run --security-opt seccomp=unconfined`) was attempted once and was denied by this environment's own tool-use security policy as a security-weakening action; per this session's operating instructions to act carefully around anything that weakens security controls, no further attempt was made to route around that denial (e.g. via `--cap-add`, a custom seccomp profile, or a privileged container). TSan coverage for `mgr_ipc` is therefore honestly recorded as **not obtained**, not as a failure of the code itself — ASan+UBSan (which do not require `personality()`) already exercised the same concurrent code paths (cancel/destroy sequencing, repeated connect/disconnect, blocked-recv-shutdown-wakeup) cleanly, and `CT-IPC-002-real` additionally validated the same logic end-to-end against a genuine kernel `AF_UNIX SOCK_SEQPACKET` socket pair (non-sanitized).
- Docker environment used: existing local image `savvy-foundation-test:ubuntu22.04-arm64-v1` (`sha256:73c8a9709607d1910231efb4648510e4d72052072629901fa28fd5c9f39753e7`), the same image recorded in `FOUNDATION_GATE_APPROVAL.md` as the Foundation Gate's own verified Docker environment (Ubuntu 22.04.5 LTS, aarch64, gcc 11.4.0, cmake 3.22.1) — reused as-is via `docker run`, never modified (`docker run --rm --platform linux/arm64 -v <worktree>:/workspace -w /workspace savvy-foundation-test:ubuntu22.04-arm64-v1 bash -c '...'`, exact commands recorded in this session's transcript).

## Blockers

- blocking: none.
- non-blocking:
  - TSan coverage for `mgr_ipc` not obtained in this Docker environment (seccomp/`personality()` restriction, security-policy-respecting decision not to force it) — ASan+UBSan plus a real-AF_UNIX-SOCK_SEQPACKET integration test (`CT-IPC-002-real`) already provide strong evidence for the same code paths.
  - macOS sanitizer coverage not obtained for any feature (confirmed pre-existing host/toolchain limitation, unrelated to this session's code).
  - `08_BLOCKERS.md` B-011 (path mapping for config/log/OTA/baseline/runtime socket location) remains `DEFERRED` at the project level — this session's `sensor_mgr_ipc_config_t`/`sensor_config_store_*`/`sensor_device_store_*` APIs all take paths/JSON text as explicit parameters (never hardcoded), so no new hardcoding was introduced; the actual production path values remain a future integration decision, unaffected by this session.
- next dependency: Stage B cross-feature wiring (real `lifecycle_hook_register` call sites for other sessions' workers, `mgr_ipc`'s `on_envelope` dispatch feeding `config`/`update_guard`/`state_report`, root `src/app/main.c` wiring, actual production socket-path selection) is CC-INTEGRATION's responsibility, not this session's.

## Final State

- IMPLEMENTATION_FINISHED
- AWAITING_CODEX_REVIEW
