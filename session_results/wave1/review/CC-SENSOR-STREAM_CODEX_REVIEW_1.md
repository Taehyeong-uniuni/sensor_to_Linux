# CODEX CC-SENSOR-STREAM WAVE 1 REVIEW 1

## 1. Target

- Session ID: `CC-SENSOR-STREAM`
- Branch: `feature/sensor-stream`
- Base SHA: `07809cb1f3f2b86a8e92ade661c48cb3adb97b52`
- Checkpoint SHA: `212010f5ab62571aa66e72b12b8eea0ed37df944`
- Final implementation SHA: `af2bcf79bf52de0b12f7948ccf6ed67eeae45c70`
- Review output: `session_results/wave1/review/CC-SENSOR-STREAM_CODEX_REVIEW_1.md`
- Exact-commit method: original worktree HEAD와 일치 여부를 확인한 뒤, `/tmp/cc-sensor-stream-review-1.z9fHNj/repo`의 detached clone에서 source inspection, build, test, sanitizer 및 link 검사를 수행했다. 원본 worktree에는 이 보고서 외 파일을 작성하지 않았다.

## 2. Verdict

- Verdict: **FAIL**
- Critical: 0
- High: 2
- Medium: 1
- Low: 0

필수 CTest ID, macOS plain/UBSan, Ubuntu 22.04 arm64 plain/UBSan은 모두 PASS했다. 그러나 실제 pinned server가 보내는 `{result:N}` 경로에서 Linux ASan heap-buffer-overflow가 재현되었고, public session API의 최대 payload 경계가 즉시 거절된다. 따라서 테스트 PASS만으로 DataResult 및 packet/send 동작을 PASS로 판정할 수 없다.

## 3. Provenance

- Original worktree: `/Users/juganghyeon/Desktop/uniuni/projects/worktrees/sensor-stream`
- Original HEAD / branch: `af2bcf79bf52de0b12f7948ccf6ed67eeae45c70` / `feature/sensor-stream` — PASS
- Original dirty status at start: 기존 다른 Review 파일 `session_results/wave1/review/CC-SENSOR-CORE_CODEX_REVIEW_1.md` 하나만 untracked였다. 수정하지 않았다.
- `contract-v1`: annotated tag, target `07809cb1f3f2b86a8e92ade661c48cb3adb97b52` — PASS
- Base, checkpoint, Foundation implementation (`aca143a7f4b76dc8cb6fff324ca21ea9f557622a`)은 모두 final SHA의 ancestor — PASS
- Contract manifest SHA-256: `a69536c286839c97e05ed7f54b5834d843f94eae4a9221ad6213de93d268fa6e` — PASS
- Dependency manifest SHA-256: `9934277d3a8d1dabd1c2632d3501743f8d2a57218c6dd6f3635b2b3844296ad2` — PASS
- Base..final changed files: 28개. Checkpoint..final: 5개 — Completion 보고와 일치.
- `git diff --check`: PASS. committed build artifact / symlink: 없음.
- Allowed path violation: 0. `contracts/**`, Foundation (`src/core/**`, `src/protocol/**`, `src/platform/interfaces/**`, `src/platform/linux/ipc/**`, `third_party/**`), root `CMakeLists.txt`, `CMakePresets.json`, `cmake/**`, root `SESSION_RESULT.md`, 다른 session path의 diff: 모두 0.
- Production mock linkage: 없음. Stream production target graph와 exported symbol에서 `mock_streaming_server`, mock/test/raw-fallback symbol을 찾지 못했다.

## 4. Android and Server Traceability

- SNS-01: pinned Android `ClientChannel`은 Stream/Voice별 `ClientChannel`을 사용하고 1000ms connect / 3000ms response wait을 사용한다. 구현은 session마다 별도 `sensor_tcp_channel_t`, worker, socket, queue, result policy를 생성한다. `SNS-STR-009`와 200회 lifecycle probe가 이를 뒷받침한다.
- SNS-02: Android `DEF.java`의 `S/I`, `S/S`, `S/Z`, `S/O`, `V/I`, `V/V`, `V/Z`, `V/O`, `T/S`를 `session.c`가 사용한다. pinned server `DeviceHandler`도 이 command 및 `T/S` handling을 수용한다. 새 wire command는 발견하지 못했다.
- SNS-03: Foundation `savvy_data_result_parse()` / `savvy_data_result_is_normal()`을 호출하고, missing/null/int32/string/fractional/duplicate matrix는 result-policy test에서 실행됐다. 다만 server의 unquoted result path는 Finding 001 때문에 안전하지 않다.
- SNS-04: Voice는 44-byte RIFF/WAVE PCM16 mono 8kHz wrapper 후 optional libbz2 compression을 사용한다. Android의 raw frame / voice buffer 기준 및 native BZip API 형태와 대조했다. system libbz2 link도 macOS와 Linux에서 확인했다.

## 5. Code Review

| Area | Result | Evidence |
|---|---|---|
| TCP lifecycle / queue | PASS | lazy connect, 1000ms connect / 3000ms response constants, non-blocking bounded enqueue, stop cancel/join, EINTR/partial I/O handling을 확인했다. |
| Stream·Voice isolation | PASS | instance-owned channel/policy와 TCP·session isolation test PASS. |
| Framing / stale response | PASS | Foundation packet/stream parser만 사용한다. response timeout/error에서 socket을 닫아 다음 request가 fresh connection에서 시작하며 `SNS-STR-010`이 실제 late response를 보낸다. |
| Packet / commands | PASS_WITH_HIGH_FINDING | 26-byte, BE length, 14-byte serial, CRC, S/V/T command mapping은 확인됐다. 그러나 session의 advertised maximum payload는 Finding 002를 가진다. |
| Result policy | FAIL | normal reset, stream threshold, voice immediate alert code는 맞지만 unquoted server response path가 Finding 001에 해당한다. |
| Normalization scope | PASS_WITH_HIGH_FINDING | `result`가 object 시작 직후 bareword key인 경우만 rewrite하며 `resultx`, other key, quoted/string 내부를 바꾸지 않는다. 그러나 rewrite buffer 종료 규약이 깨져 있다. |
| WAV | PASS | RIFF/WAVE/fmt/data, little-endian 44-byte header, PCM16 mono 8kHz, overflow/null/empty handling과 PCM preservation test PASS. |
| BZip | PASS | `find_package(BZip2 REQUIRED)`, `BZip2::BZip2`, no raw fallback, round-trip/high-entropy/larger-output/corrupt/empty/repetition test PASS. |
| Mock | MEDIUM FINDING | production target과 분리되어 있으나 standalone fixture는 CTest에 등록되지 않고 일부 fixture가 response가 아니라 request fragmentation을 만든다. Finding 003 참조. |
| Public API / ownership | PASS_WITH_HIGH_FINDING | deep-copy enqueue 및 callback lifetime 설명은 명확하다. 단 `max_payload_size`의 public contract와 allocation은 Finding 002를 가진다. |

Scope creep 검사에서 mic capture, decibel 계산, 45-frame aggregation, Smoke, ToF generator, baseline/RKNN engine, MGR IPC, application main wiring 또는 hardware adapter 구현은 발견하지 못했다.

## 6. Test Results

| Test ID | macOS plain / UBSan | Docker arm64 plain / UBSan | Independent result | Notes |
|---|---|---|---|---|
| CT-PKT-001 | PASS / PASS | PASS / PASS | PASS | `test_packet 001`, 34 assertions; encode/decode/golden/BE/CRC/length boundary. |
| CT-PKT-002 | PASS / PASS | PASS / PASS | PASS | `test_packet 002`, 35 assertions; partial header/body and coalesced parser behavior. |
| CT-PKT-003 | PASS / PASS | PASS / PASS | PASS | `test_packet 003`, 13 assertions; malformed length/stream bounds. |
| SNS-STR-001 | PASS / PASS | PASS / PASS | PASS | `test_session 001`, 10 assertions; Stream I→S→O, lazy connect, real production session API. |
| SNS-STR-002 | PASS / PASS | PASS / PASS | **FAIL under ASan** | `test_result_policy 001..006`, 57 assertions; plain/UBSan PASS지만 unquoted subtest `002`가 Linux ASan heap overflow. Finding 001. |
| SNS-STR-003 | PASS / PASS | PASS / PASS | PASS | `test_session 003` (8) 및 `003wav` (3); Voice flow, decibel packing, WAV. |
| SNS-STR-004 | PASS / PASS | PASS / PASS | PASS | `test_wav 001..004` 및 WAV/BZip round-trip; 67+6 assertions. |
| SNS-STR-005 | PASS / PASS | PASS / PASS | PASS | `test_bzip 001..006`, 47 assertions; round-trip/larger/corrupt/empty/repetition. |
| SNS-STR-006 | PASS / PASS | PASS / PASS | PASS | `test_tcp_channel 006`, 11 assertions; bounded queue reject/no eviction. |
| SNS-STR-007 | PASS / PASS | PASS / PASS | PASS | `test_tcp_channel 007`, 6 assertions; stop wake and idempotency. |
| SNS-STR-008 | PASS / PASS | PASS / PASS | PASS | `test_tcp_channel 008`, 2 assertions; 50 cycles. Separate Linux public-API probe executed 200 cycles. |
| SNS-STR-009 | PASS / PASS | PASS / PASS | PASS | TCP (4 assertions) and session isolation (2 assertions); one channel fail does not stop the other. |
| SNS-STR-010 | PASS / PASS | PASS / PASS | PASS | `test_tcp_channel 010`, 4 assertions; actual delayed first response, fresh second connection. |

모든 위 test는 placeholder가 아니라 Foundation 또는 production TCP/session/result/WAV/BZip code를 호출한다. 단 Finding 003의 standalone mock fixture 자체는 CTest에 연결되어 있지 않다.

## 7. Docker Environment

- Image: local `savvy-foundation-test:ubuntu22.04-arm64-v1` (새 `--rm`, `--network none` container)
- OS: Ubuntu 22.04.5 LTS (Jammy)
- Architecture / word size: `aarch64` / `64`
- CMake: 3.22.1
- Compiler: GCC 11.4.0
- Linker: GNU ld 2.38
- libbz2-dev: `1.0.8-5build1`, arm64
- Clean build elapsed: 36 seconds
- Plain: Foundation 9 + TCP 5 + result policy 6 + WAV 4 + BZip 6 + Stream 4 = **34/34 PASS**
- UBSan: 동일한 **34/34 PASS**
- Link evidence: `readelf -d test_bzip`은 `libbz2.so.1.0`을 NEEDED로 표시했고, `ldd`는 `/lib/aarch64-linux-gnu/libbz2.so.1.0`, link command는 `/usr/lib/aarch64-linux-gnu/libbz2.so`를 표시했다.
- Linux ASan: result-policy subtest `002`는 Finding 001의 heap-buffer-overflow로 FAIL했다.

## 8. Link and Symbol Audit

- macOS: `otool -L test_bzip`에서 `/usr/lib/libbz2.1.0.dylib` 확인.
- Linux arm64: system `libbz2.so.1.0` 확인.
- third-party BZip vendor나 다른 compression backend: 없음.
- `sensor_stream_session`, `sensor_tcp_8141`, `sensor_stream_result_policy`, `sensor_stream_compression` production archive에 mock/test/raw-fallback/dummy/stub symbol: 없음.
- Packet codec / parser: production implementation은 Foundation `savvy_packet_*`, `savvy_crc32`, `savvy_stream_parser_*`만 사용하며 중복 codec/parser는 없음.

## 9. Lifecycle

- `SNS-STR-007`: queue wait/response wait 중 stop, double start/stop PASS.
- `SNS-STR-008`: 50 create/start/submit/destroy cycles PASS.
- 독립 Linux public-API probe: 200 create/start/submit/response/stop/stop/destroy cycles 모두 completed. `/proc/self/fd` 4→4, `/proc/self/task` 1→1, server result 0.
- Stream-only failure 및 Voice-only healthy channel isolation: TCP/session `SNS-STR-009` PASS.
- timeout 뒤 stale response: `SNS-STR-010` PASS; first socket close 후 second request가 새 connection의 own response만 수신.
- destroy는 worker join 후 storage를 해제한다. callback 내부에서 같은 channel의 stop/destroy를 호출하지 않는 제약은 public header에 명시되어 있다.

## 10. Findings

### CDX-W1-SENSOR-STREAM-001

- Severity: **High**
- Title: unquoted `{result:N}` normalization이 NUL 종료 없는 buffer를 Foundation parser에 전달한다.
- Affected file: `src/features/result_policy/result_policy.c:100`, `src/features/result_policy/result_policy.c:166-167`; Foundation precondition at `src/protocol/json/json_codec.c:187`.
- Android / server / Foundation basis: pinned server `DeviceHandler`는 `{result:4}`, `{result:7}`, `{result: N}`을 전송한다. Foundation `savvy_json_parse_allow_duplicate_keys(text, len, ...)`는 `text[len] == '\0'`을 요구한다. normalizer는 `malloc(new_len)`만 한 뒤 `new_len` byte를 Foundation에 넘긴다.
- Reproduction command: Ubuntu 22.04 arm64 exact clone에서 `cmake -S src/features/result_policy -B /review/asan-result -DCMAKE_C_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer -g' -DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined`, build 후 `ASAN_OPTIONS=halt_on_error=1 /review/asan-result/_tests_result_policy/test_result_policy 002`.
- Observed: `{result:4}` 첫 처리에서 `json_codec.c:187`의 1-byte heap-buffer-overflow read. ASan은 normalizer의 12-byte allocation 바로 뒤 읽기라고 보고했다.
- Expected: body를 Foundation에 넘기기 전 `text[len]`이 항상 접근 가능한 NUL byte여야 하며, real server response가 sanitizer 오류 없이 normal/danger policy를 수행해야 한다.
- Impact: 정상 production server response만으로 process memory-safety failure 또는 결과 정책 미동작이 발생할 수 있다. plain/UBSan test의 PASS 주장은 이 경로의 메모리 안전성을 증명하지 못한다.
- Minimal fix scope: result-policy feature 안에서 raw response와 normalized response 모두에 `len + 1` NUL-terminated copy를 만들고 overflow/OOM을 처리한다. Normalization의 exact-key 범위는 유지한다.
- 수정 금지 범위: `src/protocol/**`, `contracts/**`, Foundation API, server wire format.
- Reverification: Linux ASan `test_result_policy 002`, full result-policy CTest, Stream/Voice integration `test_session 001/003`, full macOS/Docker regression.

### CDX-W1-SENSOR-STREAM-002

- Severity: **High**
- Title: `max_payload_size`와 같은 크기의 유효 raw payload를 session API가 전송 전에 거절한다.
- Affected file: `src/features/stream/include/sensor_stream/session.h:45`, `src/features/stream/session.c:289-297`, `src/features/stream/session.c:172-176`.
- Android / server / Foundation basis: pinned Android `DEF.java`는 `FRAMES_STREAM_SIZE=1843200`, `VOICE_STREAM_SIZE=1024*100`을 raw data buffer 크기로 사용하고 `PacketIfCommData`는 packet을 `26 + dataLength`로 할당한다. Foundation encoder도 header + body capacity를 요구한다. 현재 public field는 `max_payload_size`를 payload/reassembly sizing이라고 문서화하지만 session은 그 값만큼의 encode buffer와 transport max packet size를 사용한다.
- Reproduction command: exact Docker plain build에 link한 public-API harness에서 `config.max_payload_size=128`, raw `payload_len=128`, `compress=0`으로 Stream/Voice `sensor_stream_session_send_data()`를 호출.
- Observed: `stream create=0 send=10 overflow_constant=10`, `voice create=0 send=10 overflow_constant=10`; socket connect 이전에 `SAVVY_ERR_OVERFLOW`가 반환됐다.
- Expected: advertised maximum raw payload는 26-byte packet header를 포함해 전송 가능해야 하며 Voice에는 44-byte WAV header, compression enabled path에는 documented expansion bound도 반영돼야 한다.
- Impact: configured maximum 크기의 raw Stream frame 또는 Voice PCM이 실제 server에 전달되지 않는다. 특히 pinned Android-sized data를 그대로 caller-inject하는 Stage A integration에서 core Stream/Voice data path가 실패한다.
- Minimal fix scope: stream session config/implementation 안에서 raw input upper bound와 encoded packet capacity를 구분하고, header/WAV/compression expansion을 overflow-safe하게 반영한다. 해당 경계를 exercise하는 unit/integration test를 추가한다.
- 수정 금지 범위: Foundation packet codec, contracts, root CMake, unrelated session ownership.
- Reverification: raw Stream at boundary, raw Voice at boundary, compressed high-entropy boundary, `SNS-STR-001`, `SNS-STR-003`, full macOS/Docker regression.

### CDX-W1-SENSOR-STREAM-003

- Severity: **Medium**
- Title: standalone mock fixture가 CTest에 등록되지 않았고 partial/split fixture가 client 수신 response를 fragment하지 않는다.
- Affected file: `tools/mock_streaming_server/CMakeLists.txt:26-27`, `tools/mock_streaming_server/mock_streaming_server.c:187-200`.
- Basis: Review 요구는 test-only mock의 partial response, split body, delay/timeout/stale/disconnect/isolation behavior와 production code 검증을 요구한다. CMake는 executable만 만들며 `add_test()`가 없다. `partial-header`/`split-body`는 `read_one_packet()`의 request read style을 바꾼 뒤 full response를 한 번에 보낸다.
- Reproduction command: `ctest --test-dir /tmp/cc-sensor-stream-review-1.z9fHNj/macos-plain/mock -N`.
- Observed: `Total Tests: 0`. standalone fixture가 product TCP client를 대상으로 response fragmentation을 검증하지 않는다.
- Expected: fixture별 CTest 또는 integration harness가 mock target을 실행하고, mock이 실제 response header/body fragmentation 및 delay/stale behavior를 만들어 production TCP/session API의 outcome을 assert해야 한다.
- Impact: Foundation parser contract tests와 bespoke session mock은 PASS하지만, standalone mock의 advertised parity/fixture coverage는 독립적으로 증명되지 않는다.
- Minimal fix scope: `tools/mock_streaming_server/**`와 sensor-stream test paths에 fixture orchestration/assertion만 추가한다.
- Reverification: mock fixture CTests, `SNS-STR-001/003/006-010`, full macOS/Docker regression.

## 11. Confirmed Claims

- Base/checkpoint/Foundation ancestry, contract/dependency manifest hash, changed files 28개 및 checkpoint 이후 5개를 독립 확인했다.
- Foundation/contract/root CMake/root `SESSION_RESULT.md` diff 0, Allowed path violation 0, production mock link 0을 독립 확인했다.
- macOS feature suite plain 30/30 및 UBSan 30/30, Docker Ubuntu arm64 plain 34/34 및 UBSan 34/34을 새 build directory에서 실행했다.
- Docker system libbz2 linkage, no raw fallback, Stream/Voice isolation, queue overflow, stale-response reconnection, fd/thread 200-cycle probe를 독립 확인했다.
- Completion 문서의 RV1106 cross-build/board runtime 및 Hardware QA `NOT_PERFORMED` 표기는 유지돼야 한다.

## 12. Required Fix Scope

Blocking fix session required.

1. `src/features/result_policy/**`에서 Foundation length+NUL ownership precondition을 만족하도록 response normalization/input handoff를 수정한다.
2. `src/features/stream/**`에서 raw maximum payload와 encoded packet capacity를 명확히 분리하고 Stream/Voice/WAV/compression bound를 검증한다.
3. `tools/mock_streaming_server/**`와 허용 test paths에서 standalone fixture의 response-side parity를 CTest로 실행 가능하게 만든다.

Foundation, contracts, root CMake, 다른 session path는 수정 범위가 아니다.

## 13. Reverification Plan

- Finding 001: Linux ASan result-policy `002`가 clean PASS하고 real unquoted `{result:4}`, `{result: 7}`에 대해 alert/no-alert behavior를 확인한다.
- Finding 002: Stream/Voice raw boundary, Voice WAV boundary, high-entropy compressed boundary를 public API integration test로 추가한다.
- Finding 003: fixture별 response fragmentation/delay/stale/disconnect CTest를 product client와 연결한다.
- 이후 macOS plain/UBSan, Docker arm64 plain/UBSan 전체와 13 required IDs를 재실행한다.

## 14. Final Recommendation

- Fix session: **required**.
- 관리 상태 제안: `IMPLEMENTATION_FINISHED`에서 High finding remediation 및 independent re-review로 이동.
- 이 reviewer는 `MERGE_READY`를 선언하지 않는다.

## 15. Verification Boundary

- RV1106 cross-build: NOT_PERFORMED
- RV1106 board runtime: NOT_PERFORMED
- Hardware QA: NOT_PERFORMED
- Android 앱 및 streaming server 실행/배포: 범위 밖. pinned Git object source 대조만 수행했다.
- macOS ASan: NOT_PERFORMED. exact ASan build는 성공했으나 이 host의 ASan test process가 출력 없이 hang하여 중단했다. Linux arm64 ASan은 실행 가능했고 Finding 001을 검출했다.
