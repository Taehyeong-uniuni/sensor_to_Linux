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
