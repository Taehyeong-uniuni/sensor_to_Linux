# CC-SENSOR-STREAM — Wave 1 최종 구현 결과

STATE: IMPLEMENTATION_FINISHED

NEXT: AWAITING_CODEX_REVIEW

## 1. 세션 식별과 고정 근거

- SESSION_ID: `CC-SENSOR-STREAM`
- 저장소 / worktree: `sensor_to_Linux` / `/Users/juganghyeon/Desktop/uniuni/projects/worktrees/sensor-stream`
- branch: `feature/sensor-stream`
- BASE_SHA: `07809cb1f3f2b86a8e92ade661c48cb3adb97b52`
- CHECKPOINT_SHA: `212010f5ab62571aa66e72b12b8eea0ed37df944`
- `contract-v1` 실제 target: `07809cb1f3f2b86a8e92ade661c48cb3adb97b52` (annotated tag)
- Foundation implementation SHA: `aca143a7f4b76dc8cb6fff324ca21ea9f557622a` (현재 HEAD의 ancestor 확인)
- contract manifest: `contracts/contract-manifest.sha256`의 SHA-256 = `a69536c286839c97e05ed7f54b5834d843f94eae4a9221ad6213de93d268fa6e` (확인 PASS)
- dependency manifest: `third_party/DEPENDENCY_MANIFEST.sha256`의 SHA-256 = `9934277d3a8d1dabd1c2632d3501743f8d2a57218c6dd6f3635b2b3844296ad2` (확인 PASS)

### Pinned source / caller·consumer 대조

- Android Sensor baseline: `ClientChannel`의 connect 1000ms / response 3000ms와 `MainActivity.runPirErr()`를 대조했다. 이번 구현은 요구사항에 따라 Stream/Voice 오류를 서로 종료시키지 않으며, `SNS-STR-009`가 이를 확인한다.
- `streaming_server_v2` baseline: `39a6f49343e38ff8b62bb3d1ab7233065d593d4a`의 `DeviceHandler.java`가 내보내는 `{result:N}` / `{result: N}` 응답을 기준으로 한다.
- Foundation consumer: result policy는 별도 JSON parser를 만들지 않고 Foundation의 `savvy_data_result_parse()`와 `savvy_data_result_is_normal()`만 소비한다.
- caller/consumer: Stream session은 자체 TCP channel·result policy·WAV·BZip target을 소비하고, integration test가 public session API를 호출한다. mock server는 별도 standalone test double target이며 production target의 consumer가 아니다.

## 2. G001~G006 checkpoint 보존 상태

| Goal | 최종 상태 | 보존한 checkpoint 근거 |
|---|---|---|
| G001-SNS01 | DONE | TCP 8141 lifecycle, SNS-STR-006~010 5/5 및 macOS UBSan clean |
| G002-SNS02 | DONE | PIRIN/data/PIROUT wire flow, SNS-STR-001/003 |
| G003-SNS03 | DONE | result policy, DataResult Foundation codec 소비, 6/6 |
| G004-SNS04 | DONE | WAV 4/4, BZip 6/6 |
| G005-MOCK | DONE | selectable fixture mock, 4 fixture spot-check |
| G006-TESTS | DONE | CT-PKT-001~003 및 SNS-STR-001~010 존재·PASS |

사용자 승인된 `{result:N}` normalization은 유지했다. 정확히 `{` 뒤의 bareword `result` key만 인용해 Foundation codec에 넘기며, Foundation·contract 파일은 변경하지 않았다.

## 3. G007 — Docker Ubuntu 22.04 arm64

### 실행 명령과 환경

최종 re-run의 실제 Docker command는 아래와 같다. 소스는 read-only mount, build는 컨테이너의 `/tmp/cc-sensor-stream/{plain,ubsan}`에 생성한 뒤 매번 삭제하여 clean configure/build를 보장했다.

```text
docker start cc-sensor-stream-g007-rerun
```

해당 컨테이너는 `ubuntu:22.04`, `--platform linux/arm64`, `-v "$PWD:/workspace:ro"`, `-w /workspace`로 생성했고, `build-essential cmake libbz2-dev binutils file`를 설치했다. 각 mode에서 실제 수행한 source root는 다음 여섯 개다.

```text
cmake -S /workspace -B /tmp/cc-sensor-stream/<mode>/foundation -DSAVVY_IPC_REAL_TRANSPORT=ON
cmake -S /workspace/src/platform/linux/tcp_8141 -B /tmp/cc-sensor-stream/<mode>/tcp_8141
cmake -S /workspace/src/features/result_policy -B /tmp/cc-sensor-stream/<mode>/result_policy
cmake -S /workspace/src/features/wav -B /tmp/cc-sensor-stream/<mode>/wav
cmake -S /workspace/src/features/compression -B /tmp/cc-sensor-stream/<mode>/compression
cmake -S /workspace/src/features/stream -B /tmp/cc-sensor-stream/<mode>/stream
cmake --build <each-build-dir> --parallel 2
ctest --test-dir <each-build-dir> --output-on-failure
```

UBSan mode에는 `-DCMAKE_C_FLAGS=-fsanitize=undefined -fno-sanitize-recover=undefined` 및 `-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=undefined`를 추가했다. mock은 plain standalone configure/build도 수행했다.

| 항목 | 실제 값 / 결과 |
|---|---|
| image | `ubuntu:22.04`, `sha256:0e0a0fc6d18feda9db1590da249ac93e8d5abfea8f4c3c0c849ce512b5ef8982` |
| OS | Ubuntu 22.04.5 LTS (Jammy) |
| architecture / word size | `aarch64` / `64` |
| CMake / compiler / linker | CMake 3.22.1 / GCC 11.4.0 / GNU ld 2.38 |
| libbz2 package | `libbz2-dev=1.0.8-5build1`, `libbz2-1.0=1.0.8-5build1` |
| final elapsed | 34 seconds (clean build directories 기준) |
| plain Docker | PASS |
| Docker UBSan | PASS, diagnostic 없음 |

### Docker 테스트 결과

- plain: 총 34 CTest case PASS — Foundation 9, TCP 5, result policy 6, WAV 4, BZip 6, Stream 4.
- UBSan: 같은 총 34 CTest case PASS, UBSan diagnostic 없음.
- 필수 13개 ID: `CT-PKT-001~003`, `SNS-STR-001~010` 모두 PASS. Foundation의 CT-JSON 및 CT-IPC 보조 test도 PASS했다.
- timeout/hang/crash 없음. queue overflow, stale response, channel isolation, lifecycle/fd 반복은 TCP test 5/5로 PASS했다.
- WAV/BZip byte round-trip, high-entropy/larger output, corrupt-input reject, empty input, BZip leak loop는 BZip 6/6으로 PASS했다.

### libbz2와 mock 증거

```text
readelf -d test_bzip: NEEDED Shared library: [libbz2.so.1.0]
ldd test_bzip: libbz2.so.1.0 => /lib/aarch64-linux-gnu/libbz2.so.1.0
link command: ... /usr/lib/aarch64-linux-gnu/libbz2.so
production mock linkage: absent
```

`stream` build CMake target graph에서 `mock_streaming_server` 참조가 없고, `nm -A libsensor_stream_session.a`에도 mock symbol이 없음을 Docker에서 확인했다.

### checkpoint 이후 Docker 수정

첫 Docker 실행은 package version 출력의 shell quoting 오류로 build 전 종료되어, 검증 명령만 `dpkg -s` 기반 출력으로 최소 보정했다. 이후 Linux strict C11/glibc에서 드러난 선언 노출 문제를 다음 네 최소 수정으로 해결하고 macOS·Docker 전체를 재실행했다.

- `tcp_channel.c`: `_POSIX_C_SOURCE 200809L` 추가로 `getaddrinfo` 계열 선언 노출.
- TCP unit test, stream integration test, mock server: `_DEFAULT_SOURCE` 추가로 기존 `usleep` test/helper 선언 노출.

wire command, JSON field, retry, queue 정책, timeout 값, DataResult 정책에는 변경이 없다.

## 4. macOS 검증

checkpoint에서 전체 feature suite의 plain/UBSan 결과(TCP 5/5, result policy 6/6, stream 4/4, WAV 4/4, BZip 6/6)가 PASS였다. checkpoint 이후 수정 영향 범위는 다음 실제 명령으로 재검증했다.

```text
cmake -S src/platform/linux/tcp_8141 -B /tmp/cc-sensor-stream-macos-g007/plain
cmake --build /tmp/cc-sensor-stream-macos-g007/plain --parallel 2
ctest --test-dir /tmp/cc-sensor-stream-macos-g007/plain --output-on-failure

cmake -S src/platform/linux/tcp_8141 -B /tmp/cc-sensor-stream-macos-g007/ubsan \
  -DCMAKE_C_FLAGS='-fsanitize=undefined -fno-sanitize-recover=undefined' \
  -DCMAKE_EXE_LINKER_FLAGS=-fsanitize=undefined
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 ctest --test-dir /tmp/cc-sensor-stream-macos-g007/ubsan --output-on-failure

cmake -S src/features/stream -B /tmp/cc-sensor-stream-macos-g007-stream/plain
ctest --test-dir /tmp/cc-sensor-stream-macos-g007-stream/plain --output-on-failure
cmake -S src/features/stream -B /tmp/cc-sensor-stream-macos-g007-stream/ubsan <same UBSan flags>
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 ctest --test-dir /tmp/cc-sensor-stream-macos-g007-stream/ubsan --output-on-failure
cmake -S tools/mock_streaming_server -B /tmp/cc-sensor-stream-macos-g007-stream/mock
cmake --build /tmp/cc-sensor-stream-macos-g007-stream/mock --parallel 2
```

| 범위 | plain | UBSan |
|---|---:|---:|
| TCP channel (SNS-STR-006~010) | 5/5 PASS | 5/5 PASS |
| Stream session (SNS-STR-001, 003, 003wav, 009) | 4/4 PASS | 4/4 PASS |
| mock standalone | build PASS | 해당 없음 |

ASan은 checkpoint에서 이 macOS host의 sanitizer runtime 초기화 단계에서 독립 trivial program에도 재현되는 환경 문제로 `NOT_AVAILABLE`이며, 이번 완료 기준 sanitizer는 UBSan이다.

## 5. G008 — 최종 ownership·scope 점검

`git diff --check` PASS, `build/` 미추적 산출물 0, Allowed path violation 0을 확인했다. Base부터 worktree까지 변경 파일은 28개이며 전부 Allowed paths에 속한다. 아래 21개 항목은 문서 최종화 직전 PASS로 확인했다.

| # | 항목 | 결과 |
|---:|---|---|
| 01 | Allowed path violation = 0 | PASS |
| 02 | `contracts/**` diff = 0 | PASS |
| 03 | `src/core/**` diff = 0 | PASS |
| 04 | `src/protocol/**` diff = 0 | PASS |
| 05 | `src/platform/interfaces/**` diff = 0 | PASS |
| 06 | `src/platform/linux/ipc/**` diff = 0 | PASS |
| 07 | `third_party/**` diff = 0 | PASS |
| 08 | root `CMakeLists.txt` diff = 0 | PASS |
| 09 | `CMakePresets.json` diff = 0 | PASS |
| 10 | `cmake/**` diff = 0 | PASS |
| 11 | `src/app/**` diff = 0 | PASS |
| 12 | CC-SENSOR-CORE paths diff = 0 | PASS |
| 13 | Android/server repository modification = 0 | PASS |
| 14 | production mock/dummy linkage = 0 | PASS |
| 15 | 신규 wire command = 0 | PASS — 기존 `I/S/V/O/R`, existing `T/S` relay만 사용 |
| 16 | 신규 JSON field = 0 | PASS — Foundation codec에 기존 `result`만 전달 |
| 17 | durable retry 추가 = 0 | PASS |
| 18 | disk queue 추가 = 0 | PASS — Foundation memory queue만 사용 |
| 19 | timeout contract 변경 = 0 | PASS — connect 1000ms, response 3000ms |
| 20 | DataResult 별도 parser 추가 = 0 | PASS — Foundation parser 소비 |
| 21 | BZip silent raw fallback = 0 | PASS |

추가 검사도 PASS: `SESSION_RESULT.md` diff 0, `tests/verification/**` diff 0, `tools/verification/**` diff 0, contract/Foundation/root CMake 변경 0, production mock 0.

## 6. API와 동작 대조

Public API는 다음으로 한정된다.

- TCP: `sensor_tcp_channel_create/start/submit/submit_final/try_relay/is_connected/close_session/stop/destroy`
- Session: `sensor_stream_session_create/start/send_pirin/send_data/send_pirout/relay_rknn_result/is_connected/stop/destroy`
- Result policy: `sensor_result_policy_create/reset/on_response/danger_count/role/destroy`
- Codec feature: `sensor_wav_wrap`, `sensor_bzip_compress`, `sensor_bzip_decompress`

Stream과 Voice는 각 `sensor_stream_session_create()` 호출이 별도 TCP worker, socket, bounded queue, result-policy state를 소유하도록 구성되어 있다. Docker와 macOS의 `SNS-STR-009` PASS가 channel isolation을, TCP 006/007/008/010 PASS가 overflow·lifecycle·fd leak·stale response를 각각 확인한다. connect/response timeout은 각각 1000ms/3000ms로 고정돼 있다.

## 7. 변경 파일

### checkpoint 포함 전체 changed files (28)

```text
session_results/wave1/CC-SENSOR-STREAM.md
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

### checkpoint 이후 changed files

```text
session_results/wave1/CC-SENSOR-STREAM.md     # G009 finalization
src/platform/linux/tcp_8141/tcp_channel.c     # Linux POSIX declaration visibility
tests/integration/sensor_stream/session/test_session.c
tests/unit/sensor_stream/tcp_channel/test_tcp_channel.c
tools/mock_streaming_server/mock_streaming_server.c
```

## 8. 미수행 항목과 rollback

- `B-005`, `B-010`, `B-012`, `B-022`: `NOT_PERFORMED` (RV1106/hardware 범위 밖).
- `RV1106_CROSS_BUILD`: `NOT_PERFORMED`.
- `RV1106_BOARD_RUNTIME`: `NOT_PERFORMED`.
- `HARDWARE_QA`: `NOT_PERFORMED`.
- rollback: final implementation commit의 SHA는 completion report를 참조하며, 필요 시 해당 commit을 `git revert <FINAL_IMPLEMENTATION_SHA>`로 되돌린다. checkpoint SHA로의 되돌림은 별도 branch에서 수행한다.

FINAL_IMPLEMENTATION_SHA:
See the completion report for the commit containing this finalized document.

## Completion conclusion

```text
IMPLEMENTATION_FINISHED

AWAITING_CODEX_REVIEW

This is not CODEX_VERIFIED.
This is not MERGE_READY.
RV1106 cross-build and hardware QA were not performed.
```

## 9. Codex Review 1 Fix — CC-SENSOR-STREAM-FIX-1

```text
STATUS: FIX_IMPLEMENTED
NEXT: AWAITING_CODEX_REREVIEW
```

### Review와 기존 입력

- Review file: `session_results/wave1/review/CC-SENSOR-STREAM_CODEX_REVIEW_1.md`
- Review target: `af2bcf79bf52de0b12f7948ccf6ed67eeae45c70`
- 처리 Finding: `CDX-W1-SENSOR-STREAM-001`, `CDX-W1-SENSOR-STREAM-002`, `CDX-W1-SENSOR-STREAM-003`
- `PREEXISTING_FIX_INPUT`: `ACCEPTED`
- 시작 시 `tests/integration/sensor_stream/session/CMakeLists.txt`는 boundary repro executable을 build하는 변경이었고, `repro_boundary_before.c`는 production session API에 `max_payload_size=128`, Stream/Voice 127·128·129 byte를 넣어 결과를 출력하는 39-line 재현 프로그램이었다. 두 파일에는 Foundation/contract/다른 session 변경이나 외부 접근이 없었다.
- 시작 SHA-256: CMakeLists `0c71179dfe2714397dd3a46f02af9e241af39f118004d0cb4dc34949e9c6c56e`, repro `f97c465e07999b095353d07b4fdcb274cb8bf558182cbfef1063f2ff01929ef1`.
- `PREEXISTING_FILES_FINAL_ACTION`: CMakeLists는 정식 `SNS-STR-BOUNDARY-production-session-api` CTest 등록으로 수정했다. print-only untracked `repro_boundary_before.c`는 삭제하고 그 동작을 `tests/integration/sensor_stream/session/test_session.c`의 loopback production-session test로 대체했다. 보존된 behavior는 Stream/Voice 127·128·129 boundary이며, WAV 확장·Stream/Voice compressed high-entropy·compressed output larger than input·작은/overflow config 검증을 추가했다.

### Finding별 수정과 Before/After

| Finding | Before | After |
|---|---|---|
| `CDX-W1-SENSOR-STREAM-001` | unquoted normalization이 `malloc(new_len)`만 수행해 Foundation parser의 `text[len]=='\0'` precondition을 위반했고 Linux ASan heap-buffer-overflow가 재현됐다. | quoted/raw와 unquoted/normalized 모두 overflow-checked `len+1` 소유 버퍼로 만들고 마지막 NUL을 기록한다. allocation 실패는 no-op으로 처리하고 parser 호출 후 정확히 한 번 free한다. quoted 4/7, unquoted 4/space/negative, `resultx`, `myresult`, string 내부 `result:`, truncated, empty 및 terminator 없는 counted buffer를 검증했다. Docker ASan+UBSan result policy 6/6 PASS. |
| `CDX-W1-SENSOR-STREAM-002` | 기존 repro에서 Stream/Voice 127·128·129가 모두 `SAVVY_ERR_OVERFLOW(10)`이었다. raw max와 encoded packet capacity가 같은 값이었다. | raw upper bound를 session에 별도 저장하고 Stream=`26+raw`, Voice=`26+44+raw`, compressed=`26+(input+input/100+600)` capacity를 overflow-safe하게 계산한다. production session API test에서 Stream/Voice max-1·max callback 성공, max+1 synchronous reject, Voice WAV 44-byte 확장, high-entropy compressed 확대 결과를 확인했다. |
| `CDX-W1-SENSOR-STREAM-003` | mock standalone build의 `ctest -N`은 `Total Tests: 0`; `partial-header`/`split-body`는 request-side fragmentation이었다. | mock build에 production TCP channel을 사용하는 8개 CTest를 등록했다. response header/body 실제 분할, delay, timeout, late response, header/body 중 disconnect, Stream failure/Voice healthy isolation을 ephemeral port와 bounded child wait로 실행하며 child exit 0과 outcome을 검사한다. |

### 수정 파일

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

`tests/integration/sensor_stream/session/repro_boundary_before.c`는 시작 시 untracked 입력이었으며 위 정식 test로 완전히 대체되어 commit 대상에 남기지 않았다.

### 검증 결과

macOS clean `/tmp` build 최종 결과:

- Plain: 39/39 PASS — Foundation 5, TCP 5, result policy 6, WAV 4, BZip 6, Stream 5, Mock 8.
- UBSan: 39/39 PASS, diagnostic 없음.
- `-Wall -Wextra -Wpedantic`: build PASS 및 39/39 PASS.
- 첫 병렬 3-mode 실행에서 기존 `SNS-STR-006` queue timing test가 Plain 1회 실패했으나, 부하를 제거한 clean sequential 최종 실행에서 TCP 5/5 및 전체 39/39 PASS했다.
- ASan: `NOT_PERFORMED`. `detect_leaks=1`은 이 macOS runtime에서 unsupported로 본문 전에 abort했고, leak 옵션 제거 후 동일 `SNS-STR-002-unquoted-wire-quirk` process가 출력 없이 CTest 15초 timeout됐다. UBSan과 Docker arm64 ASan+UBSan으로 보완했다.

Ubuntu 22.04 arm64 Docker (`savvy-foundation-test:ubuntu22.04-arm64-v1`, `--network none`, source read-only mount) 최종 결과:

- 환경: Ubuntu 22.04.5 LTS, `aarch64`/64-bit, CMake 3.22.1, GCC 11.4.0, GNU ld 2.38.
- Plain: 43/43 PASS — Foundation 9, TCP 5, result policy 6, WAV 4, BZip 6, Stream 5, Mock 8.
- UBSan: 43/43 PASS, diagnostic 없음.
- ASan+UBSan finding tests: result policy 6/6, production session boundary 1/1, mock response 8/8 PASS.
- sanitizer arm64 emulation에서 isolation child의 2초 reap limit이 한 번 초과돼 bounded child wait를 5초, CTest timeout을 15초로 보정했고 최종 8/8 PASS했다.
- system BZip: `libbz2-dev:arm64=1.0.8-5build1`, `libbz2-1.0:arm64=1.0.8-5build1`; `readelf` NEEDED와 `ldd` 모두 `libbz2.so.1.0` 확인.
- Stream target graph 및 `libsensor_stream_session.a` symbol 검사에서 production test-server dependency/linkage 0.

필수 13 Test ID:

| ID | 결과 |
|---|---|
| `CT-PKT-001` | PASS |
| `CT-PKT-002` | PASS |
| `CT-PKT-003` | PASS |
| `SNS-STR-001` | PASS |
| `SNS-STR-002` | PASS |
| `SNS-STR-003` | PASS |
| `SNS-STR-004` | PASS |
| `SNS-STR-005` | PASS |
| `SNS-STR-006` | PASS |
| `SNS-STR-007` | PASS |
| `SNS-STR-008` | PASS |
| `SNS-STR-009` | PASS |
| `SNS-STR-010` | PASS |

추가 CTest는 boundary 1개와 mock response 8개, 총 9개다. 최종 전체 CTest count는 Docker real-IPC build 기준 43개다.

### Ownership와 scope

- session별 encode buffer, TCP worker/socket/queue, result policy ownership은 그대로 유지했다.
- 기존 timeout 1000/3000ms, queue depth/정책, retry 정책, wire command, JSON field는 변경하지 않았다.
- Allowed path violation 0, Foundation/contract/root CMake/root `SESSION_RESULT.md`/다른 session diff 0.
- durable queue 0, disk queue 0, BZip raw fallback 0, production test-server linkage 0.
- 원본 Review SHA-256 시작/종료: `48b01c72677aae9380a86b96883753809e038bb6968dfd7845a4b896d34f220d` / 동일. Review 파일은 수정·stage·commit하지 않았다.

### Exact SHA Ledger

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
See the fix completion output.

REREVIEW_TARGET_SHA:
See the fix completion output.
```

```text
RV1106_CROSS_BUILD: NOT_PERFORMED
RV1106_BOARD_RUNTIME: NOT_PERFORMED
HARDWARE_QA: NOT_PERFORMED
CODEX_REREVIEW: NOT_STARTED
MERGE_READY: NO
```
