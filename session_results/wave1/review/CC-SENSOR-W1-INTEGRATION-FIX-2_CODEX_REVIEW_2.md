# CC-SENSOR-W1-INTEGRATION-FIX-2 — Codex 독립 Fix 2 Review 2

## 1. Review identity

```text
REVIEW_SESSION_ID:
CC-SENSOR-W1-INTEGRATION-FIX-2-CODEX-REVIEW-2

REVIEW_START_HEAD:
9482ff6e59e2743d26ca484109b6d9afee78f6de

REVIEW_TARGET_SHA:
5910de88189b0194080c4cfe346ce2157d04ccdd

REVIEWED_HEAD_SHA:
5910de88189b0194080c4cfe346ce2157d04ccdd
```

Worktree identity gate 결과:

```text
pwd -P:
/Users/juganghyeon/Desktop/uniuni/projects/worktrees/sensor-w1-release-tests-on

git rev-parse --show-toplevel:
/Users/juganghyeon/Desktop/uniuni/projects/worktrees/sensor-w1-release-tests-on

branch:
fix/sensor-w1-release-tests-on

tracked staged/modified files at review start:
0
```

Review 시작 시 `git status --short --untracked-files=all`은 비어 있었다. 금지된 local artifact를 삭제, 수정, stage 또는 commit하지 않았다.

## 2. Authoritative SHA 및 lineage

```text
FOUNDATION_BASE_SHA:
07809cb1f3f2b86a8e92ade661c48cb3adb97b52

BASE_INTEGRATION_PRODUCTION_SHA:
2ccb0d0bccb3d456680d7ca4fe1a3d82d6ca303b

FIX_1_IMPLEMENTATION_SHA:
3a62d737111c611399cbdfd4e368a5b27150a18e

FIX_1_REVIEW_ARTIFACT_SHA:
1e7daaf8f8f507f9d5d42d86ba5556e2c3a7a561

FIX_1_REVIEW_LEDGER_SHA:
16a0bb071133f427af32f8216e9ba66729f54444

FIX_2_INITIAL_IMPLEMENTATION_SHA:
6538b8de5bd48d04b7e01c65ac8a4ce1140ec5eb

FIX_2_IMPLEMENTATION_SHA:
5910de88189b0194080c4cfe346ce2157d04ccdd

FIX_2_RESULT_REPORT_SHA:
8136cca97ad8181a68c03d6173f94dc8f56e36eb

FIX_2_LEDGER_COMMIT_SHA:
9482ff6e59e2743d26ca484109b6d9afee78f6de
```

Fix 2 결과 문서와 ledger SHA는 각 파일에 대한 `git log -1 --format='%H'`로 직접 조회했다. 모든 SHA가 실제 commit object임을 `git cat-file -t`로 확인했다.

다음 ancestry를 각각 독립 `git merge-base --is-ancestor` 명령으로 검사했고 전부 통과했다.

```text
07809cb1f3f2b86a8e92ade661c48cb3adb97b52
→ 2ccb0d0bccb3d456680d7ca4fe1a3d82d6ca303b
→ 3a62d737111c611399cbdfd4e368a5b27150a18e
→ 1e7daaf8f8f507f9d5d42d86ba5556e2c3a7a561
→ 16a0bb071133f427af32f8216e9ba66729f54444
→ 6538b8de5bd48d04b7e01c65ac8a4ce1140ec5eb
→ 5910de88189b0194080c4cfe346ce2157d04ccdd
→ 8136cca97ad8181a68c03d6173f94dc8f56e36eb
→ 9482ff6e59e2743d26ca484109b6d9afee78f6de
```

```text
SHA_LINEAGE_RESULT:
PASS
```

Implementation 이후 Review 시작 HEAD까지의 변경은 허용된 다음 두 문서뿐이다.

```text
session_results/wave1/integration/CC-SENSOR-W1-INTEGRATION-FIX-2.md
session_results/wave1/integration/CC-SENSOR-W1-INTEGRATION-FIX-2-SHA_LEDGER.md

UNREVIEWED_CODE_AFTER_FIX_2_IMPLEMENTATION:
0
```

## 3. 변경 범위

`16a0bb071133f427af32f8216e9ba66729f54444..5910de88189b0194080c4cfe346ce2157d04ccdd`에서 변경된 code/test 파일은 허용된 세 파일뿐이다.

```text
CHANGED_CODE_FILES:
tests/unit/sensor_core/config/test_config_store.c
tests/unit/sensor_core/mode_state/test_mode_state.c
tests/unit/sensor_core/mgr_ipc/test_mgr_ipc_client.c

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

FIX_1_REVIEWED_FILE_CHANGES:
0
```

`tests/unit/sensor_stream/tcp_channel/test_tcp_channel.c`의 Fix 1 Review ledger 시점 blob과 현재 파일 SHA-256은 모두 `ba146ef23a4fee278170081f271d6d9a960332d2ea7de09bf9a2773d90e1bcc7`로 byte-identical이다.

## 4. Finding별 code review

### SENSOR-W1-INT-FIX-R1-001 — RESOLVED

`tests/unit/sensor_core/config/test_config_store.c:15-20`의 `CHECK`는 `NDEBUG`와 무관하게 조건을 한 번 평가하고 실패 시 즉시 abort한다. 기존 69개 `assert()` 검증은 69개 always-on `CHECK` 호출로 보존됐다. 초기화/load/apply처럼 side effect가 있는 호출과 `cached`, `device`, `raw`, `st`, `cfg` 등의 실제 값 검증이 Release에서도 실행된다. 실행 코드에 `(void)` cast나 warning 억제는 없다.

독립 Release tests-on 결과는 기존 세 CTest 의미를 그대로 실행해 3/3 통과했다. Debug, UBSan, ASan에서도 같은 3/3이 통과했다.

```text
SENSOR-W1-INT-FIX-R1-001:
RESOLVED
```

### SENSOR-W1-INT-FIX-R1-002 — RESOLVED

`tests/unit/sensor_core/mode_state/test_mode_state.c:17-22`의 always-on `CHECK`로 기존 27개 assertion을 모두 보존했다. `t.changed`와 `t.runtime_value` 검증은 Release에서도 실행되며, 0, 1, 2, -1, `INT32_MAX`, `INT32_MIN` 경계값 검증이 유지된다. Dummy use나 compiler 억제는 없다.

독립 Release tests-on 2/2와 Debug/UBSan/ASan 각 2/2가 통과했다.

```text
SENSOR-W1-INT-FIX-R1-002:
RESOLVED
```

### SENSOR-W1-INT-FIX-R1-003 — RESOLVED

`tests/unit/sensor_core/mgr_ipc/test_mgr_ipc_client.c:25-30`은 동일한 always-on 정책을 사용한다. 실제 `pthread_create()` call site 22곳은 `assert()` 밖에서 반환값을 named local에 저장하고 `CHECK`로 검증한다. 조건 분기의 두 create call site가 하나의 `shutdown_thread_create_rc`와 생성 상태를 공유하는 경우를 포함해 모든 실행 경로에서 성공 여부가 bool로 추적된다.

실제 `pthread_join()` call site 21곳은 해당 생성 성공 bool로 게이팅되고 join 반환값도 always-on 방식으로 검증된다. 초기화되지 않은 `pthread_t`를 join하는 경로는 없다. `send_thread_main`, lifecycle helper, wait helper와 runtime warm-up helper가 Release에서 실제 호출된다. Warm-up helper는 실행 플래그를 thread 인자로 넘기고 join 후 true를 검증한다. Callback payload도 non-null로 검증한다.

실행 코드에서 `assert()`, `(void)` cast, unused attribute, pragma/diagnostic 또는 `-Wno-*` 억제를 찾지 못했다. 기존 cleanup 순서와 생성된 thread의 join이 유지된다.

독립 Release tests-on 5/5, Debug/UBSan/ASan 각 5/5, Real MGR IPC Debug/UBSan/ASan 각 6/6이 통과했다. Uninitialized-thread, 생성되지 않은 thread join, join 실패 또는 sanitizer 진단은 0건이다.

```text
SENSOR-W1-INT-FIX-R1-003:
RESOLVED
```

### SENSOR-W1-INT-FIX-R1-004 — DEFERRED_NON_BLOCKING

`src/features/stream/CMakeLists.txt`는 변경되지 않았다. macOS Debug와 UBSan stream-session 링크에서 기존과 같은 다음 warning이 각 1회, 합계 2회 재현됐다.

```text
ld: warning: ignoring duplicate libraries: '../_savvy_core/libsavvy_core.a', '../_savvy_protocol/libsavvy_protocol.a'
```

관련 CTest는 모두 통과했고 runtime 또는 sanitizer 결함은 관찰되지 않았다. 기존 Low, 비차단 판정을 유지한다.

```text
SENSOR-W1-INT-FIX-R1-004:
DEFERRED_NON_BLOCKING
```

## 5. Targeted Release tests-on

기존 local image `savvy-foundation-test:ubuntu22.04-arm64-v1`만 사용했다. 확인된 image ID는 `sha256:73c8a9709607d1910231efb4648510e4d72052072629901fa28fd5c9f39753e7`이고 architecture는 arm64다. `--pull=never --platform linux/arm64 --network none`, source read-only bind mount, container 내부 fresh `/tmp` build를 사용했다.

```text
CONFIG_RELEASE_TESTS_ON:
3/3 PASS

MODE_STATE_RELEASE_TESTS_ON:
2/2 PASS

MGR_IPC_RELEASE_TESTS_ON:
5/5 PASS

TARGETED_RELEASE_TOTAL:
10/10 PASS

WARNING_COUNT:
0

COMPILER_ERROR_COUNT:
0
```

## 6. Docker Linux arm64 전체 검증

67개 validation configuration을 모두 fresh `/tmp` build로 재구성했다. `tools/mock_mgr`는 각 Real MGR IPC 구성의 prerequisite로 빌드했으며 별도 validation configuration으로 중복 집계하지 않았다.

```text
DOCKER_CONFIGURE_RESULT:
67/67 PASS

DOCKER_BUILD_RESULT:
67/67 PASS

DOCKER_TEST_RESULT:
실행된 모든 CTest suite PASS

FOUNDATION_DEBUG:
9/9 PASS

SENSOR_DEBUG_MODULES:
49/49 PASS

DEBUG_AGGREGATE:
58/58 PASS

FOUNDATION_RELEASE:
9/9 PASS

RELEASE_TESTS_ON:
49/49 PASS

RELEASE_TESTS_OFF:
12/12 configure/build PASS

UBSAN_AGGREGATE:
58/58 PASS, diagnostics 0

ASAN_AGGREGATE:
58/58 PASS, diagnostics 0

REAL_MGR_IPC_DEBUG:
6/6 PASS

REAL_MGR_IPC_UBSAN:
6/6 PASS, diagnostics 0

REAL_MGR_IPC_ASAN:
6/6 PASS, diagnostics 0

DOCKER_WARNING_COUNT:
0

DOCKER_COMPILER_ERROR_COUNT:
0
```

## 7. macOS 전체 검증

macOS host의 fresh `/tmp` build에서 38개 validation configuration을 실행했다. 최초 probe는 Foundation 5/5가 통과했지만 CTest 버전별 성공 문구 차이를 집계기가 인식하지 못해 중단했다. 이 probe는 집계에서 제외하고, 수정한 집계기로 새 임시 directory에서 38개 구성을 처음부터 완주한 결과만 아래에 사용했다.

```text
MACOS_CONFIGURE_RESULT:
38/38 PASS

MACOS_BUILD_RESULT:
38/38 PASS

FOUNDATION_DEBUG:
5/5 PASS

SENSOR_CORE_DEBUG:
15/15 PASS

SENSOR_STREAM_DEBUG:
34/34 PASS

DEBUG_AGGREGATE:
54/54 PASS

UBSAN_AGGREGATE:
54/54 PASS, diagnostics 0

RELEASE_TESTS_OFF:
12/12 configure/build PASS

MACOS_WARNING_COUNT:
2 emissions, 1 unique diagnostic

MACOS_RESULT:
PASS_WITH_EXISTING_LOW_FINDING
```

## 8. SNS-STR-008 회귀 보호

TCP 8141의 Debug, Release, UBSan, ASan 단일 suite는 Docker 67개 행렬에서 각각 실행했다. 별도 fresh ASan build에서 5-test 전체 suite를 20회 반복했다.

```text
TCP_8141_DEBUG_RESULT:
5/5 PASS

TCP_8141_RELEASE_RESULT:
5/5 PASS

TCP_8141_UBSAN_RESULT:
5/5 PASS

TCP_8141_ASAN_RESULT:
5/5 PASS

TCP_8141_ASAN_REPEAT_RESULT:
20/20 suites PASS
100/100 individual tests PASS

FD_MISMATCH:
0

HANG_COUNT:
0

ASAN_DIAGNOSTICS:
0

SNS_STR_008_STATUS:
RESOLVED
```

## 9. Lifecycle 및 resource

```text
ASAN_RESULT:
PASS_WITHIN_EXECUTED_SCOPE
- Docker aggregate 58/58
- Real MGR IPC 6/6
- TCP 8141 단일 5/5 및 반복 100/100
- diagnostics 0, leak diagnostics 0

UBSAN_RESULT:
PASS_WITHIN_EXECUTED_SCOPE
- Docker aggregate 58/58
- Real MGR IPC 6/6
- macOS aggregate 54/54
- diagnostics 0

TSAN_RESULT:
NOT_RUN

FD_RESULT:
PASS_WITHIN_EXECUTED_SCOPE
- TCP 8141 ASan 반복 fd mismatch 0

THREAD_RESULT:
PASS_WITHIN_EXECUTED_SCOPE
- uninitialized thread diagnostic 0
- 생성되지 않은 thread join 0
- pthread join check failure 0
- Real MGR IPC Debug/UBSan/ASan 각 6/6

LIFECYCLE_RESULT:
PASS_WITHIN_EXECUTED_SCOPE
```

## 10. Manifest

```text
contracts/contract-manifest.sha256:
a69536c286839c97e05ed7f54b5834d843f94eae4a9221ad6213de93d268fa6e

third_party/DEPENDENCY_MANIFEST.sha256:
9934277d3a8d1dabd1c2632d3501743f8d2a57218c6dd6f3635b2b3844296ad2

MANIFEST_RESULT:
PASS
```

두 manifest 파일 자체 SHA-256이 기대값과 일치했다. `shasum -a 256 -c`로 contract 5개와 cJSON dependency 3개 내부 대상 파일도 모두 `OK`임을 확인했다.

## 11. Fix 2 결과 문서 정확성

Fix 2 결과 문서 및 SHA ledger의 authoritative SHA, 변경 파일, scope, Targeted 10/10, Docker 67/67, macOS 결과, TCP 반복, sanitizer/lifecycle 결과, manifest 결과를 Git과 이번 독립 실행 결과에 대조했다. 확인 가능한 값은 모두 일치했고 미확정 placeholder도 없다. 수정 전 FAIL 기록은 pre-fix 재현 이력으로 명확히 구분되어 있다.

```text
FIX_2_RESULT_DOCUMENT_ACCURACY:
ACCURATE
```

## 12. 신규 finding 및 severity

```text
NEW_FINDINGS:
NONE

CRITICAL:
0

HIGH:
0

MEDIUM:
0

LOW:
1
- 기존 SENSOR-W1-INT-FIX-R1-004만 유지
```

## 13. 검증 한계

```text
TSAN_RESULT:
NOT_RUN

RV1106_CROSS_BUILD:
NOT_PERFORMED

RV1106_BOARD_RUNTIME:
NOT_PERFORMED

HARDWARE_QA:
NOT_PERFORMED
```

Docker와 macOS host 검증은 수행했으나 RV1106 cross build, board runtime, hardware QA는 이번 Review 범위에 포함되지 않았다. TSAN은 실행하지 않았고 PASS로 추정하지 않는다.

## 14. 최종 Verdict

세 Medium finding은 모두 해결됐고 Fix 1의 SNS-STR-008 해결 상태가 유지된다. 허용 경로 위반과 production/Foundation/contract/root CMake 변경은 0이다. Targeted, Docker 67개, macOS 필수 검증, sanitizer, fd/thread lifecycle, manifest, SHA lineage가 모두 merge gate를 충족한다. 기존 Low duplicate-library warning만 비차단 상태로 남는다.

```text
VERDICT:
PASS_WITH_NON_BLOCKING_FINDINGS

MERGE_CANDIDATE:
YES

NEXT_STEP:
현재 fix/sensor-w1-release-tests-on HEAD의 Review artifact와 SHA ledger를 포함한 상태를 integration/wave1 merge 후보로 사용한다.
SENSOR-W1-INT-FIX-R1-004는 별도 비차단 후속 작업에서 정리하고 macOS Debug/UBSan stream-session link warning 0 및 5/5 CTest로 재검증한다.
```

이번 Review에서는 source, test, mock, CMake, 기존 결과 문서 또는 기존 ledger를 수정하지 않았고 merge, tag, push를 수행하지 않았다.
