# CC-SENSOR-W1-INTEGRATION Result

SESSION_ID:
CC-SENSOR-W1-INTEGRATION

FINAL_STATE:
INTEGRATION_FIX_REQUIRED

이 문서는 Wave 1 feature 병합과 통합 검증에서 실제로 확인된 차단 상태를 기록한다. 최종 성공, 병합 준비 또는 production 준비를 선언하지 않는다.

## 1. Exact SHA

```text
BASE_SHA:
07809cb1f3f2b86a8e92ade661c48cb3adb97b52

STARTING_HEAD_SHA:
07809cb1f3f2b86a8e92ade661c48cb3adb97b52

SENSOR_CORE_BRANCH_HEAD:
1b8edc47126c0695a8bdb9834a656e29b0e688fe

SENSOR_STREAM_BRANCH_HEAD:
91cf4f2056d674e510573f905a9c8b50546446f1

SENSOR_CORE_MERGE_SHA:
defe17f26f9321023a50926212ab170223aec32f

SENSOR_STREAM_MERGE_SHA:
2ccb0d0bccb3d456680d7ca4fe1a3d82d6ca303b

INTEGRATION_PRODUCTION_HEAD:
2ccb0d0bccb3d456680d7ca4fe1a3d82d6ca303b
```

Git ancestry 재확인 결과 Sensor Core와 Sensor Stream branch HEAD는 모두 integration production HEAD의 ancestor다.

## 2. Merge 및 scope 결과

```text
MERGE_CONFLICTS:
0

ACTUAL_CHANGED_FILE_COUNT:
78

ALLOWED_PATH_VIOLATIONS:
0

FOUNDATION_CHANGES:
0

CONTRACT_CHANGES:
0

ROOT_CMAKE_CHANGES:
0

OTHER_SESSION_CHANGES:
0
```

두 feature는 검증된 exact SHA를 Core, Stream 순서로 `--no-ff` 병합했다. Production source, test source, Foundation, contract 및 merge commit은 이 후속 문서화 작업에서 수정하지 않았다.

## 3. Manifest 결과

```text
MANIFEST_RESULT:
파일 hash 및 내부 대상 파일 8개 일치
```

- `contracts/contract-manifest.sha256` 파일 SHA-256: `a69536c286839c97e05ed7f54b5834d843f94eae4a9221ad6213de93d268fa6e`
- `third_party/DEPENDENCY_MANIFEST.sha256` 파일 SHA-256: `9934277d3a8d1dabd1c2632d3501743f8d2a57218c6dd6f3635b2b3844296ad2`
- Contract manifest 대상 5개와 dependency manifest 대상 3개를 실제 파일과 대조했고 모두 일치했다.

## 4. macOS 결과

```text
MACOS_RESULT:
Debug 54/54
UBSan 54/54
Release tests-off root + 11 modules 성공
```

- Foundation 5개, Sensor Core 15개, Sensor Stream feature 26개, mock response 8개를 clean out-of-tree Debug와 UBSan에서 실행했다.
- Root와 11개 production module의 Release tests-off 빌드가 완료됐고 각 빌드의 등록 CTest는 0개였다.
- Core MGR IPC, health, TCP 8141 lifecycle/fd test를 각각 3회 반복 실행했다.
- Production archive의 mock/test symbol과 target은 0이었다.
- BZip은 macOS SDK의 system `libbz2.1.0`에 연결됐다.
- Result policy archive는 Foundation의 `savvy_data_result_parse()`와 `savvy_data_result_is_normal()`을 consumer symbol로 사용했다.

## 5. Docker Ubuntu 22.04 arm64 결과

고정 local image `savvy-foundation-test:ubuntu22.04-arm64-v1`을 `--pull=never`, `--network none`, source read-only mount로 사용했다.

```text
DOCKER_ARM64_RESULT:
Debug modules 58/58
Real MGR IPC 6/6
Release tests 45/45
Release tests-off root + 11 modules 성공
UBSan modules 58/58
Real MGR IPC UBSan 6/6
```

- 환경: Ubuntu 22.04, aarch64/64-bit, CMake 3.22.1, GCC 11.4.0.
- `libbz2-dev`와 `libbz2-1.0`은 `1.0.8-5build1`이며 test binary의 `NEEDED`, `ldd`, link command에서 system `libbz2.so.1.0`을 확인했다.
- Production Stream archive의 mock server symbol은 0이었다.
- DataResult consumer/provider symbol 연결을 arm64 binary에서도 확인했다.
- Debug 및 UBSan lifecycle/fd 검증은 성공했다.

## 6. Sanitizer 및 lifecycle 상태

```text
ASAN_RESULT:
FAILED — SNS-STR-008-fd-leak 비결정 실패 재현

TSAN_RESULT:
NOT_RUN — ASan 결함 확인 후 중단

FD_RESULT:
INTEGRATION_FIX_REQUIRED

THREAD_RESULT:
Debug/UBSan lifecycle 검증 성공
macOS 직접 OS thread-count는 NOT_AVAILABLE
```

Docker arm64 ASan 전체 행렬에서 Foundation 9개와 Sensor Core 15개는 LeakSanitizer를 포함해 성공했다. 이후 TCP 8141의 `SNS-STR-008-fd-leak`가 실패하여 나머지 전체 ASan 행렬과 TSan은 중단했다.

## 7. 실패 상세

```text
FAILING_TEST:
SNS-STR-008-fd-leak

RELATED_FILE:
tests/unit/sensor_stream/tcp_channel/test_tcp_channel.c

RELATED_LINE:
266

INTRODUCED_BY_SHA:
212010f5ab62571aa66e72b12b8eea0ed37df944

INTEGRATION_SHA:
2ccb0d0bccb3d456680d7ca4fe1a3d82d6ca303b
```

재현 결과:

- 최초 전체 ASan 행렬에서 1회 실패했다.
- 분리 재실행에서는 3회 성공했다.
- fresh ASan 20회 stress에서는 1~4회 성공 후 5회째 동일 실패가 재현됐다.

현재 증거에 기반한 분석:

- fd 비교용 `fd_probe_after`가 echo server thread의 `pthread_join()`보다 먼저 수행된다.
- ASan 환경의 thread scheduling에 따라 server-side accepted fd가 아직 닫히지 않은 상태에서 다음 process-wide fd 번호가 비교될 수 있다.
- 현재 증거는 production fd leak보다는 committed test의 동기화·결정성 결함에 더 부합한다.
- 다만 production fd leak이 없다고 최종 확정한 것은 아니다.

```text
PRODUCTION_FD_LEAK:
NOT_CONFIRMED

COMMITTED_TEST_DETERMINISM_DEFECT:
REPRODUCED

FINAL_DISPOSITION:
INTEGRATION_FIX_REQUIRED
```

## 8. 재현 명령

```bash
docker run --rm --pull=never \
  --platform linux/arm64 --network none \
  --mount type=bind,src="$PWD",dst=/src,readonly \
  savvy-foundation-test:ubuntu22.04-arm64-v1 sh -euc '
cmake -S /src/src/platform/linux/tcp_8141 \
  -B /tmp/asan-tcp \
  -DSAVVY_SENSOR_STREAM_BUILD_TESTS=ON \
  -DCMAKE_C_FLAGS="-fsanitize=address -fno-omit-frame-pointer" \
  -DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address
cmake --build /tmp/asan-tcp --parallel 4
for i in $(seq 1 20); do
  ASAN_OPTIONS=abort_on_error=1:detect_leaks=1 \
    /tmp/asan-tcp/_tests_tcp_8141/test_tcp_channel 008
done
'
```

## 9. 검증 경계

```text
RV1106_CROSS_BUILD:
NOT_PERFORMED

RV1106_BOARD_RUNTIME:
NOT_PERFORMED

HARDWARE_QA:
NOT_PERFORMED
```

Root CMake에는 Wave 1 feature 통합 executable wiring이 없으므로 module-local CMake entry point를 사용했다. Root 통합 executable은 `NOT_AVAILABLE`이다.

## 10. 최종 처분

```text
PRODUCTION_SOURCE_CHANGED:
NO

TEST_SOURCE_CHANGED:
NO

MERGE_COMMITS_CHANGED:
NO

FINAL_STATE:
INTEGRATION_FIX_REQUIRED
```

다음 작업은 integration production HEAD `2ccb0d0bccb3d456680d7ca4fe1a3d82d6ca303b`에서 별도 integration fix branch를 생성해 `SNS-STR-008` test ordering과 fd lifecycle 판정을 수정하고, 독립 Codex review와 전체 integration 재검증을 수행하는 것이다.
