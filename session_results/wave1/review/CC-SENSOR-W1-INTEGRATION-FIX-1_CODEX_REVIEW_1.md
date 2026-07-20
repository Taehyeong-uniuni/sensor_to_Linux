# CC-SENSOR-W1-INTEGRATION-FIX-1 — Codex 독립 Fix Review 1

## 1. Review identity

```text
REVIEW_SESSION_ID:
CC-SENSOR-W1-INTEGRATION-FIX-1-CODEX-REVIEW-1

SOURCE_SESSION_ID:
CC-SENSOR-W1-INTEGRATION-FIX-1

BASE_INTEGRATION_PRODUCTION_SHA:
2ccb0d0bccb3d456680d7ca4fe1a3d82d6ca303b

INTEGRATION_RESULT_COMMIT_SHA:
1936517d2bafc099ffd8864c53f8fe39ef9bac7d

INTEGRATION_LEDGER_COMMIT_SHA:
1e2e97ae1590446dc3211e6df5d53a54240c9b20

FIX_START_HEAD:
2ccb0d0bccb3d456680d7ca4fe1a3d82d6ca303b

FIX_IMPLEMENTATION_SHA:
3a62d737111c611399cbdfd4e368a5b27150a18e

FIX_RESULT_REPORT_SHA:
a9d08b029ca10faf5197ad6be91ff9ca7b42fafd

FIX_LEDGER_COMMIT_SHA:
a4e1887959b4001e41ddc110d9d7e9519b692f0f

REVIEW_START_HEAD:
a4e1887959b4001e41ddc110d9d7e9519b692f0f

REVIEW_TARGET_SHA:
3a62d737111c611399cbdfd4e368a5b27150a18e

REVIEWED_HEAD_SHA:
3a62d737111c611399cbdfd4e368a5b27150a18e
```

Worktree identity gate 결과:

```text
pwd -P:
/Users/juganghyeon/Desktop/uniuni/projects/worktrees/sensor-w1-fd-lifecycle-test

git rev-parse --show-toplevel:
/Users/juganghyeon/Desktop/uniuni/projects/worktrees/sensor-w1-fd-lifecycle-test

branch:
fix/sensor-w1-fd-lifecycle-test

starting HEAD:
a4e1887959b4001e41ddc110d9d7e9519b692f0f

tracked staged/modified files at review start:
0
```

모든 authoritative SHA는 Git object에서 40자리 full SHA로 확인했다. 다음 lineage와 ancestry가 모두 성립했다.

```text
2ccb0d0bccb3d456680d7ca4fe1a3d82d6ca303b
→ 3a62d737111c611399cbdfd4e368a5b27150a18e
→ a9d08b029ca10faf5197ad6be91ff9ca7b42fafd
→ a4e1887959b4001e41ddc110d9d7e9519b692f0f

2ccb0d0bccb3d456680d7ca4fe1a3d82d6ca303b
→ 1936517d2bafc099ffd8864c53f8fe39ef9bac7d
→ 1e2e97ae1590446dc3211e6df5d53a54240c9b20
```

Implementation 이후 review 시작 HEAD까지의 변경은 기존 Fix 결과 문서와 Fix SHA ledger 두 파일뿐이었다. 미검토 code/test 변경은 없었다.

## 2. 변경 범위

`2ccb0d0bccb3d456680d7ca4fe1a3d82d6ca303b..3a62d737111c611399cbdfd4e368a5b27150a18e`의 code/test 변경은 다음 한 파일뿐이다.

```text
CHANGED_CODE_FILES:
1

M tests/unit/sensor_stream/tcp_channel/test_tcp_channel.c

DIFF_STAT:
1 file changed, 17 insertions(+), 3 deletions(-)

ALLOWED_PATH_VIOLATIONS:
0

PRODUCTION_CHANGES:
0

FOUNDATION_CHANGES:
0

CONTRACT_CHANGES:
0

ROOT_CMAKE_CHANGES:
0
```

`src/**`, `include/**`, `contracts/**`, `third_party/**`, root `CMakeLists.txt`, `CMakePresets.json`, `cmake/**`, `tests/verification/**`, `tools/**`, `SESSION_RESULT.md`의 변경은 모두 0이다.

## 3. SNS-STR-008 code review

### 3.1 Baseline 순서

- `make_listener()`가 동기적으로 만든 listener fd는 baseline 전부터 존재한다. 이 fd는 final probe가 끝날 때까지 의도적으로 계속 열려 있어 두 probe에 대칭적으로 포함된다. 현재 `dup(0)` low-water-mark 방식에서 listener를 baseline 전에 닫힌 상태로 두고 final 전에 닫으면, 더 낮은 fd 번호의 hole 때문에 production socket leak을 놓칠 수 있으므로 이 고정 fixture의 대칭 유지가 leak 검출 능력을 보존한다.
- `fd_probe_before`는 `pthread_create(echo_accept_thread)`보다 먼저 실행된다(`test_tcp_channel.c:251-255`). 따라서 echo thread 시작 과정의 일시적 process-wide fd 사용과 baseline probe가 경합하지 않는다.
- baseline 전에는 echo thread, accepted socket, TCP channel worker/socket이 생성되지 않는다. baseline 시점의 유일한 test-owned fd는 양쪽 probe에 동일하게 포함되는 고정 listener다.
- 50회 create/start/submit/destroy, listener의 lifetime, echo protocol 의미는 바뀌지 않았다.

### 3.2 Final probe 순서

- 각 TCP channel은 `sensor_tcp_channel_destroy()`로 먼저 정리된다. Production `sensor_tcp_channel_stop()`은 queue cancel 후 worker를 join하고, `worker_main()`은 반환 전에 열려 있는 `sockfd`를 닫는다(`tcp_channel.c:388-405,577-603`).
- echo thread는 정확히 50회의 accept loop를 유지하며, 각 accepted fd를 thread 내부에서 `close(fd)`한다(`test_tcp_channel.c:220-236`).
- `pthread_join(th, NULL)`은 final fd probe 전에 실행되고 반환값은 `CHECK(join_rc == 0, ...)`로 검증된다(`test_tcp_channel.c:278-283`). Join 실패는 조용히 무시되지 않는다.
- join이 성공하면 echo thread와 그 accepted fd는 final probe보다 먼저 정리된다. 고정 listener fd만 baseline과 동일하게 열려 있고 final probe 뒤 `close(ctx.l.listen_fd)`로 정리된다.
- 기존 join은 원래도 함수 끝에서 실행됐으므로, 이동으로 새로운 blocking 조건을 만들지 않고 final probe와 last accepted-fd close 사이의 race만 제거한다.

### 3.3 Leak 검출 능력과 안전성

- Production-owned fd를 test가 직접 닫지 않는다. 기존 `sensor_tcp_channel_destroy(ch)` 경로가 그대로 유지된다.
- `fd_probe_after == fd_probe_before` assertion과 50회 loop는 유지됐다.
- fd 허용 오차, retry, polling, 추가 sleep, sanitizer 비활성화, warning 억제는 추가되지 않았다.
- 기존 연결·송수신·종료·50회 lifecycle 의미는 유지됐다. 새 assertion은 echo thread join 성공 여부만 추가로 검증한다.

```text
ROOT_CAUSE_REVIEW:
CONFIRMED
- pre-fix baseline은 echo thread 생성 이후여서 thread startup의 일시적 fd와 경합 가능
- pre-fix final probe는 echo thread join 이전이어서 last accepted-fd close와 경합 가능
- production fd leak의 증거가 아니라 committed test fixture의 process-wide fd probe 순서 결함

FIX_SAFETY_REVIEW:
PASS

SNS_STR_008_STATUS:
RESOLVED

SNS_STR_008_FIX_VERDICT:
PASS
```

새 스레드 시작 과정이 정확히 어떤 ASan 내부 파일을 여는지는 committed artifact만으로 특정하지 않았다. 그러나 thread 생성 전 baseline과 join 후 final probe라는 ordering 자체가 외부 runtime의 구체적 구현에 의존하지 않고 해당 race를 제거한다.

## 4. Targeted 독립 검증

기존 local image `savvy-foundation-test:ubuntu22.04-arm64-v1`(`linux/arm64`, image id `sha256:73c8a9709607d1910231efb4648510e4d72052072629901fa28fd5c9f39753e7`)만 사용했다. `--pull=never`, `--network none`, source read-only bind mount, fresh container `/tmp` build를 사용했고 image build나 dependency download는 하지 않았다.

```text
TARGETED_DEBUG_RESULT:
20/20 PASS

TARGETED_RELEASE_RESULT:
20/20 PASS

TARGETED_UBSAN_RESULT:
20/20 PASS
UBSAN_DIAGNOSTICS: 0

TARGETED_ASAN_TOTAL:
100

TARGETED_ASAN_PASS:
100

TARGETED_ASAN_FAIL:
0

TARGETED_ASAN_RESULT:
100/100 PASS

ASAN_DIAGNOSTICS:
0

FD_MISMATCH:
0

HANG_COUNT:
0

EXTERNAL_INTERRUPTION:
0
```

Sanitizer는 diagnostic 발생 시 즉시 non-zero로 종료하도록 `halt_on_error=1`을 사용했고 ASan은 leak detection을 활성화했다.

## 5. TCP 8141 전체 committed suite

SNS-STR-006~010의 실제 CTest 5개를 네 구성에서 실행했다. ASan 반복은 targeted와 같은 fresh ASan build를 사용했다.

```text
TCP_8141_DEBUG_RESULT:
5/5 PASS

TCP_8141_RELEASE_RESULT:
5/5 PASS

TCP_8141_UBSAN_RESULT:
5/5 PASS, diagnostics 0

TCP_8141_ASAN_RESULT:
5/5 PASS, diagnostics 0

TCP_8141_ASAN_REPEAT_RESULT:
20/20 suite runs PASS
100/100 individual tests PASS
fd mismatch 0
hang 0
diagnostics 0
```

## 6. macOS 전체 검증

Apple clang 17.0.0, arm64 macOS에서 source 밖의 fresh `/tmp` build를 사용했다. 총 38개 검증 구성을 독립 집계했다.

```text
MACOS_CONFIGURE_RESULT:
38/38 PASS

MACOS_BUILD_RESULT:
38/38 PASS

MACOS_TEST_RESULT:
Foundation Debug: 5/5
Sensor Core Debug: 15/15
Sensor Stream Debug: 34/34
Debug aggregate: 54/54
UBSan aggregate: 54/54
Release tests-off root + 11 modules: 12/12 configure/build PASS

MACOS_WARNING_COUNT:
2 emissions, 1 unique diagnostic
- Debug stream-session link: 1
- UBSan stream-session link: 1

MACOS_WARNING_DIAGNOSTIC:
ld: warning: ignoring duplicate libraries: '../_savvy_core/libsavvy_core.a', '../_savvy_protocol/libsavvy_protocol.a'

MACOS_SANITIZER_DIAGNOSTICS:
0

MACOS_RESULT:
PASS_WITH_LOW_FINDING
```

동일 warning은 base SHA의 fresh macOS Debug build에서도 재현됐다. Test 결과에는 영향을 주지 않았으며 finding `SENSOR-W1-INT-FIX-R1-004`로 기록한다.

## 7. Docker Linux arm64 전체 검증

검증 구성 수를 기존 보고에서 복사하지 않고 다음과 같이 다시 계산했다.

```text
Foundation Debug                                      1
Foundation Release                                    1
Sensor Debug modules (Core 6 + Stream 5 + mock 1)    12
Real MGR IPC Debug                                    1
Sensor Release tests-on                              12
Release tests-off root + 11 modules                  12
Sensor UBSan (root + Core 6 + Stream 5 + mock 1)     13
Real MGR IPC UBSan                                    1
Sensor ASan (root + Core 6 + Stream 5 + mock 1)      13
Real MGR IPC ASan                                     1
                                                       --
TOTAL                                                 67
```

`tools/mock_mgr`는 Real MGR IPC의 필수 prerequisite로 각 sanitizer 구성에서 fresh build했으며 composite Real MGR IPC 검증 구성 수에는 별도 중복 가산하지 않았다.

```text
DOCKER_CONFIGURE_RESULT:
67/67 PASS

DOCKER_BUILD_RESULT:
64/67 PASS
3/67 FAIL

DOCKER_ARM64_RESULT:
64/67 validation configurations PASS
3/67 FAIL (Release tests-on: config, mode_state, mgr_ipc)

FOUNDATION_DEBUG:
9/9 PASS

FOUNDATION_RELEASE:
9/9 PASS

SENSOR_DEBUG_MODULES:
Core 15/15 + Stream 34/34 = 49/49 PASS
Foundation 포함 aggregate 58/58 PASS

REAL_MGR_IPC_DEBUG:
6/6 PASS

SENSOR_RELEASE_TESTS_ON:
12/12 configure PASS
9/12 build PASS
3/12 build FAIL
실행 가능한 9개 구성의 test 39/39 PASS

RELEASE_TESTS_OFF_ROOT_AND_11:
12/12 configure/build PASS

SENSOR_UBSAN_MODULES:
13/13 configure/build PASS
58/58 tests PASS
diagnostics 0

REAL_MGR_IPC_UBSAN:
6/6 PASS
diagnostics 0

SENSOR_ASAN_MODULES:
13/13 configure/build PASS
58/58 tests PASS
diagnostics 0

REAL_MGR_IPC_ASAN:
6/6 PASS
diagnostics 0

DOCKER_WARNING_COUNT:
0 (Release -Werror diagnostics는 build error로 별도 집계)
```

## 8. Release tests-on 세 실패의 base/fix 비교

공통 환경과 명령 형태:

```text
IMAGE: savvy-foundation-test:ubuntu22.04-arm64-v1
PLATFORM: linux/arm64
NETWORK: none
COMPILER: GCC 11.4.0
BUILD_TYPE: Release
FLAGS: -O3 -DNDEBUG -Wall -Wextra -Werror -std=c11
TEST OPTION: -DSAVVY_BUILD_TESTS=ON

cmake -S /src/src/features/<module> -B /tmp/<build> \
  -DCMAKE_BUILD_TYPE=Release -DSAVVY_BUILD_TESTS=ON
cmake --build /tmp/<build> -j1
```

Fix는 read-only mount로, base는 `git archive 2ccb0d0...` 스냅샷을 같은 container에 주입해 동일 조건으로 실행했다.

### 8.1 config

```text
FIX configure: PASS
FIX build: FAIL (rc=2)
BASE configure: PASS
BASE build: FAIL (rc=2)
ORIGIN: PRE_EXISTING_AT_BASE
FAILING_TARGET: test_sensor_core_config
```

정확한 대표 compiler diagnostic:

```text
/src/tests/unit/sensor_core/config/test_config_store.c:24:27: error: variable 'device' set but not used [-Werror=unused-but-set-variable]
/src/tests/unit/sensor_core/config/test_config_store.c:15:17: error: unused variable 'cached' [-Werror=unused-variable]
/src/tests/unit/sensor_core/config/test_config_store.c:88:17: error: variable 'raw' set but not used [-Werror=unused-but-set-variable]
/src/tests/unit/sensor_core/config/test_config_store.c:71:20: error: variable 'st' set but not used [-Werror=unused-but-set-variable]
/src/tests/unit/sensor_core/config/test_config_store.c:213:27: error: unused variable 'device' [-Werror=unused-variable]
cc1: all warnings being treated as errors
```

`assert()` 안에만 존재하는 호출과 값 사용이 `-DNDEBUG`에서 제거되어 다수의 local variable이 unused가 된다. Warning만 억제하면 Release test assertion과 일부 side-effect 호출도 계속 사라지므로 올바른 fix가 아니다.

### 8.2 mode_state

```text
FIX configure: PASS
FIX build: FAIL (rc=2)
BASE configure: PASS
BASE build: FAIL (rc=2)
ORIGIN: PRE_EXISTING_AT_BASE
FAILING_TARGET: test_sensor_core_mode_state
```

정확한 compiler diagnostic:

```text
/src/tests/unit/sensor_core/mode_state/test_mode_state.c:21:30: error: variable 't' set but not used [-Werror=unused-but-set-variable]
cc1: all warnings being treated as errors
```

`t`의 모든 검증이 `assert()`에만 있어 `-DNDEBUG`에서 사라지는 동일한 Release test harness 문제다.

### 8.3 mgr_ipc

```text
FIX configure: PASS
FIX aggregate build: FAIL (rc=2)
BASE configure: PASS
BASE aggregate build: FAIL (rc=2)
ORIGIN: PRE_EXISTING_AT_BASE
FAILING_TARGETS:
- test_sensor_core_mgr_ipc
- test_sensor_core_mgr_ipc_lifecycle
```

두 target을 각각 명시적으로 빌드해 모두 rc=2를 확인했다. 정확한 대표 compiler diagnostic:

```text
/src/tests/unit/sensor_core/mgr_ipc/test_mgr_ipc_client.c:325:20: error: unused variable 'st' [-Werror=unused-variable]
/src/tests/unit/sensor_core/mgr_ipc/test_mgr_ipc_client.c:411:10: error: variable 'buf' set but not used [-Werror=unused-but-set-variable]
/src/tests/unit/sensor_core/mgr_ipc/test_mgr_ipc_client.c:926:14: error: 'send_thread_main' defined but not used [-Werror=unused-function]
/src/tests/unit/sensor_core/mgr_ipc/test_mgr_ipc_client.c:516:5: error: 'send_thread' is used uninitialized [-Werror=uninitialized]
/src/tests/unit/sensor_core/mgr_ipc/test_mgr_ipc_client.c:1251:13: error: 'first_thread' may be used uninitialized [-Werror=maybe-uninitialized]
/src/tests/unit/sensor_core/mgr_ipc/test_mgr_ipc_client.c:67:59: error: unused parameter 'client' [-Werror=unused-parameter]
cc1: all warnings being treated as errors
```

여기서도 `assert(pthread_create(...))` 같은 side-effect 호출까지 `-DNDEBUG`에서 제거되므로, 뒤의 `pthread_join()`이 초기화되지 않은 `pthread_t`를 사용한다고 진단된다. 단순 warning 억제는 test 의미를 복구하지 않는다.

### 8.4 영향 분리

```text
SNS-STR-008 fix 자체에 대한 영향:
없음. 세 파일과 해당 CMake target은 fix diff에 없고 base/fix에서 동일 재현.

Sensor Wave 1 integration merge 가능성에 대한 영향:
있음. 필수 Release tests-on module build 3개가 실패해 regression gate를 차단.

추가 integration fix 필요:
YES
```

## 9. Findings

### SENSOR-W1-INT-FIX-R1-001 — Medium

- 제목: `config` Release tests-on target이 `assert`/`NDEBUG` 상호작용으로 빌드되지 않음
- Affected file: `tests/unit/sensor_core/config/test_config_store.c:7,15,24,71,82,88,124-213`
- 발생 commit: base `2ccb0d0bccb3d456680d7ca4fe1a3d82d6ca303b`와 fix `3a62d737111c611399cbdfd4e368a5b27150a18e`에서 관찰. 최초 도입 commit은 bisect하지 않음.
- Base 재현: YES
- 재현 명령: 8.1의 Release tests-on configure/build
- Output: 8.1의 exact diagnostic
- 영향: `test_sensor_core_config` 미빌드, 3개 CTest 미실행. Warning 억제 시 assertion과 side-effect 호출이 사라진 무효한 Release test가 됨.
- Fix 범위 관계: SNS-STR-008 fix와 무관한 pre-existing Sensor Core test defect
- Integration merge 차단: YES
- 최소 수정 범위: config test target에서 Release에도 항상 실행되는 test assertion 정책을 적용한다. `assert()` side effect 의존을 제거하거나 test-only로 `NDEBUG` 제거를 명시한다. Warning 억제는 사용하지 않는다.
- 재검증: 동일 Docker Release tests-on configure/build 후 config 3/3 CTest 실행, 이어서 Debug/UBSan/ASan 회귀

### SENSOR-W1-INT-FIX-R1-002 — Medium

- 제목: `mode_state` Release tests-on target이 `assert` 제거 후 unused-but-set-variable로 빌드되지 않음
- Affected file: `tests/unit/sensor_core/mode_state/test_mode_state.c:8,21-42`
- 발생 commit: base와 fix에서 관찰. 최초 도입 commit은 bisect하지 않음.
- Base 재현: YES
- 재현 명령: 8.2의 Release tests-on configure/build
- Output: 8.2의 exact diagnostic
- 영향: `test_sensor_core_mode_state` 미빌드, 2개 CTest 미실행
- Fix 범위 관계: SNS-STR-008 fix와 무관한 pre-existing Sensor Core test defect
- Integration merge 차단: YES
- 최소 수정 범위: mode_state test assertion을 Release에서도 유지되는 always-on test check로 전환하거나 test-only `NDEBUG` 정책을 명시한다.
- 재검증: 동일 Docker Release tests-on configure/build 및 2/2 CTest, 이어서 Debug/UBSan/ASan 회귀

### SENSOR-W1-INT-FIX-R1-003 — Medium

- 제목: `mgr_ipc` 두 Release tests-on target이 `assert` 제거로 미빌드되고 thread 생성 side effect도 소실됨
- Affected file: `tests/unit/sensor_core/mgr_ipc/test_mgr_ipc_client.c:17,67,251-325,404-516,641-798,907-1463`
- 발생 commit: base와 fix에서 관찰. 최초 도입 commit은 bisect하지 않음.
- Base 재현: YES
- 재현 명령: 8.3의 Release tests-on configure/build 및 두 target 개별 build
- Output: 8.3의 exact diagnostic
- 영향: `test_sensor_core_mgr_ipc`, `test_sensor_core_mgr_ipc_lifecycle` 미빌드, 5개 CTest 미실행. `assert(pthread_create(...))` 제거로 thread handle이 초기화되지 않는 test 의미 결함도 존재.
- Fix 범위 관계: SNS-STR-008 fix와 무관한 pre-existing Sensor Core test defect
- Integration merge 차단: YES
- 최소 수정 범위: mgr_ipc test에서 side-effect 호출을 assertion 밖으로 이동하고 always-on test assertion을 사용하거나, 두 test target에 일관된 test-only `NDEBUG` 정책을 적용한다. Warning 억제는 사용하지 않는다.
- 재검증: 두 target의 Release build, mgr_ipc 5/5 CTest, Real MGR IPC 6/6, Debug/UBSan/ASan 회귀

### SENSOR-W1-INT-FIX-R1-004 — Low

- 제목: macOS `stream-session` test link가 동일 static library를 중복 전달함
- Affected file: `src/features/stream/CMakeLists.txt:82-89`
- 발생 commit: base와 fix에서 동일 재현. 최초 도입 commit은 bisect하지 않음.
- Base 재현: YES
- 재현 명령: macOS에서 `cmake -S src/features/stream -B /tmp/stream -DCMAKE_BUILD_TYPE=Debug` 후 verbose build
- Compiler/linker output: `ld: warning: ignoring duplicate libraries: '../_savvy_core/libsavvy_core.a', '../_savvy_protocol/libsavvy_protocol.a'`
- 영향: 현재 macOS Debug/UBSan tests는 모두 통과하며 runtime 영향은 재현되지 않음
- Fix 범위 관계: SNS-STR-008 fix와 무관한 pre-existing link graph 문제
- Integration merge 차단: NO
- 최소 수정 범위: `sensor_stream_session`의 direct dependency와 sibling target의 transitive PUBLIC dependency를 정리해 최종 link line의 중복만 제거
- 재검증: macOS Debug/UBSan `stream-session` verbose link warning 0 및 5/5 CTest

```text
NEW_FINDINGS:
4

CRITICAL:
0

HIGH:
0

MEDIUM:
3

LOW:
1
```

## 10. Lifecycle 및 resource 판정

```text
ASAN_RESULT:
PASS_WITHIN_EXECUTED_SCOPE
- targeted 100/100
- TCP 8141 20/20 suite repeats
- Docker Sensor 58/58
- Docker Real MGR IPC 6/6
- diagnostics 0

UBSAN_RESULT:
PASS_WITHIN_EXECUTED_SCOPE
- targeted 20/20
- TCP 8141 5/5
- macOS 54/54
- Docker Sensor 58/58
- Docker Real MGR IPC 6/6
- diagnostics 0

TSAN_RESULT:
NOT_RUN

FD_RESULT:
targeted ASan fd mismatch 0/100
TCP 8141 ASan repeat fd mismatch 0/20 suite runs

THREAD_RESULT:
echo thread join CHECK 전 실행 통과
독립 실행 hang 0
관찰된 미조인 test thread 0

LIFECYCLE_RESULT:
PASS_WITHIN_EXECUTED_SCOPE

PRODUCTION_FD_LEAK:
NOT_REPRODUCED

COMMITTED_TEST_DETERMINISM_DEFECT:
RESOLVED
```

`NOT_REPRODUCED`는 이번 검증 범위의 결과이며 production leak의 절대 부재를 선언하지 않는다.

## 11. Foundation, contract, manifest

```text
FOUNDATION_CHANGES:
0

CONTRACT_CHANGES:
0

ROOT_CMAKE_CHANGES:
0

contracts/contract-manifest.sha256:
a69536c286839c97e05ed7f54b5834d843f94eae4a9221ad6213de93d268fa6e

third_party/DEPENDENCY_MANIFEST.sha256:
9934277d3a8d1dabd1c2632d3501743f8d2a57218c6dd6f3635b2b3844296ad2

MANIFEST_RESULT:
PASS
```

두 manifest 파일 자체 hash가 기대값과 일치했고, manifest 내부의 모든 contract 및 cJSON 대상 파일도 `shasum -a 256 -c`로 PASS했다.

## 12. Fix 결과 문서 정확성

```text
FIX_RESULT_DOCUMENT_ACCURACY:
MOSTLY_ACCURATE_WITH_MINOR_OMISSIONS
```

일치한 항목:

- Exact SHA, lineage, 구현 변경 파일 수와 scope
- 두 fd probe의 ordering root cause와 최소 변경 설명
- targeted Debug/Release/UBSan 20/20, ASan 100/100
- TCP 8141 네 구성과 ASan 20회 반복
- macOS 38개 구성의 build/test 결과와 54/54 집계
- Docker Debug/UBSan/ASan 58/58, Real MGR IPC 6/6
- Release tests-on 실패 모듈 3개와 compiler warning 종류
- manifest hash, RV1106/board/hardware 미수행, production fd leak `NOT_REPRODUCED`

보완이 필요한 항목:

- 기존 Fix 결과 문서는 Release 실패를 기존 결함이라고 분류했지만 base SHA 동일 조건 재현 결과를 제시하지 않았다. 이번 review에서 세 모듈 모두 base/fix 동일 재현으로 보강했다.
- macOS Debug 및 UBSan의 동일 duplicate-library linker warning 2회가 기존 Fix 결과 문서에 기록되지 않았다. Test 결과에는 영향이 없고 Low finding이다.
- ASan runtime이 baseline 경합 시 정확히 어떤 내부 파일을 여는지는 추정 영역이다. Ordering 결함 및 fix 효과는 독립 검증됐지만 runtime 내부 원인을 확정 표현해서는 안 된다.

```text
ULTRAGOAL_STATUS_ASSESSMENT:
12/13 complete, final-report story in_progress인 local tool state를 확인.
이 상태는 Git artifact, code review, test 결과와 분리했으며 verdict 근거로 사용하지 않음.
```

## 13. 검증 한계

```text
RV1106_CROSS_BUILD:
NOT_PERFORMED

RV1106_BOARD_RUNTIME:
NOT_PERFORMED

HARDWARE_QA:
NOT_PERFORMED

TSAN:
NOT_RUN
```

Targeted/full regression은 요청된 macOS host 및 기존 Docker Linux arm64 image 범위에서 수행했다. Release tests-on 세 모듈은 build 단계에서 실패했으므로 해당 10개 CTest(config 3 + mode_state 2 + mgr_ipc 5)는 실행되지 못했다.

## 14. 최종 판정

Targeted fix와 branch merge 가능성을 분리한다.

```text
SNS_STR_008_STATUS:
RESOLVED

SNS_STR_008_FIX_VERDICT:
PASS

INTEGRATION_REGRESSION_STATUS:
FAILED

OVERALL_VERDICT:
BLOCKED

MERGE_CANDIDATE:
NO
```

판정 이유:

- SNS-STR-008의 baseline/final ordering은 코드상 결정적으로 바뀌었고 요구된 targeted 및 TCP 8141 stress가 모두 통과했다.
- Critical 0, High 0, scope violation 0, production/Foundation/contract/root CMake 변경 0, sanitizer diagnostic 0, manifest PASS다.
- 그러나 필수 Docker Release tests-on build가 `config`, `mode_state`, `mgr_ipc`에서 실제 실패한다. 모두 fix가 새로 유발한 결함은 아니지만 merge gate는 branch의 현재 전체 검증 상태를 기준으로 하므로 무시할 수 없다.

```text
NEXT_STEP:
별도 Sensor Wave 1 integration fix에서 SENSOR-W1-INT-FIX-R1-001~003을 최소 범위로 수정한다.
세 Release tests-on target을 모두 build/test한 뒤 Docker 전체 67개 구성과 macOS 필수 matrix를 재검증한다.
그 전에는 integration/wave1에 merge하지 않는다.
Low finding SENSOR-W1-INT-FIX-R1-004는 비차단 후속 정리로 분리 가능하다.
```
