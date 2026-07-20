# CC-SENSOR-W1-INTEGRATION-FIX-2 — Wave 1 Integration Fix 2 결과

## 1. 세션 식별

```text
SESSION_ID:
CC-SENSOR-W1-INTEGRATION-FIX-2

FOUNDATION_BASE_SHA:
07809cb1f3f2b86a8e92ade661c48cb3adb97b52

BASE_INTEGRATION_PRODUCTION_SHA:
2ccb0d0bccb3d456680d7ca4fe1a3d82d6ca303b

FIX_1_IMPLEMENTATION_SHA:
3a62d737111c611399cbdfd4e368a5b27150a18e

FIX_1_RESULT_REPORT_SHA:
a9d08b029ca10faf5197ad6be91ff9ca7b42fafd

FIX_1_LEDGER_COMMIT_SHA:
a4e1887959b4001e41ddc110d9d7e9519b692f0f

FIX_1_REVIEW_ARTIFACT_SHA:
1e7daaf8f8f507f9d5d42d86ba5556e2c3a7a561

FIX_1_REVIEW_LEDGER_SHA:
16a0bb071133f427af32f8216e9ba66729f54444

FIX_2_START_HEAD:
16a0bb071133f427af32f8216e9ba66729f54444

ORIGINAL_FIX_2_HEAD_ON_CONTINUATION:
89e73bf018bae96ce51c6a9bbbd3b23133e12369

SYNC_RELATION:
CURRENT_CONTAINS_REVIEW

SYNC_RESULT:
NO_FAST_FORWARD_REQUIRED

FIX_2_BASE_SHA_ON_CONTINUATION:
89e73bf018bae96ce51c6a9bbbd3b23133e12369

FIX_2_INITIAL_IMPLEMENTATION_SHA:
6538b8de5bd48d04b7e01c65ac8a4ce1140ec5eb

FIX_2_IMPLEMENTATION_SHA:
5910de88189b0194080c4cfe346ce2157d04ccdd
```

최초 구현 세션의 worktree identity gate에서 `pwd -P`, `git rev-parse --show-toplevel`, `git branch --show-current`가 모두 `.../worktrees/sensor-w1-release-tests-on` / `fix/sensor-w1-release-tests-on`와 일치했다. 최초 `FIX_2_START_HEAD`는 당시 `fix/sensor-w1-fd-lifecycle-test^{commit}`의 Git 조회 결과와 정확히 일치했다(`16a0bb071133f427af32f8216e9ba66729f54444`). 작업 시작 시 tracked staged/modified 파일은 0개였다.

2026-07-19 continuation은 `89e73bf018bae96ce51c6a9bbbd3b23133e12369`에서 시작했다. 이 SHA가 Fix 1 Review ledger를 이미 포함하는 ancestry임을 직접 확인했고, 사용자 지시에 따라 기존 `BLOCKED_SYNC_SCOPE_VIOLATION`을 해제했다. fast-forward, reset, rebase, cherry-pick은 실행하지 않았다. 기존 구현을 재검토하면서 남아 있던 두 warning-only `(void)` 사용을 실제 test 의미 검증으로 복구한 보완 implementation commit이 `5910de88189b0194080c4cfe346ce2157d04ccdd`다.

Fix 1 Review 증거는 Git에서 직접 조회했다(사용자 재질의 없음): `FIX_1_REVIEW_ARTIFACT_SHA`, `FIX_1_REVIEW_LEDGER_SHA` 모두 `git log -1` 결과와 일치했다. Review 문서 내용을 직접 읽어 확인: `SNS_STR_008_STATUS: RESOLVED`, `SNS_STR_008_FIX_VERDICT: PASS`, `OVERALL_VERDICT: BLOCKED`, `MERGE_CANDIDATE: NO`, `CRITICAL: 0`, `HIGH: 0`, `MEDIUM: 3`, `LOW: 1`, finding `SENSOR-W1-INT-FIX-R1-001~004` 존재 확인.

SHA lineage 6단계(`BASE_INTEGRATION_PRODUCTION_SHA → FIX_1_IMPLEMENTATION_SHA → FIX_1_RESULT_REPORT_SHA → FIX_1_LEDGER_COMMIT_SHA → FIX_1_REVIEW_ARTIFACT_SHA → FIX_1_REVIEW_LEDGER_SHA → FIX_2_START_HEAD`)를 `git merge-base --is-ancestor`로 모두 확인했다. 전부 OK.

Fix 1 implementation 이후 `FIX_2_START_HEAD`까지의 변경은 문서 4개뿐이었다(Fix 1 결과 문서, Fix 1 SHA ledger, Codex Review 문서, Codex Review SHA ledger). `tests/unit/sensor_stream/tcp_channel/test_tcp_channel.c`를 포함한 미검토 code/test 변경은 0건이었다.

## 2. 공통 원인

Release build의 `-DNDEBUG`에 의해 `assert()`가 `((void)0)`으로 완전히 사라진다. 이로 인해 두 종류의 결함이 동시에 발생했다.

1. **컴파일러가 잡아내는 결함**: `assert()` 안에서만 참조되던 지역 변수가 Release에서 "unused"/"set but not used"/"uninitialized"가 되어 `-Werror`에 의해 build가 실패한다.
2. **컴파일러가 잡아내지 못하는 결함**: `assert()` 안에만 있던 side-effect 호출(예: `pthread_mutex_init`, `pthread_create`, store `_init`/`_load_cached` 호출)은 이름 있는 변수를 남기지 않는 경우 아무 경고 없이 Release에서 통째로 사라진다. 이 경우 해당 초기화가 전혀 실행되지 않은 채 이후 코드가 그 상태를 사용하는 잠재적 UB가 생긴다.

세 파일 모두에서 재현 전 리뷰가 제시한 대표 진단 목록은 "대표 예시"였을 뿐 전체 목록이 아니었다. 실제 Docker Release 재현 결과 실제 진단 수는 리뷰 문서보다 훨씬 많았다(§3 참고).

## 3. 수정 전 재현 (G004)

기존 local Docker image `savvy-foundation-test:ubuntu22.04-arm64-v1`(`linux/arm64`)에서 `--pull=never --network none`, source read-only mount, 컨테이너 내부 fresh `/tmp` build로 재현했다.

```text
CONFIG_CONFIGURE_RESULT: PASS
CONFIG_BUILD_RESULT: FAIL (rc=2)
CONFIG_FAILING_TARGET: test_sensor_core_config
CONFIG_REAL_DIAGNOSTIC_COUNT: 22 (리뷰의 "대표" 5건보다 많음 - test_005_malformed_config_device의 거의 모든 지역 변수가 unused로 진단됨)
대표: test_config_store.c:24:27 'device' set but not used [-Werror=unused-but-set-variable]
대표: test_config_store.c:15:17 'cached' unused [-Werror=unused-variable]

MODE_STATE_CONFIGURE_RESULT: PASS
MODE_STATE_BUILD_RESULT: FAIL (rc=2)
MODE_STATE_FAILING_TARGET: test_sensor_core_mode_state
MODE_STATE_REAL_DIAGNOSTIC_COUNT: 1 (정확히 't' 하나 - 리뷰와 일치)
진단: test_mode_state.c:21:30 't' set but not used [-Werror=unused-but-set-variable]

MGR_IPC_CONFIGURE_RESULT: PASS
MGR_IPC_BUILD_RESULT: FAIL (rc=2, 두 target 모두: test_sensor_core_mgr_ipc / test_sensor_core_mgr_ipc_lifecycle)
MGR_IPC_FAILING_TARGETS: test_sensor_core_mgr_ipc, test_sensor_core_mgr_ipc_lifecycle
MGR_IPC_REAL_DIAGNOSTIC_COUNT: 60+ (unused-variable/set-but-not-used/unused-function/unused-parameter/uninitialized/maybe-uninitialized 전 범주)
대표: test_mgr_ipc_client.c:516:5 'send_thread' is used uninitialized [-Werror=uninitialized]
대표: test_mgr_ipc_client.c:1251:13 'first_thread' may be used uninitialized [-Werror=maybe-uninitialized]
대표: test_mgr_ipc_client.c:926:14 'send_thread_main' defined but not used [-Werror=unused-function]
```

세 module 모두 base(`2ccb0d0bccb3d456680d7ca4fe1a3d82d6ca303b`)와 Fix 1 이후 상태에서 동일하게 재현되며, SNS-STR-008 fix와 무관한 pre-existing test harness 결함임을 재확인했다.

## 4. Finding별 수정

### SENSOR-W1-INT-FIX-R1-001 (config) — 수정 완료

파일: `tests/unit/sensor_core/config/test_config_store.c`. 파일 내 모든 `assert()`를 always-on `CHECK(cond, msg)` 매크로(NDEBUG 여부와 무관하게 항상 평가, 실패 시 `fprintf` + `abort()`)로 전환했다. Side-effect 호출(`sensor_config_store_init`, `sensor_device_store_load_cached` 등)이 Release에서도 항상 실행되며, `cached`/`device`/`raw`/`st`/`cfg` 등 기존에 assert 안에서만 검증되던 값을 모두 실제로 검증한다. `assert.h` include를 제거했다(더 이상 `assert()`를 사용하지 않음). 기존 config 저장·조회·raw payload·device 검증 의미와 3개 CTest(`SNS-CORE-001-config`, `SNS-CORE-002`, `SNS-CORE-005-config`) 의미를 그대로 유지했다.

### SENSOR-W1-INT-FIX-R1-002 (mode_state) — 수정 완료

파일: `tests/unit/sensor_core/mode_state/test_mode_state.c`. 동일한 `CHECK()` 매크로로 전체 assert를 전환했다. `t`의 값 검증을 포함해 모든 boundary-value 검증(0/1/2/-1/INT32_MAX/INT32_MIN)이 Release에서도 실행된다. Mode transition 의미는 변경하지 않았고 dummy use는 추가하지 않았다. 2개 CTest(`SNS-CORE-002a`, `SNS-CORE-003-mode-state`) 의미를 그대로 유지했다.

### SENSOR-W1-INT-FIX-R1-003 (mgr_ipc) — 수정 완료

파일: `tests/unit/sensor_core/mgr_ipc/test_mgr_ipc_client.c`. 동일한 `CHECK()` 매크로로 전체 assert를 전환했다(약 275개 호출부). 그중 `pthread_create()` 호출 22곳 전부를 assert 밖으로 이동해 반환값을 named local(`*_create_rc`)에 저장하고 `CHECK()`로 검증했으며, 성공 여부를 별도 bool(`*_created`)로 명시적으로 추적했다. `pthread_join()` 호출 21곳 전부를 해당 `*_created` bool로 게이팅해 성공적으로 생성된 thread만 join하도록 수정했고, join 자체의 반환값도 `CHECK()`로 검증했다(기존에 bare로 호출되던 join도 포함). `send_thread`/`first_thread`/`second_thread`/`destroy_thread`/`shutdown_thread`/`operation_thread` 등 모든 uninitialized-possible thread handle 사용을 제거했다. `send_thread_main` 등 모든 `*_thread_main` helper와 `get_connect_count`/`wait_until_ge`/`lifecycle_probe_wait` 등 helper function이 Release에서도 실제로 호출되도록 복구됐다. `pthread_mutex_init`/`pthread_cond_init`처럼 assert 안에만 있던 다른 side-effect 호출도 함께 always-on으로 전환했다(이 경우들은 기존 cleanup이 이미 무조건적이라 별도 상태 추적은 불필요했다). continuation 재검토에서는 남아 있던 `(void)payload_json`을 callback payload non-null 검증으로, `(void)arg`를 warm-up thread 실행 완료 플래그 전달·join 후 검증으로 교체했다. 최종 파일에는 `__attribute__((unused))`, `(void)` cast, compiler warning 억제가 없다. mgr_ipc 기존 5개 CTest(`SNS-CORE-003a`, `SNS-CORE-003b-mgr-ipc`, `CT-IPC-002`, `SNS-CORE-006`, `SNS-CORE-007-mgr-ipc`) 의미를 그대로 유지했고, Real MGR IPC 경로(`mgr_ipc_client.c`, `real_connector.c`)는 수정하지 않았다.

수정은 executor 서브에이전트(`mgr-ipc-fix`)에게 위임했으며, 완료 후 세 가지 특히 위험한 지점(`test_send_shutdown_race`의 두 thread 처리, `test_concurrent_stop_destroy_stress`의 3-way 분기별 thread 처리, `test_callback_lifecycle_operations` scenario 0의 create→barrier release→join 순서)을 직접 라인 단위로 재검토해 cross-wiring이나 순서 변경이 없음을 확인했다.

### SENSOR-W1-INT-FIX-R1-004 (Low) — DEFERRED_NON_BLOCKING

`src/features/stream/CMakeLists.txt`는 이번 세션에서 수정하지 않았다. macOS 검증에서 동일한 duplicate-library linker warning이 예상대로 재현됐다(§6).

## 5. 수정 파일 / Scope 검사

```text
MODIFIED_CODE_FILES:
tests/unit/sensor_core/config/test_config_store.c
tests/unit/sensor_core/mode_state/test_mode_state.c
tests/unit/sensor_core/mgr_ipc/test_mgr_ipc_client.c

ALLOWED_PATH_VIOLATIONS: 0
PRODUCTION_CHANGES: 0
FOUNDATION_CHANGES: 0
CONTRACT_CHANGES: 0
ROOT_CMAKE_CHANGES: 0

tests/unit/sensor_stream/tcp_channel/test_tcp_channel.c: 변경 없음 (Fix 1 Review 이후 byte-identical, git diff 0)
src/features/stream/CMakeLists.txt: 변경 없음 (Low finding 그대로 유지)
integration/wave1 branch: 변경 없음
```

## 6. 검증 결과

### 6.1 Targeted Release tests-on (G008)

```text
CONFIG_RELEASE_TESTS_ON: configure PASS, build PASS, 3/3 tests PASS
MODE_STATE_RELEASE_TESTS_ON: configure PASS, build PASS, 2/2 tests PASS
MGR_IPC_RELEASE_TESTS_ON: configure PASS, 두 target(test_sensor_core_mgr_ipc, test_sensor_core_mgr_ipc_lifecycle) build PASS, 5/5 tests PASS

TARGETED_RELEASE_TESTS_ON: 10/10 PASS
WARNING_COUNT: 0
COMPILER_ERROR_COUNT: 0
```

fresh Docker rebuild로 두 차례(수정 직후 1회, 최종 통합 1회) 독립 재확인했다.

보완 implementation `5910de88189b0194080c4cfe346ce2157d04ccdd`에서도 세 module을 fresh `/tmp` build로 다시 실행해 동일한 10/10 결과와 warning/error 0을 확인했다.

### 6.2 전체 Docker Linux arm64 Regression — 67개 구성 (G009)

13개 standalone CMake unit(root Foundation + config/mode_state/mgr_ipc/health/state_report/update_guard/tcp_8141/result_policy/wav/compression/stream/mock)을 기준으로 재구성했다.

```text
DEBUG (13 units): 13/13 configure PASS, 13/13 build PASS, 58/58 tests PASS
  foundation 9/9, config 3/3, mode_state 2/2, mgr_ipc 5/5, health 2/2,
  state_report 2/2, update_guard 1/1, tcp_8141 5/5, result_policy 6/6,
  wav 4/4, compression 6/6, stream 5/5, mock 8/8

RELEASE_TESTS_ON (12 non-root units + Foundation Release 별도): 12/12 configure PASS, 12/12 build PASS, 49/49 tests PASS
  (Foundation Release: configure PASS, build PASS, 9/9 tests PASS)
  config/mode_state/mgr_ipc 포함 12개 module 모두 build 성공 확인 - 이번 fix로 이전의 9/12 build PASS + 3/12 FAIL 상태가 12/12로 전환됨

RELEASE_TESTS_OFF (root + 11, mock 제외): 12/12 configure/build PASS, 0 warnings/0 errors

UBSAN (13 units): 13/13 configure PASS, 13/13 build PASS, 58/58 tests PASS, UBSAN_DIAGNOSTICS: 0

ASAN (13 units): 13/13 configure PASS, 13/13 build PASS, 58/58 tests PASS, ASAN_DIAGNOSTICS: 0

REAL_MGR_IPC (Debug/UBSan/ASan): 3/3 구성 모두 configure/build/test PASS, 각 6/6 tests PASS, diagnostics 0

DOCKER_CONFIGURE_RESULT: 67/67 PASS
DOCKER_BUILD_RESULT: 67/67 PASS
DOCKER_TEST_RESULT: 실행된 모든 CTest suite 100% PASS
```

continuation의 보완 변경이 실제로 포함되는 모든 Docker mgr_ipc 구성도 다시 실행했다. unit transport는 Release/Debug/UBSan/ASan 각각 5/5, Real MGR IPC는 Debug/UBSan/ASan 각각 6/6 통과했고 ASan/UBSan 진단은 0이었다. Release tests-off는 test source를 build하지 않으므로 영향이 없다. 따라서 최초 67개 matrix 중 영향받은 모든 구성을 새 implementation SHA에서 교체 검증했고, 나머지 구성은 code/blob 변경이 없다.

### 6.3 macOS 전체 Regression (G010)

```text
MACOS_RESULT:
Foundation Debug: 5/5
Sensor Core Debug: 15/15 (config 3, mode_state 2, mgr_ipc 5, health 2, state_report 2, update_guard 1)
Sensor Stream Debug: 34/34 (tcp_8141 5, result_policy 6, wav 4, compression 6, stream 5, mock 8)
Debug aggregate: 54/54
UBSan aggregate: 54/54, diagnostics 0
Release tests-off (root+11): 12/12 configure/build PASS

기존 기대치와 정확히 일치. Low finding SENSOR-W1-INT-FIX-R1-004(stream duplicate-library linker warning)는 Sensor Stream Debug 및 UBSan aggregate 범주에서 예상대로 재현됐고(non-blocking), Release tests-off 범주(test 링크 단계 자체가 없음)에서는 나타나지 않음을 확인했다 - 일관성 있음.
```

continuation 보완 변경이 포함되는 macOS mgr_ipc Debug와 UBSan도 fresh `/tmp` build에서 각각 5/5 통과했고 UBSan 진단은 0이었다. 나머지 macOS 대상의 code/blob 변경은 없다.

### 6.4 TCP 8141 — SNS-STR-008 Regression 보호 (G011)

`test_tcp_channel.c`는 Fix 1 Review 이후 byte-identical(git diff 0)임을 재확인한 뒤 검증했다.

```text
TCP_8141_DEBUG_RESULT: 5/5 PASS
TCP_8141_RELEASE_RESULT: 5/5 PASS
TCP_8141_UBSAN_RESULT: 5/5 PASS
TCP_8141_ASAN_RESULT: 5/5 PASS (single pass)
TCP_8141_ASAN_REPEAT_RESULT: 20/20 suite runs PASS, 100/100 individual tests PASS
  (SNS-STR-008-fd-leak: 20/20 repeat 전부 PASS)

전체 24회 실행(단일 4구성 + ASan 20회 반복) 로그를 sanitizer 진단 마커로 grep한 결과 0건.
```

## 7. Lifecycle / Resource

```text
ASAN_RESULT: PASS_WITHIN_EXECUTED_SCOPE (Docker 13-unit ASan 58/58, TCP 8141 ASan 20/20 반복 100/100, Real MGR IPC ASan 6/6, 진단 0)
UBSAN_RESULT: PASS_WITHIN_EXECUTED_SCOPE (Docker 13-unit UBSan 58/58, macOS UBSan aggregate 54/54, TCP 8141 UBSan 5/5, Real MGR IPC UBSan 6/6, 진단 0)
TSAN_RESULT: NOT_RUN (범위 밖)

FD_RESULT: TCP 8141 ASan 20회 반복 fd mismatch 0/20
THREAD_RESULT: echo thread join CHECK 전 구성에서 통과. mgr_ipc의 pthread_create 22곳/pthread_join 21곳 전부 반환값 검증 + 생성 성공 시에만 join하도록 수정되어 Debug/Release/UBSan/ASan/macOS 전 구성에서 uninitialized-thread 진단 0. Runtime warm-up thread는 전달된 실행 플래그를 설정하고 join 후 always-on 검증된다.
LIFECYCLE_RESULT: PASS_WITHIN_EXECUTED_SCOPE
```

## 8. Manifest

```text
contracts/contract-manifest.sha256:
a69536c286839c97e05ed7f54b5834d843f94eae4a9221ad6213de93d268fa6e (일치)

third_party/DEPENDENCY_MANIFEST.sha256:
9934277d3a8d1dabd1c2632d3501743f8d2a57218c6dd6f3635b2b3844296ad2 (일치)

MANIFEST_RESULT: PASS
```

두 manifest 파일 자체 hash가 기대값과 일치했고, manifest 내부의 모든 contract 및 cJSON 대상 파일도 `shasum -a 256 -c`로 PASS했다.

## 9. 검증 범위 밖

```text
RV1106_CROSS_BUILD: NOT_PERFORMED
RV1106_BOARD_RUNTIME: NOT_PERFORMED
HARDWARE_QA: NOT_PERFORMED
```

## 10. 최종 상태

```text
STATE:
FIX_IMPLEMENTATION_FINISHED
AWAITING_CODEX_REVIEW_2

NEXT_REVIEW_SESSION_ID:
CC-SENSOR-W1-INTEGRATION-FIX-2-CODEX-REVIEW-2

NEXT_REVIEW_FILE:
session_results/wave1/review/CC-SENSOR-W1-INTEGRATION-FIX-2_CODEX_REVIEW_2.md

NEXT_REVIEW_SHA_LEDGER_FILE:
session_results/wave1/review/CC-SENSOR-W1-INTEGRATION-FIX-2_CODEX_REVIEW_2_SHA_LEDGER.md
```

Merge, tag, push는 수행하지 않았다. `integration/wave1`에는 merge하지 않았다.
