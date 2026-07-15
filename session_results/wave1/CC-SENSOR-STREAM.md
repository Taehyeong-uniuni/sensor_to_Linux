# CC-SENSOR-STREAM — Wave 1 Checkpoint

**STATE: IMPLEMENTATION_PARTIAL_CHECKPOINT**

This is a checkpoint, not the final session result. It was created because
the user interrupted an in-progress Docker verification step to check
session API cost, and asked to stop starting new goals and safely
checkpoint work completed so far. No new goals were started after the
interrupt; this file and the checkpoint commit are the only actions taken
in response to that request.

## Session identity

- SESSION_ID: CC-SENSOR-STREAM
- Repository: sensor_to_Linux
- Worktree: /Users/juganghyeon/Desktop/uniuni/projects/worktrees/sensor-stream
- Branch: feature/sensor-stream (existing branch, no new branch created)
- Base / starting HEAD: `07809cb1f3f2b86a8e92ade661c48cb3adb97b52` (verified at G000 Preflight: matches `contract-v1` exactly, annotated tag, ancestor-confirmed)
- Foundation implementation SHA (this session): `aca143a7f4b76dc8cb6fff324ca21ea9f557622a` — confirmed present and an ancestor of HEAD at Preflight.

## Goal status (G001–G010, per the registered ultragoal plan `CC-SENSOR-STREAM`)

| Goal | Status | Notes |
|---|---|---|
| G000 Preflight | DONE | All 13 items passed (see conversation; not repeated here since this file is Allowed-path scoped to this session's own result doc) |
| G001-SNS01 (TCP 8141 channel lifecycle) | **DONE** | `src/platform/linux/tcp_8141/`. All 5 SNS-STR tests specific to the transport pass (006/007/008/009/010), plain + UBSan. |
| G002-SNS02 (wire commands) | **DONE** | `src/features/stream/session.c`. Integration-tested via SNS-STR-001/003, plain + UBSan. |
| G003-SNS03 (result policy + DataResult) | **DONE** | `src/features/result_policy/`. Includes the user-approved narrow JSON-normalization bridge for the real server's unquoted-key `{result:N}` wire format (see "Key finding" below). 6/6 tests pass, plain + UBSan. |
| G004-SNS04 (WAV/BZip) | **DONE** | `src/features/wav/`, `src/features/compression/`. Implemented by a parallel subagent against a precise spec; independently rebuilt and re-tested by me. 4/4 WAV tests, 6/6 BZip tests pass. |
| G005-MOCK (mock streaming server) | **DONE** | `tools/mock_streaming_server/`. ~20 selectable fixtures; 4 spot-checked directly against Foundation's real `savvy_packet_decode`/`savvy_crc32` (I-echo, danger-result unquoted body, CRC-mismatch injection, length-overflow rejection) — all behaved as intended. Not linked into any production target. |
| G006-TESTS (13 required test IDs) | **DONE** | All 13 present and passing: CT-PKT-001/002/003 (Foundation's own pre-existing `tests/contract` suite, unmodified, re-run and confirmed passing — 5/5 including CT-JSON); SNS-STR-001 through SNS-STR-010, all passing (see table below). |
| G007-BUILD (macOS + Docker) | **PARTIAL** | macOS host: done, see below. **Docker Ubuntu 22.04 arm64: not executed** — a verification script was written and the arm64 `ubuntu:22.04` image was pulled, but the actual `docker run` verification pass was interrupted by the user before it started running and was not retried. This is the single largest remaining gap. |
| G008-OWNERSHIP (self-check) | **PARTIAL** | Core check done as part of this checkpoint (see below): all 27 changed files fall within Allowed paths, zero forbidden-path diffs. The full, final 21-point enumerated pass (explicit `git diff contract-v1...HEAD` content review, explicit confirmation of zero new commands/fields/retries/timeouts as a closing statement) was not performed as a separate formal step. |
| G009-RESULT (this file) | **PARTIAL** | This is the checkpoint version. The full 41-item final version depends on G007's Docker results and G008's full pass, neither of which is done yet. |
| G010-COMMIT | **PARTIAL** | This checkpoint commit is being created now, but it is explicitly a checkpoint, not the final G010 commit — no `MERGE_READY`/`CODEX_VERIFIED` claim is made, and completion is not claimed. |

## Key finding worth preserving across the checkpoint

**Discovered wire-compatibility gap (user-approved fix already implemented):**
the real, pinned `streaming_server_v2` (commit `39a6f49343e38ff8b62bb3d1ab7233065d593d4a`) emits DataResult response bodies as unquoted-key JSON — literally `{result:4}` / `{result:7}`, via raw Java string concatenation (`DeviceHandler.java`) — which is not valid strict JSON. Foundation's `savvy_data_result_parse()` (built on strict-JSON cJSON, and correctly implementing the empirically-verified Gson-parity matrix in `contracts/json_field_policy.md` §4, which was tested only against quoted-key JSON) rejects this with `SAVVY_ERR_PROTOCOL` — confirmed by compiling and running a standalone probe against the real Foundation codec. Left unaddressed, this would make Stream/Voice danger detection silently non-functional against the real production server. The user was asked and chose: add a narrow, mechanical, byte-level normalization inside `src/features/result_policy/result_policy.c` (inserts quotes around an *exact* bareword `result` key immediately after `{`, nothing else) before calling the unmodified `savvy_data_result_parse()`. This is implemented, documented in-code with the full evidence trail, and covered by `SNS-STR-002-unquoted-wire-quirk` (passing).

**Deliberate divergence from Android worth flagging for the final result file:** the pinned Android `runPirErr()` closes *both* Stream and Voice channels when *either* one times out/disconnects/errors (`MainActivity.java` ~1424-1454). This session's implementation deliberately does **not** replicate that coupling — required behaviors #12/#13 in this session's own spec explicitly mandate that a Stream failure must not terminate Voice and vice versa, which is what was implemented and is what `SNS-STR-009` tests. This is an intentional, spec-directed improvement over the pinned mobile app's combined-error-state UX design, not an oversight.

## Tests run and results (macOS host, this checkpoint)

All of the following were built and run in this same working session, most recently just before the interrupt (not stale):

| Suite | Command | Result |
|---|---|---|
| Foundation contract (CT-PKT-001/002/003, CT-JSON-001/002) | `cmake -S . -B build/foundation_check && ctest` | 5/5 passed |
| tcp_8141 (SNS-STR-006/007/008/009/010) | `cmake -S src/platform/linux/tcp_8141 -B build/tcp_8141 && ctest` | 5/5 passed |
| tcp_8141 (same, UBSan) | same + `-fsanitize=undefined -fno-sanitize-recover=undefined` | 5/5 passed, no diagnostics |
| result_policy (SNS-STR-002 ×6) | `cmake -S src/features/result_policy -B build/result_policy && ctest` | 6/6 passed |
| result_policy (same, UBSan) | same + UBSan flags | 6/6 passed, no diagnostics |
| stream (SNS-STR-001/003/003wav/009) | `cmake -S src/features/stream -B build/stream && ctest` | 4/4 passed |
| stream (same, UBSan) | same + UBSan flags | 4/4 passed, no diagnostics |
| wav (SNS-STR-004 WAV half ×4) | `cmake -S src/features/wav -B build/wav && ctest` | 4/4 passed |
| compression (SNS-STR-004/005 BZip half ×6) | `cmake -S src/features/compression -B build/compression && ctest` | 6/6 passed |
| mock_streaming_server (manual spot-check, 4 fixtures) | ad hoc probe client against `s-i-echo`, `s-r-danger`, `invalid-crc`, `length-overflow` | all behaved as designed |

**ASan:** attempted on `tcp_8141`; the sanitizer runtime itself hangs during shadow-memory initialization before `main()` runs, reproduced identically with a trivial unrelated one-malloc program — a pre-existing macOS/Xcode AppleClang ASan issue on this host, not a defect in this session's code (also independently hit and diagnosed the same way by the parallel WAV/BZip subagent). `NOT_AVAILABLE`, not attempted further.

**Docker Ubuntu 22.04 arm64:** `NOT_PERFORMED` this checkpoint — image pulled (`ubuntu:22.04`, arm64, confirmed native/no emulation needed since the Docker daemon itself reports `aarch64`), a verification script was staged, but the actual container run was interrupted before executing.

**RV1106 cross-build / hardware:** `RV1106_CROSS_BUILD=NOT_PERFORMED`, `RV1106_BOARD_RUNTIME=NOT_PERFORMED`, `HARDWARE_QA=NOT_PERFORMED` (no toolchain available; consistent with the rest of Wave 1).

## Known errors / blockers

- None blocking correctness. The only real gap is the un-executed Docker Ubuntu 22.04 arm64 pass (G007) and the not-yet-finalized formal G008/G009/G010 closing steps.
- Two real bugs were found and fixed *during* this session's own test-writing (not production-code bugs): a stack-overflow in a test harness helper (1MB array on a pthread's default ~512KB stack — fixed by heap-allocating), and a test assumption mismatch (assumed one TCP connection per command; the actual — correct — implementation reuses one connection across a whole PIRIN→data→PIROUT sequence, matching the pinned Android `ClientChannel` contract, so the test's mock server and expectations were corrected instead of the implementation).

## Changed files (27, all within Allowed paths — verified via `git status --porcelain` against the session's Allowed-paths list)

```
src/features/compression/CMakeLists.txt
src/features/compression/bzip_codec.c
src/features/compression/include/sensor_stream/bzip.h
src/features/compression/tests/CMakeLists.txt
src/features/compression/tests/test_bzip.c
src/features/result_policy/CMakeLists.txt
src/features/result_policy/include/sensor_stream/result_policy.h
src/features/result_policy/result_policy.c
src/features/stream/CMakeLists.txt
src/features/stream/include/sensor_stream/session.h
src/features/stream/session.c
src/features/wav/CMakeLists.txt
src/features/wav/include/sensor_stream/wav.h
src/features/wav/tests/CMakeLists.txt
src/features/wav/tests/test_wav.c
src/features/wav/wav_encoder.c
src/platform/linux/tcp_8141/CMakeLists.txt
src/platform/linux/tcp_8141/include/sensor_platform/tcp_channel.h
src/platform/linux/tcp_8141/tcp_channel.c
tests/integration/sensor_stream/session/CMakeLists.txt
tests/integration/sensor_stream/session/test_session.c
tests/unit/sensor_stream/result_policy/CMakeLists.txt
tests/unit/sensor_stream/result_policy/test_result_policy.c
tests/unit/sensor_stream/tcp_channel/CMakeLists.txt
tests/unit/sensor_stream/tcp_channel/test_tcp_channel.c
tools/mock_streaming_server/CMakeLists.txt
tools/mock_streaming_server/mock_streaming_server.c
```
(plus this file, `session_results/wave1/CC-SENSOR-STREAM.md`, itself)

**Allowed-path violations: 0.** No forbidden path was touched (contracts/, src/core/, src/protocol/, src/platform/interfaces/, src/platform/linux/ipc/, third_party/, src/app/, src/platform/linux/common/, root CMakeLists.txt/CMakePresets.json/cmake/, tests/verification/, tools/verification/, other sessions' feature directories, tests/integration/system/, scripts/, repo root SESSION_RESULT.md, other repos/worktrees — none appear in the changed-files list above). `.omc/` and `build/` are untracked local tooling/build state, deliberately excluded from staging.

**CROSS_SESSION_DEPENDENCY** (unchanged since implementation): CC-SENSOR-CORE's public Config/Device snapshot and event API does not exist yet on this branch. Stage A uses feature-local injection ports (`sensor_stream_config_t` passed at session-create time: `server_ip`/`server_port`/`compress`/`danger_count_threshold`/`device_serial`) and test-only mocks, per the session brief. Production wiring to a real Config/Device snapshot and Alert/event surface is deferred to integration/Stage B.

## Exact next resume point

1. Re-run the (already-written, already-tested via `docker pull`) Docker Ubuntu 22.04 arm64 verification: the script at the time of interrupt built each of `src/platform/linux/tcp_8141`, `src/features/{result_policy,wav,compression,stream}`, and `tools/mock_streaming_server` standalone inside the container (installing `build-essential cmake libbz2-dev` first), plus the root Foundation contract suite, then ran `ctest` for each. None of this had executed yet when interrupted — start there.
2. Complete G008's formal closing pass: explicit `git diff --name-only contract-v1...HEAD` re-check, explicit zero-diff confirmation for each of the 21 enumerated categories.
3. Expand this file from the checkpoint version to the full 41-item final `CC-SENSOR-STREAM.md` (folding in the Docker results from step 1).
4. Do the single G010 completion-condition check (not a loop) and create the **final** implementation commit (this checkpoint commit is not that commit) with final-format Korean reporting, still without merge/tag/push and without ever declaring `MERGE_READY`/`CODEX_VERIFIED`.
