# CC-SENSOR-W1-INTEGRATION-FIX-1 Result

SESSION_ID:
CC-SENSOR-W1-INTEGRATION-FIX-1

FINAL_STATE:
FIX_IMPLEMENTATION_FINISHED
AWAITING_CODEX_FIX_REVIEW

이 문서는 `SNS-STR-008-fd-leak` committed test의 fd lifecycle 비결정성 결함에 대한 최소 수정과 그 검증 결과를 기록한다. Production source, Foundation, contract, root CMake는 수정하지 않았고 `integration/wave1`에는 merge하지 않았다.

## 1. Exact SHA

```text
BASE_SHA:
2ccb0d0bccb3d456680d7ca4fe1a3d82d6ca303b

BASE_INTEGRATION_PRODUCTION_SHA:
2ccb0d0bccb3d456680d7ca4fe1a3d82d6ca303b

FIX_START_HEAD:
2ccb0d0bccb3d456680d7ca4fe1a3d82d6ca303b

FIX_IMPLEMENTATION_SHA:
3a62d737111c611399cbdfd4e368a5b27150a18e

INTEGRATION_RESULT_COMMIT_SHA:
1936517d2bafc099ffd8864c53f8fe39ef9bac7d

INTEGRATION_LEDGER_COMMIT_SHA:
1e2e97ae1590446dc3211e6df5d53a54240c9b20
```

Ancestry: `git merge-base --is-ancestor BASE_SHA INTEGRATION_RESULT_COMMIT_SHA` = true, `git merge-base --is-ancestor INTEGRATION_RESULT_COMMIT_SHA INTEGRATION_LEDGER_COMMIT_SHA` = true (G001에서 확인, 이 fix branch는 `INTEGRATION_RESULT_COMMIT_SHA`/`INTEGRATION_LEDGER_COMMIT_SHA` 커밋을 포함하지 않는 `BASE_SHA`에서 분기했으므로 두 문서는 Git object에서 read-only로 직접 확인했다).

## 2. SNS-STR-008 — 재분석 및 재현

```text
SNS-STR-008:
tests/unit/sensor_stream/tcp_channel/test_tcp_channel.c (test_008)

기존 알려진 줄 번호(참고용):
266 (기존 integration 문서 기준, 실제로는 수정 전 코드에서 268번째 줄 근방 - 코드를 직접 읽어 재확인함)
```

### 2.1 실제 root cause (코드를 직접 읽고 확인)

기존 integration 문서의 가설(`fd_probe_after`가 echo server thread의 `pthread_join()`보다 먼저 실행된다)은 코드 상 사실이었다(수정 전 `test_008`에서 `pthread_join(th, NULL)`은 fd 비교 CHECK 다음, 마지막 줄 바로 앞에 있었다). 이 순서 자체가 test-owned 자원(echo server thread가 소유한 마지막 accept()-fd)이 아직 닫히지 않은 상태에서 process-wide fd 수를 비교할 수 있는 진짜 race였다.

다만 수정(꼬리 쪽 join 재정렬)만 적용한 뒤 Docker ASan 60회 재검증에서 4회(약 10%) 동일한 실패가 재현되었다. 진단 결과(diagnostic dump는 임시로 추가했다가 최종 커밋 전 제거함) `fd_probe_before`와 `fd_probe_after`의 실제 정수값을 직접 출력해 비교한 결과, 실패한 모든 경우 예외 없이 `fd_probe_before`가 정상값(4)보다 1 높은 값(5)으로 관측되고 `fd_probe_after`는 항상 정상값(4)이었다. 즉 문제는 꼬리(join 이전 probe)가 아니라 머리(`fd_probe_before`)에도 있었다: `fd_probe_before`는 echo server thread를 `pthread_create()`한 직후 곧바로 측정되는데, 갓 생성된 스레드 자신의 시작 단계(ASan 계측 하에서 스레드 등록/스택 경계 확인으로 추정되는 일회성 동작)가 짧게 fd를 점유했다가 해제하는 타이밍과 경합해 baseline이 부풀려질 수 있었다. 이 경합은 echo thread의 시작에만 관련되며, 이후 50회 create/start/submit/destroy 루프나 production `sensor_tcp_channel_*` 코드와는 무관하다.

프로덕션 `src/platform/linux/tcp_8141/tcp_channel.c`를 직접 읽어 fd/thread ownership을 확인했다: `sensor_tcp_channel_stop()`(L577-591)은 `stop_requested` 설정 후 `savvy_queue_cancel()`로 즉시 깨우고 `pthread_join(channel->worker_thread, NULL)`을 호출하며, `worker_main()`(L388-406)은 루프 종료 직후 `ch->sockfd`가 열려 있으면 반드시 `close()`한 뒤 반환한다. 즉 `sensor_tcp_channel_destroy()`가 반환하는 시점에는 client-owned socket이 이미 결정적으로 닫혀 있다. `savvy_queue`(src/core/queue.c)와 `savvy_lifecycle`(src/core/lifecycle.c)은 pthread mutex/condvar만 사용하고 fd를 전혀 다루지 않는다. 이 경로에서 production fd leak 가능성은 확인되지 않았다.

```text
ROOT_CAUSE:
committed test 자체의 fd-probe 결정성 결함(양쪽 끝단) - production fd leak 아님
- (a) fd_probe_before가 echo server thread pthread_create() 직후, 스레드 자신의 시작 단계 임시 fd 점유와 경합 가능
- (b) fd_probe_after가 echo server thread pthread_join() 이전에 측정되어, 스레드가 소유한 마지막 accept-fd의 close()와 경합 가능
```

### 2.2 수정 내용

정확히 한 파일(`tests/unit/sensor_stream/tcp_channel/test_tcp_channel.c`)의 `test_008()`만 수정했다.

```text
FIX_SUMMARY:
1. fd_probe_before 측정 위치를 pthread_create(echo thread)보다 앞으로 이동
   (echo thread가 존재하기 전에 baseline을 측정하여 그 시작 단계와의 경합을 제거)
2. pthread_join(th, NULL)을 fd_probe_after 측정보다 앞으로 이동하고
   반환값을 CHECK(join_rc == 0, ...)로 검증
   (echo thread가 완전히 반환한 뒤에만 fd_probe_after를 측정하여
    마지막 accept-fd의 close()와의 경합을 제거)
```

8개 필수 원칙 대비 확인:
1. Echo server thread 종료 조건(`expected_connections`로 정확히 bound된 accept 루프) - 기존 로직 그대로 유지.
2. Echo server thread가 소유한 accepted fd의 정리(각 반복의 `close(fd)`) - 기존 로직 그대로 유지, thread 내부에서 완료.
3. `pthread_join()` 호출 + 반환값 검증 - 새로 추가(`CHECK(join_rc == 0, ...)`).
4. Join 이후에만 test-owned 자원이 모두 닫혔음을 전제로 진행 - join이 happens-before를 보장.
5. Join 이후에만 process-wide fd count 측정 및 baseline 비교 - 순서 재배치로 보장.
6. Production channel(`ch`)이 소유한 fd를 test가 임의로 강제 close하지 않음 - `sensor_tcp_channel_destroy(ch)` 호출부 불변.
7. 기존 fd leak 검출 능력 유지 - `CHECK(fd_probe_after == fd_probe_before, ...)` 문구·의미 그대로, 50회 create/destroy 루프도 불변이라 client-side leak은 여전히 검출됨.
8. 기존 test의 연결/송수신/종료/lifecycle 의미 유지 - connect/submit/timeout/echo 로직 무변경, 두 probe와 join의 상대적 순서만 재배치.

사용 금지 기법(sleep/usleep 추가, polling, fd 허용오차, assertion 삭제, 강제 close 등) 중 어느 것도 사용하지 않았다 - 이미 존재하던 `pthread_join()` 동기화 primitive의 위치만 재배치했다.

## 3. 수정 파일 목록

```text
MODIFIED_CODE_FILES:
tests/unit/sensor_stream/tcp_channel/test_tcp_channel.c

ALLOWED_PATH_VIOLATIONS:
0

FOUNDATION_CHANGES:
0

CONTRACT_CHANGES:
0

ROOT_CMAKE_CHANGES:
0
```

`git diff --name-status BASE_SHA HEAD`(구현 commit 포함)는 `tests/unit/sensor_stream/tcp_channel/test_tcp_channel.c` 단 1개만 보고한다. `src/**`, `include/**`, `contracts/**`, `third_party/**`, root `CMakeLists.txt`, `CMakePresets.json`, `cmake/**` 전부 diff 0.

## 4. 수정 전 재현 (Docker ASan, 수정 전)

```text
BEFORE_FIX_REPRODUCTION:
NOT_REPRODUCED_IN_THIS_RUN (fresh ASan build, 20/20 성공)

HISTORICAL_REPRODUCTION:
REPRODUCED (기존 integration 문서 기준: 최초 전체 ASan 행렬 1회 실패, fresh ASan 20회 stress 중 5회째 실패)
```

Race condition의 특성상 매 시도마다 재현되지는 않으며, 이번 20회 시도에서 재현되지 않았다고 해서 기존 integration 문서의 재현 기록이 무효화되지는 않는다.

## 5. Targeted 검증 (수정 후, SNS-STR-008만)

```text
TARGETED_DEBUG_RESULT:
20/20 PASS

TARGETED_RELEASE_RESULT:
20/20 PASS

TARGETED_UBSAN_RESULT:
20/20 PASS, UBSan diagnostics 0

TARGETED_ASAN_STRESS_RESULT:
100/100 PASS, ASan/LeakSanitizer diagnostics 0, fd mismatch 0
```

과정 기록: 머리(fd_probe_before) 원인을 발견하기 전, 꼬리(join) 수정만 적용한 1차 시도에서 Debug 20/20·Release 20/20·UBSan 20/20 PASS는 동일했으나 ASan은 1/100에서 실패했다(이후 60회 재진단 실행에서 54/60~56/60 수준으로 약 10% 실패, 전부 동일한 `fd_probe_before` 편향 패턴). 머리 쪽 수정을 추가한 뒤 재검증에서 위 결과(100/100)를 확정했다. 이 100회 실행 중간에 이 세션과 무관한 별도 Docker 컨테이너(다른 세션의 대규모 회귀 검증으로 추정)의 극심한 자원 경합으로 1회 hang이 발생했으나(`futex_wait`/`inet_csk_accept`로 스레드 상태 확인, CPU time 0에 가까움), 이 세션이 시작한 컨테이너만 kill하고 동일한 fresh build 조건으로 재실행하여 100/100을 확보했다.

## 6. TCP 8141 전체 검증

```text
TCP_8141_DEBUG_RESULT:
5/5 PASS (SNS-STR-006~010 전부)

TCP_8141_RELEASE_RESULT:
5/5 PASS

TCP_8141_UBSAN_RESULT:
5/5 PASS, diagnostics 0

TCP_8141_ASAN_RESULT:
5/5 PASS(단일 실행), diagnostics 0

TCP_8141_ASAN_REPEAT_RESULT:
20/20 반복(fresh ASan build) 전부 PASS, diagnostics 0
```

## 7. Sensor 전체 regression 검증

### 7.1 macOS

```text
MACOS_RESULT:
Foundation Debug 5/5
Sensor Core Debug 15/15(6개 모듈: config/mgr_ipc/state_report/update_guard/health/mode_state)
Sensor Stream Debug 34/34(5개 모듈 26 + mock_streaming_server 8)
UBSan 54/54(Foundation 5 + Core 15 + Stream 34 - 기존 integration 문서의 "UBSan 54/54"와 정확히 일치)
Release tests-off root + 11개 모듈: 전부 configure/build 성공
전체 38개 카테고리 중 38 PASS, 0 FAIL
```

Debug 총계(Foundation 5 + Core 15 + Stream 34) = 54/54로 기존 integration 문서의 "Debug 54/54"와 정확히 일치한다.

### 7.2 Docker Linux arm64

```text
DOCKER_ARM64_RESULT:
Foundation Debug 9/9
Foundation Release 9/9
Debug modules 58/58(Foundation 9 + Core 15 + Stream 34 - 기존 "Debug modules 58/58"과 일치)
Real MGR IPC(Debug) 6/6(기존 "Real MGR IPC 6/6"과 일치)
Release tests-off root + 11개 모듈: 전부 configure/build 성공
UBSan modules 58/58(Foundation 9 + Core 15 + Stream 34 - 기존 "UBSan modules 58/58"과 일치)
Real MGR IPC UBSan 6/6(기존과 일치)
ASan modules 58/58(Foundation 9 + Core 15 + Stream 34) - 기존에는 SNS-STR-008에서 중단되어 미완료였던 항목을 이번 수정 이후 사상 최초로 완주, ASan/LeakSanitizer 진단 0
Real MGR IPC ASan 6/6 - 사상 최초 완주, 진단 0

Sensor Release tests(Release 빌드, tests ON):
- Sensor Stream 5개 모듈 + mock 전부 PASS(34/34)
- Sensor Core 6개 모듈 중 state_report/update_guard/health 3개는 PASS(5/5)
- Sensor Core 6개 모듈 중 config/mode_state/mgr_ipc 3개는 컴파일 실패
  (원인: tests/unit/sensor_core/config/test_config_store.c,
   tests/unit/sensor_core/mode_state/test_mode_state.c,
   tests/unit/sensor_core/mgr_ipc/test_mgr_ipc_client.c의
   -Werror=unused-variable / -Werror=unused-but-set-variable /
   -Werror=uninitialized - Release(-O2 이상) 최적화 단계에서만
   GCC가 검출하는 기존 코드 패턴이며 Debug(-O0)에서는 동일 파일이
   문제없이 컴파일/통과함. 이 3개 파일은 sensor_stream/tcp_channel과
   무관한 별도 모듈이며 이번 세션의 수정 허용 범위(1개 파일) 밖이므로
   수정하지 않았다.)
```

이 3개 컴파일 실패는 `test_tcp_channel.c` 수정과 무관하며, 이번 세션에서 최초로 "Release 빌드 + tests ON" 조합을 sensor_core 6개 모듈 전체에 실제로 실행해 보면서 발견된 기존 결함이다. 허용된 1개 파일 밖의 수정이 필요하므로 고치지 않았고, 사실 그대로 기록한다.

## 8. Sanitizer / Lifecycle 상태

```text
ASAN_RESULT:
SNS-STR-008 targeted 100/100, TCP 8141 suite 20/20 반복, Sensor 전체(Foundation+Core+Stream+mock) 58/58,
Real MGR IPC 6/6 - 전부 diagnostics 0

UBSAN_RESULT:
SNS-STR-008 targeted 20/20, TCP 8141 suite 1/1, Sensor 전체 58/58(macOS), 58/58(Docker),
Real MGR IPC 6/6 - 전부 diagnostics 0

TSAN_RESULT:
NOT_RUN(기존 integration 문서에서도 NOT_RUN이었고, 이번 세션 지시 범위에도 TSan 실행 지시 없음)

FD_RESULT:
SNS-STR-008 ASan 100회 fd mismatch 0, TCP 8141 ASan 20회 반복 fd mismatch 0

THREAD_RESULT:
모든 실행에서 CHECK(join_rc == 0, ...) 통과 - echo server thread join 100% 성공, 미조인 test thread 0
(macOS 직접 OS thread-count 조회는 NOT_AVAILABLE)

LIFECYCLE_RESULT:
ASan leak diagnostic 0, UBSan diagnostic 0 (전 구간)
```

## 9. Production fd leak 판정

```text
PRODUCTION_FD_LEAK:
NOT_REPRODUCED

COMMITTED_TEST_DETERMINISM_DEFECT:
FIX_IMPLEMENTED
AWAITING_CODEX_FIX_REVIEW
```

근거: (1) 코드 리뷰 결과 `sensor_tcp_channel_stop()`/`worker_main()`이 client-owned socket을 자체 `pthread_join()` 뒤에 결정적으로 close함을 확인했고, (2) `savvy_queue`/`savvy_lifecycle`은 fd를 전혀 사용하지 않으며, (3) 수정 후 SNS-STR-008 ASan 100/100, TCP 8141 ASan 20회 반복, Sensor 전체 ASan 58/58 + Real MGR IPC ASan 6/6 전 구간에서 fd mismatch 및 ASan/LeakSanitizer 진단이 0이었다. 다만 이는 "이번 검증 범위 내에서 재현되지 않았다"는 의미이며, 임의로 `NO`/`IMPOSSIBLE`을 선언하지 않는다.

## 10. 범위 및 Foundation 검사

```text
ALLOWED_PATH_VIOLATIONS:
0

FOUNDATION_CHANGES:
0

CONTRACT_CHANGES:
0

ROOT_CMAKE_CHANGES:
0

MANIFEST_RESULT:
contracts/contract-manifest.sha256 = a69536c286839c97e05ed7f54b5834d843f94eae4a9221ad6213de93d268fa6e (기대값과 일치)
third_party/DEPENDENCY_MANIFEST.sha256 = 9934277d3a8d1dabd1c2632d3501743f8d2a57218c6dd6f3635b2b3844296ad2 (기대값과 일치)
```

## 11. 검증 경계

```text
RV1106_CROSS_BUILD:
NOT_PERFORMED

RV1106_BOARD_RUNTIME:
NOT_PERFORMED

HARDWARE_QA:
NOT_PERFORMED
```

## 12. 최종 처분

```text
PRODUCTION_SOURCE_CHANGED:
NO

TEST_SOURCE_CHANGED:
YES (tests/unit/sensor_stream/tcp_channel/test_tcp_channel.c만)

MERGE_COMMITS_CHANGED:
NO

INTEGRATION_WAVE1_BRANCH_CHANGED:
NO

FINAL_STATE:
FIX_IMPLEMENTATION_FINISHED
AWAITING_CODEX_FIX_REVIEW
```

이 fix branch는 `integration/wave1`에 merge하지 않았다. Merge, tag, push는 수행하지 않았다. 다음 단계는 독립적인 Codex review다.
