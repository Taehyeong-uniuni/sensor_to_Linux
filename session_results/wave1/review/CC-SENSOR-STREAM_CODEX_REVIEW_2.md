# CODEX CC-SENSOR-STREAM WAVE 1 REVIEW_2 1

## 1. Target and Exact SHA Ledger

- Session ID: `CC-SENSOR-STREAM`
- Review round: `REVIEW_2`
- Repository/worktree: `sensor_to_Linux` / `/Users/juganghyeon/Desktop/uniuni/projects/worktrees/sensor-stream`
- Expected/actual branch: `feature/sensor-stream` / `feature/sensor-stream` — PASS
- Exact target 검증 방법: 원본 worktree에서 provenance와 dirty-state guard를 확인한 뒤, `/tmp/cc-sensor-stream-review2-1.Tql3Z6/repo`에 `--no-local` clone을 만들고 `fd0d0eaad50595e34d869a96afb8a662f62e07e8`을 detached checkout했다. 모든 source inspection, build, test, sanitizer, link 검증은 이 clone 또는 `/tmp` build directory에서 수행했다.

```text
CONTRACT_TAG_TARGET_SHA:
07809cb1f3f2b86a8e92ade661c48cb3adb97b52

FOUNDATION_IMPLEMENTATION_SHA:
aca143a7f4b76dc8cb6fff324ca21ea9f557622a

CHECKPOINT_SHA:
212010f5ab62571aa66e72b12b8eea0ed37df944

ORIGINAL_IMPLEMENTATION_SHA:
af2bcf79bf52de0b12f7948ccf6ed67eeae45c70

CODEX_REVIEW_1_TARGET_SHA:
af2bcf79bf52de0b12f7948ccf6ed67eeae45c70

FIX_IMPLEMENTATION_SHA:
5fecfcb15b11f5d93562f4cec2d0ae713acc6674

FIX_REPORT_SHA:
fd0d0eaad50595e34d869a96afb8a662f62e07e8

REVIEW_2_TARGET_SHA:
fd0d0eaad50595e34d869a96afb8a662f62e07e8
```

`FIX_REPORT_SHA == REVIEW_2_TARGET_SHA`이며 추가 구현 commit은 없다. `contract-v1`, checkpoint, Foundation implementation, original implementation, Fix implementation은 모두 Review 2 target의 ancestor로 확인됐다.

## 2. Verdict

- Verdict: **PASS**
- `CDX-W1-SENSOR-STREAM-001`: **RESOLVED**
- `CDX-W1-SENSOR-STREAM-002`: **RESOLVED**
- `CDX-W1-SENSOR-STREAM-003`: **RESOLVED**
- Critical: 0
- High: 0
- Medium: 0
- Low: 0
- Required 13 IDs: PASS
- macOS plain/UBSan: PASS
- Ubuntu 22.04 arm64 Docker plain/UBSan: PASS
- Finding-specific ASan+UBSan: PASS
- Allowed path violation: 0
- Foundation/contract change: 0
- Production test-server linkage: 0

세 original finding의 재현 경로가 source와 독립 test에서 모두 해소됐고, PASS 조건을 막는 신규 finding은 확인되지 않았다.

## 3. Provenance and Ownership

원본 worktree 시작 상태는 다음 두 기존 Review 1 파일만 untracked였다.

```text
?? session_results/wave1/review/CC-SENSOR-CORE_CODEX_REVIEW_1.md
?? session_results/wave1/review/CC-SENSOR-STREAM_CODEX_REVIEW_1.md
```

`CC-SENSOR-STREAM_CODEX_REVIEW_1.md`는 시작/종료 SHA-256이 모두 `48b01c72677aae9380a86b96883753809e038bb6968dfd7845a4b896d34f220d`이며 수정·stage·commit하지 않았다. Review 2 output은 작성 직전까지 존재하지 않았다.

Fix implementation commit `5fecfcb15b11f5d93562f4cec2d0ae713acc6674`의 변경 파일:

```text
src/features/result_policy/result_policy.c
src/features/stream/include/sensor_stream/session.h
src/features/stream/session.c
tests/integration/sensor_stream/session/CMakeLists.txt
tests/integration/sensor_stream/session/test_session.c
tests/unit/sensor_stream/result_policy/test_result_policy.c
tools/mock_streaming_server/CMakeLists.txt
tools/mock_streaming_server/mock_streaming_server.c
tools/mock_streaming_server/test_mock_response.c
```

Report-only commit `fd0d0eaad50595e34d869a96afb8a662f62e07e8`의 변경 파일:

```text
session_results/wave1/CC-SENSOR-STREAM.md
```

`af2bcf79bf52de0b12f7948ccf6ed67eeae45c70...fd0d0eaad50595e34d869a96afb8a662f62e07e8`에 대해 `git diff --check`는 PASS했다. 정량 ownership 결과:

| 항목 | 결과 |
|---|---:|
| Allowed path violation | 0 |
| Foundation change (`src/core`, `src/protocol`, platform interfaces/IPC, third_party) | 0 |
| Contract change | 0 |
| Root `CMakeLists.txt`/`CMakePresets.json`/`cmake/**` change | 0 |
| Root `SESSION_RESULT.md` change | 0 |
| Other-session change | 0 |
| Review file commit | 0 |
| New wire command | 0 |
| New JSON field | 0 |
| Timeout change | 0 |
| Queue policy change | 0 |
| Durable/disk queue | 0 / 0 |
| BZip raw fallback | 0 |
| Production test-server linkage | 0 |

기존 wire byte는 `S/V/T` start와 `I/S/Z/V/O/R` command로 유지되고, connect/response timeout은 `1000/3000ms`, queue depth는 4로 유지된다.

## 4. Original Finding Status

| Finding | Review 1 severity | Review 2 status | 핵심 근거 |
|---|---|---|---|
| `CDX-W1-SENSOR-STREAM-001` | High | **RESOLVED** | quoted/raw와 unquoted/normalized 입력 모두 `len + 1` 종단 NUL 소유 버퍼를 사용하며 Docker ASan+UBSan 6/6 PASS |
| `CDX-W1-SENSOR-STREAM-002` | High | **RESOLVED** | raw maximum과 Stream/Voice/WAV/BZip encoded capacity 분리, production session boundary 1/1 PASS |
| `CDX-W1-SENSOR-STREAM-003` | Medium | **RESOLVED** | mock standalone `ctest -N` 8개, response-side fixture와 production TCP API behavior 8/8 PASS |

## 5. Finding 001 Verification

판정: **RESOLVED**

Source inspection:

- `src/features/result_policy/result_policy.c:98-137`의 `make_parser_input()`은 rewrite가 없으면 `body_len + 1`, rewrite가 있으면 `body_len + 2 + 1`을 overflow-safe하게 계산한다.
- `body_len > SIZE_MAX - extra - 1`이면 `SAVVY_ERR_OVERFLOW`, `malloc()` 실패면 `SAVVY_ERR_OUT_OF_MEMORY`를 반환한다.
- raw/normalized 양쪽 모두 `text[parser_len] = '\0'`을 기록한다.
- `sensor_result_policy_on_response()`은 성공한 소유 버퍼만 Foundation parser에 넘기고 parser 호출 직후 정확히 한 번 `free(parser_text)`한다. 실패 경로는 policy state를 변경하지 않는 no-op이다.
- normalizer는 optional whitespace 뒤 `{`, exact bareword `result`, optional whitespace, `:`인 첫 key만 rewrite한다. `resultx`, `myresult`, 문자열 내부 `result:`는 rewrite하지 않는다.
- Foundation source 변경은 0이다.

입력 matrix 실제 검증:

| 입력/동작 | 결과 |
|---|---|
| `{"result":4}`, `{"result":7}` | PASS |
| `{result:4}`, `{result: 4}`, `{result:-1}` | PASS |
| `resultx`, `myresult` negative control | PASS |
| 문자열 내부 `result:` | PASS |
| truncated input | PASS, parse-failure no-op |
| empty input | PASS, no-op |
| quoted/unquoted counted buffer without terminator | PASS |
| Voice normal/danger callback | PASS |
| Stream danger threshold/reset | PASS |

주요 실행 결과:

```text
macOS plain result-policy: 6/6 PASS
macOS UBSan result-policy: 6/6 PASS
Docker arm64 UBSan result-policy: 6/6 PASS
Docker arm64 ASan+UBSan result-policy: 6/6 PASS
독립 subtest SNS-STR-002-unquoted-wire-quirk: PASS
```

Docker ASan+UBSan은 Review 1의 `json_codec.c` 1-byte heap-buffer-overflow 재현 command와 같은 product path를 실행했으며 sanitizer diagnostic이 없었다.

## 6. Finding 002 Verification

판정: **RESOLVED**

Public contract와 구현:

- `src/features/stream/include/sensor_stream/session.h:39-46`은 `max_payload_size`를 packet/WAV/BZip 전의 maximum raw input으로 명시한다.
- `src/features/stream/session.c:54-99`는 다음 encoded capacity를 별도로 계산한다.
  - Stream raw: `26 + raw max`
  - Voice raw: `26 + 44 + raw PCM max`
  - Compressed: `26 + (codec input + codec input/100 + 600)`; Voice는 codec input에 44-byte WAV header를 먼저 포함한다.
- WAV addition, BZip growth, 600-byte addition, packet header addition을 각각 overflow-check한다. BZip input의 `UINT_MAX` 제한과 packet Length의 `UINT32_MAX` 제한도 검사한다.
- `src/features/stream/session.c:175-180`은 실제 raw input만 `session->max_payload_size`와 비교한다.
- encode buffer와 production TCP transport capacity에는 raw max가 아니라 계산된 encoded capacity를 전달한다.
- `compress != 1`의 기존 raw 의미, timeout, queue, retry, wire command는 변경되지 않았다.

`SNS-STR-BOUNDARY-production-session-api`는 production `sensor_stream_session_*` API와 loopback TCP server를 직접 사용했다. 실제 결과:

| Case | 결과/실제 관측 |
|---|---|
| Stream max-1 (127) | accepted, callback success, server body 127 |
| Stream max (128) | accepted, callback success, server body 128 |
| Stream max+1 (129) | synchronous `SAVVY_ERR_OVERFLOW`, callback 0 |
| Voice max-1 (127) | accepted, callback success, server body 171 |
| Voice max (128) | accepted, callback success, server body 172 |
| Voice max+1 (129) | synchronous `SAVVY_ERR_OVERFLOW`, callback 0 |
| Voice WAV expansion | 44-byte expansion과 `RIFF` magic 확인 |
| Stream compressed high-entropy | accepted, Command `Z`, compressed body > 128 |
| Voice compressed high-entropy | accepted, Command `Z`, compressed WAV body > 128 |
| Compressed output larger than input | Stream/Voice 모두 encoded capacity 내 성공 |
| small max 0 | Stream/Voice create PASS |
| `SIZE_MAX` config | Stream/Voice create `SAVVY_ERR_OVERFLOW`, session NULL |

```text
macOS plain boundary: 1/1 PASS
macOS UBSan boundary: 1/1 PASS
Docker arm64 UBSan boundary: 1/1 PASS
Docker arm64 ASan+UBSan boundary: 1/1 PASS
```

## 7. Finding 003 Verification

판정: **RESOLVED**

Mock clean build의 실제 enumeration:

```text
ctest --test-dir /tmp/cc-sensor-stream-review2-1.Tql3Z6/macos/plain/mock -N

MOCK-response-header-split
MOCK-response-body-split
MOCK-response-delay
MOCK-response-timeout
MOCK-late-response-after-timeout
MOCK-disconnect-before-response-header
MOCK-disconnect-during-response-body
MOCK-stream-failure-voice-healthy
Total Tests: 8
```

`tools/mock_streaming_server/CMakeLists.txt:30-85`는 mock tests에서 production `tcp_channel.c`를 test-local library로 compile하고 8개 CTest를 등록한다. dependency 방향은 test에서 production으로만 향한다.

Fixture/source inspection 결과:

- `partial-header`는 RESPONSE의 26-byte header 내부 5바이트 뒤에서 실제 송신을 분할한다.
- `split-body`는 full header + body 3바이트 뒤에서 실제 송신을 분할한다.
- delay는 500ms 후 정상 response, timeout/late는 250ms client timeout보다 늦은 600ms response를 만든다.
- disconnect-before-header는 request를 소비한 뒤 response byte 없이 close한다.
- disconnect-during-body는 full response header와 body 4바이트만 보낸 뒤 close한다.
- isolation은 독립 ephemeral port의 Stream failure server와 Voice healthy server에 production TCP channel 두 개를 연결한다.
- port는 모두 `--port=0`으로 OS가 배정하며, completion/child wait와 CTest timeout은 bounded다.
- 실패 시 harness는 non-zero를 반환하고, timeout 시 child를 kill/waitpid하여 reap한다.

실제 결과:

```text
macOS plain: 8/8 PASS
macOS UBSan: 8/8 PASS
Docker arm64 plain: 8/8 PASS
Docker arm64 UBSan: 8/8 PASS
Docker arm64 ASan+UBSan final probe: 8/8 PASS
```

## 8. macOS Results

환경:

```text
OS/architecture: Darwin 25.5.0 / arm64
CMake: 4.4.0
Compiler: AppleClang 17.0.0
libbz2: 1.0.8 (macOS SDK)
```

각 mode는 별도 clean `/tmp` build directory에서 Foundation, TCP, result policy, WAV, compression, stream, mock을 configure/build/test했다.

| 범위 | Plain | UBSan | `-Wall -Wextra -Wpedantic` |
|---|---:|---:|---:|
| Foundation | 5/5 | 5/5 | 5/5 |
| TCP 8141 | 5/5 | 5/5 | 5/5 |
| Result policy | 6/6 | 6/6 | 6/6 |
| WAV | 4/4 | 4/4 | 4/4 |
| BZip | 6/6 | 6/6 | 6/6 |
| Stream/session/boundary | 5/5 | 5/5 | 5/5 |
| Mock response | 8/8 | 8/8 | 8/8 |
| **합계** | **39/39 PASS** | **39/39 PASS** | **39/39 PASS** |

UBSan diagnostic은 없었다. strict warning build도 성공했다. macOS Foundation에 Linux-only real IPC transport 옵션을 강제한 사전 probe는 `SOCK_CLOEXEC` 미정의로 build되지 않았으므로 39-test macOS 기준에는 기본 transport build를 사용했고, real IPC transport는 Docker Linux에서 검증했다.

macOS ASan 실제 현상:

- exact target result-policy ASan build는 성공했다.
- `ASAN_OPTIONS=detect_leaks=1`은 `AddressSanitizer: detect_leaks is not supported on this platform`으로 subtest 시작 전에 abort했다.
- `detect_leaks=0`의 동일 `SNS-STR-002-unquoted-wire-quirk`는 CTest의 20초 제한에서 timeout됐다.
- 이는 이 host의 sanitizer runtime 현상이며, 지침에 따라 finding으로 만들지 않았다. macOS UBSan과 Docker arm64 ASan+UBSan으로 보완했다.

## 9. Docker arm64 Results

외부 네트워크를 사용하지 않았다. 로컬 image, `--network none`, exact clone read-only mount, 매 container의 새 `/tmp` build directory를 사용했다.

| 항목 | 실제 값 |
|---|---|
| Runtime image | `savvy-foundation-test:ubuntu22.04-arm64-v1` |
| Runtime image ID | `sha256:73c8a9709607d1910231efb4648510e4d72052072629901fa28fd5c9f39753e7` |
| Base `ubuntu:22.04` local image ID | `sha256:0e0a0fc6d18feda9db1590da249ac93e8d5abfea8f4c3c0c849ce512b5ef8982` |
| OS | Ubuntu 22.04.5 LTS |
| Architecture / word size | `aarch64` / 64 |
| CMake | 3.22.1 |
| Compiler | GCC 11.4.0 |
| Linker | GNU ld 2.38 |
| libbz2 | `libbz2-dev=1.0.8-5build1`, `libbz2-1.0=1.0.8-5build1` |

| 범위 | Plain | UBSan |
|---|---:|---:|
| Foundation real IPC | 9/9 | 9/9 |
| TCP 8141 | 5/5 | 5/5 |
| Result policy | 6/6 | 6/6 |
| WAV | 4/4 | 4/4 |
| BZip | 6/6 | 6/6 |
| Stream/session/boundary | 5/5 | 5/5 |
| Mock response | 8/8 | 8/8 |
| **합계** | **43/43 PASS** | **43/43 PASS** |

Finding-specific clean ASan+UBSan:

```text
Result policy: 6/6 PASS
Production session boundary: 1/1 PASS
Mock response final probe: 8/8 PASS
```

최초 탐색 mock ASan+UBSan+LeakSanitizer run에서 production outcome은 기대값이었지만 child normal-exit 2초 wait가 `timeout`, `disconnect-before-header` 두 fixture에서 일시 초과했다. 새 clean probe에서 해당 두 fixture는 LeakSanitizer를 켜고 각 3회 연속 PASS했고, 주소/UB sanitizer를 유지하고 leak 검사만 끈 final 전체는 8/8 PASS했다. 지속 재현되는 production 또는 cleanup 결함은 확인되지 않았다.

## 10. Required Test IDs

아래 command는 macOS plain exact-target build에서 각 ID를 독립 실행했다. 모든 test는 production/Foundation code를 실제 호출하며 placeholder가 아니다.

| ID | 실제 executable/CTest | 실제 command 요약 | 실제 product behavior | 결과 |
|---|---|---|---|---|
| `CT-PKT-001` | `test_packet 001` / `CT-PKT-001` | `ctest --test-dir <plain>/foundation -R '^CT-PKT-001$'` | 0B/1B/JSON/binary packet golden vectors | PASS |
| `CT-PKT-002` | `test_packet 002` / `CT-PKT-002` | `ctest --test-dir <plain>/foundation -R '^CT-PKT-002$'` | partial header, split body, coalesced packet parsing | PASS |
| `CT-PKT-003` | `test_packet 003` / `CT-PKT-003` | `ctest --test-dir <plain>/foundation -R '^CT-PKT-003$'` | CRC, length overflow, serial policy | PASS |
| `SNS-STR-001` | `test_session 001` / `SNS-STR-001-stream-full-flow` | `ctest --test-dir <plain>/stream -R '^SNS-STR-001-'` | production Stream I→S→O, lazy connect/callback | PASS |
| `SNS-STR-002` | `test_result_policy 001..006` / 6 CTests | `ctest --test-dir <plain>/result_policy -R '^SNS-STR-002-'` | DataResult matrix, unquoted wire, threshold/reset | PASS |
| `SNS-STR-003` | `test_session 003,003wav` / 2 CTests | `ctest --test-dir <plain>/stream -R '^SNS-STR-003-'` | Voice flow, decibel, WAV/compression | PASS |
| `SNS-STR-004` | `test_wav 001..004`, `test_bzip 005` | WAV `-R '^SNS-STR-004-WAV-'`, BZip `-R '^SNS-STR-004-BZIP-'` | WAV header/format/overflow와 WAV BZip round-trip | PASS |
| `SNS-STR-005` | `test_bzip 001..004,006` / 5 CTests | `ctest --test-dir <plain>/compression -R '^SNS-STR-005-'` | round-trip, larger output, corrupt/empty, leak loop | PASS |
| `SNS-STR-006` | `test_tcp_channel 006` | `ctest --test-dir <plain>/tcp_8141 -R '^SNS-STR-006-'` | bounded queue overflow | PASS |
| `SNS-STR-007` | `test_tcp_channel 007` | `ctest --test-dir <plain>/tcp_8141 -R '^SNS-STR-007-'` | lifecycle wake/idempotent start-stop | PASS |
| `SNS-STR-008` | `test_tcp_channel 008` | `ctest --test-dir <plain>/tcp_8141 -R '^SNS-STR-008-'` | repeated connect/disconnect fd stability | PASS |
| `SNS-STR-009` | `test_tcp_channel 009`, `test_session isolation` | TCP/stream 각각 `-R '^SNS-STR-009-'` | channel 및 session Stream/Voice isolation | PASS |
| `SNS-STR-010` | `test_tcp_channel 010` | `ctest --test-dir <plain>/tcp_8141 -R '^SNS-STR-010-'` | stale/late response 처리 | PASS |

Required ID 총 13개 모두 PASS다.

## 11. Link and Production Separation

Docker arm64 plain build의 실제 결과:

```text
readelf -d test_bzip:
NEEDED Shared library: [libbz2.so.1.0]

ldd test_bzip:
libbz2.so.1.0 => /lib/aarch64-linux-gnu/libbz2.so.1.0

link.txt:
... /usr/lib/aarch64-linux-gnu/libbz2.so
```

Production separation:

- `src/features/stream`과 `src/platform/linux/tcp_8141` source에서 `mock_streaming_server`/`test_mock_response` reference 0.
- production `sensor_stream_session` target build graph에서 mock reference 0.
- `nm -A libsensor_stream_session.a`에서 mock symbol 0.
- production test-server linkage: **NO**.

## 12. Session Result Audit

감사 대상: `session_results/wave1/CC-SENSOR-STREAM.md`

| 요구 항목 | 결과 | 근거 |
|---|---|---|
| Review target full SHA | PASS | `af2bcf79...` 명시 |
| Fix implementation full SHA | PASS | `5fecfcb15...` 명시 |
| Fix report/Review 2 target self-reference | PASS | completion output 참조 형식 사용; 이번 review에서 실제 SHA `fd0d0eaa...` 조회 |
| 세 finding 수정 설명 | PASS | Before/After 표 |
| 수정 파일 목록 | PASS | 9개 fix file 명시 |
| 기존 입력 파일 처리 | PASS | `PREEXISTING_FIX_INPUT`과 repro 대체/삭제 기록 |
| macOS 39/39 | PASS | plain/UBSan/warnings 각각 39/39 |
| Docker 43/43 | PASS | plain/UBSan 각각 43/43 |
| 필수 13 ID | PASS | 13개 표 |
| 추가 boundary 1 | PASS | production session boundary 1/1 |
| 추가 mock 8 | PASS | mock response 8/8 |
| Ownership | PASS | allowed/Foundation/contract/other-session 0 |
| Review 파일 무변경 | PASS | SHA-256 동일 및 commit 0 |
| RV1106/hardware `NOT_PERFORMED` | PASS | 세 항목 명시 |
| `CODEX_VERIFIED`/`MERGE_READY` 과장 없음 | PASS | `CODEX_REREVIEW: NOT_STARTED`, `MERGE_READY: NO` |

문서 앞부분의 34-test 수치는 Fix 이전 historical implementation 결과이고, Fix 1 섹션은 최종 exact target의 39/43 및 추가 9 CTest를 명확히 기록한다. Review 2 실제 결과와 일치한다.

## 13. New Findings

없음.

```text
Critical: 0
High: 0
Medium: 0
Low: 0
```

macOS ASan runtime 제약과 최초 exploratory Docker LeakSanitizer child-exit 지연은 각각 host/runtime 현상이거나 반복 검증에서 재현되지 않은 비필수 탐색 신호다. production 오류로 확정할 근거가 없으므로 finding으로 등록하지 않았다.

## 14. Final Recommendation

**PASS**. 세 original finding은 모두 RESOLVED이며 Fix session은 추가로 요구되지 않는다. Required 13 IDs, macOS plain/UBSan, Docker arm64 plain/UBSan, finding-specific sanitizer, ownership, system libbz2 link, production/mock separation이 모두 PASS했다.

이 reviewer는 `MERGE_READY`를 선언하지 않는다.

## 15. Verification Boundary

- RV1106 cross-build: `NOT_PERFORMED`
- RV1106 board runtime: `NOT_PERFORMED`
- Hardware QA: `NOT_PERFORMED`
- Android 앱/실제 streaming server 실행 또는 배포: 범위 밖이며 수행하지 않았다.
- 외부 시스템/제3자 서비스/외부 네트워크: 접근하지 않았다.
- Original production source/test/Review 1: 수정하지 않았다.
- Review 2 report 외 repository file: 작성하지 않았다.
- macOS ASan: host runtime 제약으로 `NOT_AVAILABLE`; 실제 abort/timeout 현상은 §8에 기록했다.
- Docker arm64 ASan+UBSan: result policy 6/6, boundary 1/1, final mock 8/8 PASS.

```text
FIX_SESSION_REQUIRED: NO
MERGE_READY: NOT_DECLARED_BY_REVIEWER
```
