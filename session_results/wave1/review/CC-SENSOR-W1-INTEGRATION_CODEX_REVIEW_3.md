# CC-SENSOR-W1-INTEGRATION-CODEX-REVIEW-3

Sensor Wave 1 최종 독립 검증 — Reviewed Fix Merge 이후 전체 상태 판정

## 1. 개요

본 Review는 `CC-SENSOR-W1-INTEGRATION-FIX-MERGE-1`이 `integration/wave1`에 병합된 이후,
전체 Sensor Wave 1 상태(Foundation 위 Sensor Core + Sensor Stream 통합, 기존 finding 재검증,
전체 Docker Linux arm64 67개 구성, 전체 macOS 38개 구성, TCP 8141 sanitizer 반복, manifest/contract
불변)를 독립적으로 최종 판정한다.

`CC-SENSOR-W1-INTEGRATION-FIX-MERGE-1.md`는 merge 시점에 Release tests-on 12/12 + TCP 8141 ASan
단일 5/5 pass만 확인하는 제한된 smoke test를 수행했고, 전체 67개 Docker 구성과 전체 macOS
regression은 명시적으로 범위 밖으로 두고 "다음 독립 Review(Review 3)에서 수행 예정"이라고 기록했다.
본 문서가 그 유예된 전체 매트릭스를 실제 merge SHA 콘텐츠에 대해 처음으로 완전 실행한 결과다.

이번 저장소에는 67개/38개 매트릭스를 자동화하는 checked-in 스크립트가 전혀 존재하지 않는다
(`find`로 확인: `.sh`, `Dockerfile`, `.github/`, `ci/`, `scripts/`, CI 설정 전부 부재).
과거 Fix 1/Fix 2/Review 1/Review 2의 모든 수치 역시 매 세션 수작업으로 cmake/docker/ctest
명령을 구성해 산출된 것이었다. 본 Review 역시 동일한 방식으로, 그러나 이번에는 실제 merge
commit 콘텐츠에 대해 독립적으로 처음부터 재실행했다.

Source, test, mock, CMake는 일절 수정하지 않았다. 이번 세션에서 write한 파일은 본 문서와
SHA ledger, 각각 별도 commit 뿐이다.

## 2. Worktree Identity Gate

```
REVIEW_SESSION_ID: CC-SENSOR-W1-INTEGRATION-CODEX-REVIEW-3
WORKTREE:          /Users/juganghyeon/Desktop/uniuni/projects/worktrees/sensor-wave1
BRANCH:            integration/wave1
REVIEW_START_HEAD: d5144195e954b7e92605078b0b88bbd05ef8a8ad
```

`pwd -P`, `git rev-parse --show-toplevel`, `git branch --show-current`, `git rev-parse HEAD`가
모두 기대값과 일치했다. `git status --short --untracked-files=all`는 비어 있었다(clean).
`git worktree list`는 기대된 5개의 sibling worktree(foundation/contract-v1,
feature/sensor-core, feature/sensor-stream, fix/sensor-w1-fd-lifecycle-test,
fix/sensor-w1-release-tests-on)를 정확히 보여주었다. 새 branch/worktree 생성 없음.

## 3. Authoritative SHA 및 SHA Lineage

전체 SHA 목록은 §19 SHA ledger 참조. 다음 lineage 체크를 `git merge-base --is-ancestor`로
각각 독립 실행했다(총 16개):

- Main chain: `FOUNDATION_BASE_SHA → BASE_INTEGRATION_PRODUCTION_SHA → INTEGRATION_RESULT_COMMIT_SHA
  → INTEGRATION_LEDGER_COMMIT_SHA → FIX_MERGE_SHA`, `REVIEW_2_LEDGER_COMMIT_SHA → FIX_MERGE_SHA`,
  `FIX_MERGE_SHA → MERGE_RESULT_REPORT_SHA → MERGE_LEDGER_COMMIT_SHA(=REVIEW_START_HEAD)` — 8건 전부 ANCESTOR_OK.
- Fix branch 내부 chain: `FIX_1_IMPLEMENTATION_SHA → FIX_1_REVIEW_ARTIFACT_SHA → FIX_1_REVIEW_LEDGER_SHA
  → FIX_2_INITIAL_IMPLEMENTATION_SHA → FIX_2_IMPLEMENTATION_SHA → FIX_2_RESULT_REPORT_SHA
  → FIX_2_LEDGER_COMMIT_SHA → REVIEW_2_ARTIFACT_COMMIT_SHA → REVIEW_2_LEDGER_COMMIT_SHA` — 8건 전부 ANCESTOR_OK.

```
SHA_LINEAGE_RESULT: PASS (16/16 ANCESTOR_OK)
```

`MERGE_RESULT_REPORT_SHA`(083b88b9...)와 `MERGE_LEDGER_COMMIT_SHA`(d5144195...)는 self-reference
회피로 어느 문서에도 기록되어 있지 않아, `git log -1 --format=%H -- <file>`로 직접 조회했다.
`MERGE_LEDGER_COMMIT_SHA`는 `REVIEW_START_HEAD`와 동일함을 확인했다.

## 4. Review Target 고정

```
REVIEW_TARGET_SHA:  4159395df546f1734c0b1064bdb6e091e7653e56
REVIEWED_HEAD_SHA:  4159395df546f1734c0b1064bdb6e091e7653e56
```

`git diff --name-status 4159395d d5144195`는 다음 2개 문서 파일만 보여주었다(둘 다 `A`):

```
session_results/wave1/integration/CC-SENSOR-W1-INTEGRATION-FIX-MERGE-1-SHA_LEDGER.md
session_results/wave1/integration/CC-SENSOR-W1-INTEGRATION-FIX-MERGE-1.md
```

```
UNREVIEWED_CODE_AFTER_MERGE: 0
```

## 5. Merge 구조 검증

`git show -s --format='%H%n%P%n%s' 4159395d`:

```
4159395df546f1734c0b1064bdb6e091e7653e56
1e2e97ae1590446dc3211e6df5d53a54240c9b20 ea4d664f3a2e8b825d197536d1f0c57a9abbd9ac
merge(sensor): integrate reviewed Wave 1 fixes
```

FIRST_PARENT(1e2e97a)=INTEGRATION_LEDGER_COMMIT_SHA, SECOND_PARENT(ea4d664)=REVIEW_2_LEDGER_COMMIT_SHA — 기대값과 일치.

`git show --remerge-diff 4159395d`는 commit 메타데이터 6줄만 출력했고 실제 diff 본문은 없었다
(= conflict resolution 편집 없음).

Merge 결과의 4개 test file blob을 merge commit / Review 2 ledger commit(ea4d664) / Fix 2 구현
commit(5910de88) 세 지점에서 각각 `git rev-parse <sha>:<path>`로 비교했다. 넷 모두 세 지점에서
완전히 동일한 blob SHA를 가졌다:

| 파일 | blob SHA |
|---|---|
| tests/unit/sensor_stream/tcp_channel/test_tcp_channel.c | e3ca4c0b815d1da10418284f45f647a9978589e9 |
| tests/unit/sensor_core/config/test_config_store.c | 744188adca5ad915be54dadcf3f4ed48a42a3d7c |
| tests/unit/sensor_core/mode_state/test_mode_state.c | d0f3e6330cff13daa6315c7373e830a2e98a604c |
| tests/unit/sensor_core/mgr_ipc/test_mgr_ipc_client.c | 0304fe434e941a46ac312eea9ae9de1ec27cb6b3 |

```
MERGE_PARENT_RESULT:   PASS
MERGE_CONFLICTS:       0
MERGE_CONTENT_DRIFT:   0
```

## 6. 전체 Wave 1 변경 범위 (Foundation base → merge)

`git diff --stat 07809cb1 4159395d`: **88 files changed, 16286 insertions(+), 0 deletions(-)**.
`git diff --name-status`의 88개 항목 전부 status `A`(추가) — 기존 파일에 대한 수정/삭제 없음,
순수 추가 diff. (제3의 독립 code-review agent가 최초 "78개"로 오산했으나, `wc -l` / `awk
'{print $1}' | uniq -c` / `--stat` 요약 세 가지 방법으로 재확인한 결과 88이 맞으며, 해당 agent의
frozen-path/root-CMake 실제 커맨드 결과는 본 세션의 독립 결과와 완전히 일치했다 — 산술 오기일 뿐
결론에는 영향 없음.)

Frozen path 확인 (`grep -E '^[AMD]\s+(contracts/|src/core/|src/protocol/|src/platform/interfaces/|src/platform/linux/ipc/|third_party/)'`):
매치 0건. 두 명의 독립 리뷰어(본 세션 직접 실행 + diffscope-review agent)가 각각 별도로 동일한
결과를 얻었다.

Root build file(`CMakeLists.txt`, `CMakePresets.json`, `cmake/`) diff는 Foundation base부터
merge까지 전 구간에서 **완전히 비어 있음** — 원본 integration 구간(07809cb1→2ccb0d0b)과 fix
구간(2ccb0d0b→4159395d) 모두 0 변경. 승인 범위 초과 여부를 따질 필요조차 없다(변경 자체가 없음).

```
ALLOWED_PATH_VIOLATIONS: 0
FOUNDATION_CHANGES:      0
CONTRACT_CHANGES:        0
```

### 6.1 독립 프로덕션 코드 리뷰 (기존 Review 결론을 복사하지 않음)

Foundation 위에 신규 추가된 전체 프로덕션 모듈(config/device_store, health/sensor_lifecycle,
mgr_ipc_client/cancel_source/real_connector, mode_state, state_report, update_guard,
result_policy, wav_encoder, bzip_codec, stream/session, tcp_channel, mock_mgr,
mock_streaming_server)을 전문 code-reviewer 에이전트가 처음부터 다시 읽고 다음을 확인했다:

- mgr_ipc_client.c(745줄): registry-pin(`client_enter/leave` + `api_callers` + `destroy_requested`)이
  콜백-destroy-detach와 외부 stop 동시 발생을 포함한 모든 destroy race에서 client/cond/mutex의
  use-after-free를 정확히 방지함을 확인. lock 순서 일관(`lifecycle→registry`, `state→io`, 중첩 없음).
- tcp_channel.c(604줄): 모든 `socket()` fd가 성공 시 `ch->sockfd`에 저장되거나 모든 실패/중단/타임아웃
  분기에서 `close()`됨을 확인 — **프로덕션 fd 누수 없음**, SNS-STR-008이 test-only 문제였다는 결론과 일치.
- result_policy.c/wav_encoder.c/bzip_codec.c: 버퍼 경계, overflow guard, 페어링된 할당/해제 확인, 결함 없음.
- config_store.c/device_store.c/sensor_lifecycle.c: 모든 조기 반환 경로에서 mutex 해제, overflow-checked
  사이징 확인.

### 6.2 신규 Finding: SENSOR-W1-INT-R3-001 [Low]

**제목**: stop()/destroy() 시 아직 시작되지 않은 채 대기 중이던 send의 `pending_send_t`(pend)가
누수됨 (bounded).

**근거** (본 세션이 실제 코드를 직접 읽어 재확인):

- `src/features/stream/session.c:131` `send_pirin`에서 `malloc(pend)` — 오직 `on_pirin_complete`
  내부 line 113의 `free(pend)`에서만 해제.
- `src/features/stream/session.c:233` `send_data`에서 `malloc(pend)` — 오직 `on_data_complete`
  내부 line 167의 `free(pend)`에서만 해제.
- `src/features/stream/session.c:274` `send_pirout`에서 `malloc(pend)` — 오직 `on_pirout_complete`
  내부 line 256의 `free(pend)`에서만 해제.
- `src/platform/linux/tcp_8141/tcp_channel.c:79-86` `queue_item_destroy`: `free(item->packet)`만
  수행(line 82), `item->ctx`(= pend)는 건드리지 않음. 주석: "이 함수는 cancel()/destroy() 시점에
  아직 대기 중이던 item에 대해서만 실행되며, 보고할 결과가 없다".
- `src/platform/linux/tcp_8141/include/sensor_platform/tcp_channel.h:74-76`: "stop()/destroy()에 의해
  대기 중이던 채로 drop된 요청은 on_complete 콜백을 아예 받지 않는다"는 계약을 명시적으로 문서화.
- `src/features/stream/session.c:359`: bounded queue depth 리터럴 `4 /* bounded queue depth */`.

**메커니즘**: 대기열에 쌓인 채로 drop된 item은 on_complete가 호출되지 않으므로 pend가 절대
해제되지 않고, drop 경로(`queue_item_destroy`)도 `item->ctx`를 해제하지 않는다.

**트리거**: N개의 send를 제출한 직후(lazy connect 최대 1000ms 또는 wait_for_response 최대
3000ms로 worker가 아직 블록 상태) queue에 아직 시작되지 않은 항목이 남아 있는 상태로
stop()/destroy() 호출.

**영향 범위**: queue depth=4로 bounded, 1회 stop-with-backlog 이벤트당 최대 약 3개
`pending_send_t`(~24 bytes) 누수. Use-after-free 아님, 무제한 누수 아님. 세션을 매우 빈번하게
생성/파괴하면서 매번 teardown 시점에 backlog와 경합하는 워크로드가 아니라면 실질적 영향은 낮음.

```
SEVERITY:            Low
MERGE_BLOCKING:       NO
```

이번 Review에서는 source 수정이 금지되어 있으므로 수정하지 않으며, 향후 fix 세션의 대상으로 기록한다.

## 7. 이전 Finding 재검증

독립 code-reading 재검증(Read-only, 소스 직접 확인)과 targeted/전체 build 결과를 함께 사용했다.

### SNS-STR-008 — **RESOLVED**

`test_tcp_channel.c` test_008(240-286행): fd-probe-before(251-252행)는 echo thread 생성 이전에
채취되고, `pthread_join`(278-279행)의 반환값이 `CHECK`로 검증되며, fd-probe-after(281-283행)는
join이 완료된 이후에만 채취된다. 잔여 race 없음(join은 thread가 완전히 종료할 때까지 블록).
Docker(tcp_8141 ASan 5/5, diagnostics 0) 및 20회 fresh ASan 반복(100/100, FD_MISMATCH 0)으로
실증 재확인.

### SENSOR-W1-INT-FIX-R1-001 (config) — **RESOLVED**

`test_config_store.c` 전체에서 bare `assert(` 0건(주석 언급만 존재), `<assert.h>` 미포함.
`CHECK` 매크로(15-20행)는 abort() 기반, NDEBUG 비의존. Release+`-Werror` 실빌드(Docker)에서
3/3 통과를 **두 차례 독립 실행**으로 재확인.

### SENSOR-W1-INT-FIX-R1-002 (mode_state) — **RESOLVED**

`test_mode_state.c` 전체에서 bare `assert(` 0건. `CHECK` 매크로(17-22행) NDEBUG 비의존. 유일한
지역변수 `t`(32행)가 6개 호출 지점에서 할당 및 always-evaluated `CHECK` 내부에서 읽혀
`-Werror=unused-but-set-variable` 위험 없음. Release+`-Werror` 실빌드 2/2 통과 두 차례 재확인.

### SENSOR-W1-INT-FIX-R1-003 (mgr_ipc) — **RESOLVED**

`test_mgr_ipc_client.c`(1628줄) 전체에서 bare `assert(` 0건. 22개 `pthread_create` + 21개
`pthread_join` 호출 지점 전부가 일관된 패턴(named rc 변수 캡처 → `CHECK` → `_created` guard →
join도 동일 패턴)을 따르며, create/join은 assert/CHECK 조건문 내부가 아닌 평문으로 실행되어
NDEBUG 하에서도 무조건 실행됨. 8개 함수, 60개 이상 CHECK 지점을 표본 검사해 클러스터링 없이
전체 파일에 균일하게 적용됨을 확인. Release+`-Werror` 실빌드 5/5 통과 두 차례 재확인.

### SENSOR-W1-INT-FIX-R1-004 (macOS duplicate-library warning) — **DEFERRED_NON_BLOCKING (Low, 상태 유지)**

`src/features/stream/CMakeLists.txt`를 직접 추적: Foundation(cJSON/savvy_core/savvy_protocol)은
정확히 한 번만 `add_subdirectory`됨(33-35행) — 단일 archive. `sensor_stream_session` 타겟이
`savvy_core`/`savvy_protocol`을 직접 PUBLIC 링크(83-84행)하면서 동시에 이들을 이미
PUBLIC-전이적으로 제공하는 4개 sibling target과도 연결되어, 최종 링크 라인에 동일 archive가
중복 등장 — 이것이 benign한 `ld: warning: ignoring duplicate libraries`의 원인이며 ODR/이중
정의 위험은 없음(savvy_core 타겟은 정확히 하나만 존재). macOS 실빌드에서 정확히 동일한 경고
텍스트로 재현됨을 확인(Debug+UBSan stream 빌드에서 각 1회, Release-tests-off에서는 0회 —
FIX-2 문서와 정확히 일치). Merge-blocking 아님, 상태 변경 없음.

## 8. Targeted Post-merge 검증 (Release tests-on)

Docker 67개 매트릭스의 Release-tests-on 카테고리와 동일 데이터:

```
CONFIG_RELEASE_TESTS_ON:     3/3 PASS
MODE_STATE_RELEASE_TESTS_ON: 2/2 PASS
MGR_IPC_RELEASE_TESTS_ON:    5/5 PASS
TCP_8141 (RELEASE):          5/5 PASS
FOUNDATION_RELEASE:          9/9 PASS
비-root Release 12개 unit:   49/49 PASS
경고 0, 컴파일 에러 0
```

## 9. Docker Linux arm64 전체 검증 (67개 구성) — 실제 실행 완료

```
IMAGE:      savvy-foundation-test:ubuntu22.04-arm64-v1
IMAGE_ID:   sha256:73c8a9709607d1910231efb4648510e4d72052072629901fa28fd5c9f39753e7
PLATFORM:   linux/arm64 (네이티브, 에뮬레이션 없음 — Apple M2 Docker Desktop arm64 Linux VM)
NETWORK:    none
SOURCE:     read-only bind mount (write 시도 rejected 확인됨)
BUILD:      각 unit마다 container 내부 fresh /tmp
PULL/BUILD: 없음 (--pull=never, 기존 로컬 image만 사용)
```

13개 standalone CMake unit(Root Foundation, config, health, mgr_ipc, mode_state, state_report,
update_guard, tcp_8141, result_policy, wav, compression, stream, mock_streaming_server) ×
{Debug, Release-tests-on, Release-tests-off, UBSan, ASan} + Real-MGR-IPC(mgr_ipc ×
{Debug,UBSan,ASan}, `SENSOR_MGR_IPC_REAL_TRANSPORT=ON`, `tools/mock_mgr` 사전 빌드) =
13+13+12+13+13+3 = **67개 구성**을 전부 실제 실행했다.

```
DOCKER_CONFIGURE_RESULT: 67/67 PASS
DOCKER_BUILD_RESULT:     67/67 PASS

FOUNDATION_DEBUG:        9/9    (SAVVY_IPC_REAL_TRANSPORT=ON, host-linux preset 기본값과 일치;
                                 CT-PKT-001/002/003, CT-JSON-001/002, CT-IPC-001/002/003, CT-IPC-CANCEL)
SENSOR_DEBUG_MODULES:    49/49
DEBUG_AGGREGATE:         58/58

FOUNDATION_RELEASE:      configure/build PASS, tests 9/9
RELEASE_TESTS_ON:        49/49 PASS (config 3/3, mode_state 2/2, mgr_ipc 5/5 포함)
RELEASE_TESTS_OFF:       12/12 configure/build PASS

UBSAN_AGGREGATE:         58/58 PASS, diagnostics 0
ASAN_AGGREGATE:          58/58 PASS, diagnostics 0   (LSan operability 사전 확인: 4321-byte 의도적
                                                       leak 주입 → 정상 탐지, rc=134)

REAL_MGR_IPC_DEBUG:      6/6 PASS
REAL_MGR_IPC_UBSAN:      6/6 PASS, diagnostics 0
REAL_MGR_IPC_ASAN:       6/6 PASS, diagnostics 0

DOCKER_WARNING_COUNT:        0
DOCKER_COMPILER_ERROR_COUNT: 0

총 테스트 실행/통과: 250/250 (58 Debug + 58 Release-tests-on[9+49] + 58 UBSan + 58 ASan + 18 Real-MGR-IPC[6×3])
실패: 0건
```

Root Foundation은 최초 `SAVVY_IPC_REAL_TRANSPORT=OFF`로 실행되어 5/5(4개 CT-IPC-* 계약 테스트가
빠짐)로 나왔으나, 이는 과거 Review 1/2가 사용한 `host-linux` preset 기본값(ON)과 다른 방법론
선택이었음을 실행 도중 발견해 즉시 ON으로 재실행, 9/9·58/58로 기존 기준선과 일치시켰다(OFF
버전은 fake-transport 변형으로 5/5 확인, 정식 집계에서 제외). 이 discrepancy와 조치는 §16에
투명하게 기록한다.

## 10. macOS 전체 검증 (38개 구성) — 실제 실행 완료

```
HOST:      Darwin arm64, Apple M2
TOOLCHAIN: cmake 4.4.0, Apple clang 17.0.0 (clang-1700.4.4.1), ctest 4.4.0
BUILD:     각 unit마다 host의 fresh /tmp
```

13개 unit × {Debug, UBSan} + Release-tests-off(root+11, mock 제외) = 13+13+12 = **38개 구성**.

```
MACOS_CONFIGURE_RESULT: 38/38 PASS
MACOS_BUILD_RESULT:     38/38 PASS

FOUNDATION_DEBUG:      5/5   (SAVVY_IPC_REAL_TRANSPORT는 macOS에서 AF_UNIX SOCK_SEQPACKET
                              부재로 NOT_AVAILABLE — OFF로 실행, 정상)
SENSOR_CORE_DEBUG:     15/15
SENSOR_STREAM_DEBUG:   34/34
DEBUG_AGGREGATE:       54/54

UBSAN_AGGREGATE:       54/54 PASS, diagnostics 0
RELEASE_TESTS_OFF:     12/12 configure/build PASS

MACOS_WARNING_COUNT:        2건 / unique 1건 — R1-004 stream duplicate-library ld warning
                            (Debug+UBSan stream 빌드에서만, Release-off 0건) — 알려진 Low, 상태 불변
MACOS_COMPILER_ERROR_COUNT: 0
```

CTest 버전별 성공 문구 차이 함정(Review 2 문서가 지적한 바로 그 문제 — CMake 4.4.0의 ctest는
전체 통과 시 ", 0 tests failed" 절을 생략)을 이번에도 실제로 겪었으나, exit code를 근거로 삼는
견고한 파싱으로 올바르게 처리했음을 확인했다.

범위 외 axis(Real-MGR-IPC, root real-transport, Release-tests-on)는 macOS에서 플랫폼상
불가능(AF_UNIX SOCK_SEQPACKET Linux 전용) 또는 방법론상 Linux/Docker 전용 axis로 명확히
구분되어 보고되었으며, 어느 것도 미실행을 PASS로 표현하지 않았다.

ASan은 bonus/informational로만 시도: mode_state에서 configure/build는 성공하나 instrumented
바이너리가 SNS-CORE-002a에서 재현성 있게 hang(15s/60s timeout), `detect_leaks=1` 설정 시
"detect_leaks is not supported on this platform"으로 즉시 abort. macOS 38 필수 범위에서
ASan을 제외하는 판단이 옳았음을 더 정밀한 근거로 재확인했다.

## 11. TCP 8141 반복 및 Lifecycle — 실제 실행 완료

```
TCP_8141_DEBUG_RESULT:   5/5 PASS, diagnostics 0
TCP_8141_RELEASE_RESULT: 5/5 PASS, diagnostics 0
TCP_8141_UBSAN_RESULT:   5/5 PASS, diagnostics 0
TCP_8141_ASAN_RESULT:    5/5 PASS, diagnostics 0

TCP_8141_ASAN_REPEAT_RESULT: 20/20 suites PASS, 100/100 individual tests PASS
FD_MISMATCH:    0
HANG_COUNT:     0
ASAN_DIAGNOSTICS: 0
경과 시간: 361초 (약 6분 1초), 20회 모두 fresh cmake configure + build
```

반복 전 LeakSanitizer 작동 여부를 의도적으로 주입한 1234-byte leak으로 사전 검증(정상 탐지,
rc=134) — ASAN_DIAGNOSTICS:0이 탐지 기능 비활성화가 아닌 진짜 clean 결과임을 담보한다.

## 12. Sanitizer / fd / thread / lifecycle 종합

```
ASAN_RESULT:      PASS (Docker 58/58 + Real-MGR-IPC 6/6 + TCP repeat 100/100, 전부 diagnostics 0)
UBSAN_RESULT:     PASS (Docker 58/58 + Real-MGR-IPC 6/6 + macOS 54/54 + TCP 5/5, 전부 diagnostics 0)
TSAN_RESULT:      NOT_RUN — Docker 기본 seccomp 프로파일이 TSan 초기화에 필요한 personality()
                  syscall을 차단함("CHECK failed: tsan_platform_linux.cpp:296 personality(...)
                  != -1", InitializePlatform() 단계, main() 진입 전, rc=66) — 환경/샌드박스
                  제약이며 코드 결함이나 data race가 아니다. 샌드박스 확장(--security-opt
                  seccomp=unconfined)은 이번 세션 지시("신규 검증 시작 금지") 및 원 프로토콜
                  §13의 "PASS로 추정하지 마라" 원칙에 따라 승인하지 않고 NOT_RUN으로 정직하게 기록한다.
FD_RESULT:        PASS (FD_MISMATCH 0, tcp_channel.c 프로덕션 코드 fd 누수 없음 독립 확인)
THREAD_RESULT:    PASS (mgr_ipc_client.c 22 create/21 join 전부 RESOLVED 확인, TCP 8141 hang 0)
LIFECYCLE_RESULT: PASS (SNS-STR-008 fix 20회 fresh ASan 반복으로 실증, mgr_ipc registry-pin
                  독립 코드 리뷰로 use-after-free 없음 확인)
```

## 13. Manifest 및 Frozen Contract

```
shasum -a 256 -c contracts/contract-manifest.sha256   → 5/5 OK
shasum -a 256 -c third_party/DEPENDENCY_MANIFEST.sha256 → 3/3 OK

contracts/contract-manifest.sha256 자체 해시:
  a69536c286839c97e05ed7f54b5834d843f94eae4a9221ad6213de93d268fa6e  (기대값과 일치)
third_party/DEPENDENCY_MANIFEST.sha256 자체 해시:
  9934277d3a8d1dabd1c2632d3501743f8d2a57218c6dd6f3635b2b3844296ad2  (기대값과 일치)

CONTRACT_MANIFEST_TARGETS:   5/5 OK
DEPENDENCY_MANIFEST_TARGETS: 3/3 OK
MANIFEST_RESULT: PASS
```

Frozen Foundation 코드와 contract는 §6에서 확인한 바와 같이 전 구간 무변경.

## 14. 결과 문서 정확성

`CC-SENSOR-W1-INTEGRATION-FIX-MERGE-1.md` 및 그 SHA ledger를 Git 증거와 본 Review의 독립
실행 결과에 대조했다. Merge 메커니즘(parent, blob 동일성), post-merge smoke 수치(Release
tests-on 12/12·49/49 — 본 Review 전체 매트릭스의 RELEASE_TESTS_ON=49/49 상위집합과 정합,
TCP 8141 ASan 단일 5/5), manifest 해시, RV1106 미수행 기록 모두 실제 재실행 결과와 일치했다.
전체 67/38 매트릭스를 명시적으로 범위 밖이라 밝히고 다음 Review로 유예한 자기서술도 과장이나
은폐 없이 정확했다.

```
MERGE_RESULT_DOCUMENT_ACCURACY: ACCURATE
```

## 15. 신규 Finding 요약

```
SENSOR-W1-INT-R3-001 [Low] — session.c pending_send_t bounded leak at stop()/destroy() (§6.2)
```

그 외 신규 Critical/High/Medium finding 없음. Frozen path/root CMake 변경 없음.

## 16. 검증 한계 및 투명성 기록

- Docker 67-config 매트릭스 실행 중 root Foundation의 `SAVVY_IPC_REAL_TRANSPORT` 플래그 선택에 대한
  방법론 discrepancy가 발견되어(최초 OFF 지시 → 5/5, 과거 기준선은 ON → 9/9) 실행 도중 정정하고
  ON으로 재실행해 9/9·58/58 기준선과 합치시켰다. 이 판단과 근거는 본 문서 §9에 투명하게 기록했다.
- 독립 code-review 과정에서 diff 대상 파일 개수를 한 리뷰어가 78개로 오산했으나(정답은 88개),
  실제 커맨드 기반 핵심 결론(frozen path 0건, root CMake 0건 변경)에는 영향이 없었음을 재확인 후
  본 문서에 88개로 정정 기록했다.
- TSan은 Docker 기본 seccomp 제약으로 NOT_RUN — 코드 결함이 아닌 환경 제약이며, 샌드박스를
  확장하면 실행 가능하나 이번 세션 범위에서는 승인하지 않았다.
- RV1106 cross-build, RV1106 board runtime, hardware QA는 Wave 1 이력 전체에서 단 한 번도
  수행된 적이 없으며 본 Review도 예외가 아니다(저장소에 toolchain 자체가 존재하지 않음). PASS로
  추정하지 않는다.

## 17. 최종 판정

```
CRITICAL: 0
HIGH:     0
MEDIUM:   0
LOW:      2  (SENSOR-W1-INT-FIX-R1-004 유지, SENSOR-W1-INT-R3-001 신규)

SNS_STR_008_STATUS:          RESOLVED
SENSOR-W1-INT-FIX-R1-001:    RESOLVED
SENSOR-W1-INT-FIX-R1-002:    RESOLVED
SENSOR-W1-INT-FIX-R1-003:    RESOLVED
SENSOR-W1-INT-FIX-R1-004:    DEFERRED_NON_BLOCKING (Low, 상태 불변)

Allowed path violation:      0
Unreviewed code after merge: 0
Merge content drift:         0
Docker 67/67 configure/build PASS, 실행된 CTest 전체 PASS(250/250)
macOS 38/38 configure/build PASS, 필수 검증 전체 PASS
ASan diagnostics = 0, UBSan diagnostics = 0
fd/thread/lifecycle = PASS
Manifest = PASS, SHA lineage = PASS, Merge parent = PASS
```

Critical/High 없음. 유일하게 남은 finding은 기존 Low(R1-004)와 신규 Low(R3-001) 두 건뿐이며
둘 다 merge-blocking이 아니다.

```
VERDICT: PASS_WITH_NON_BLOCKING_FINDINGS

SENSOR_WAVE1_VERIFIED: YES

READY_FOR_WAVE2_BASE: YES

WAVE2_BASE_SHA: 4159395df546f1734c0b1064bdb6e091e7653e56
```
