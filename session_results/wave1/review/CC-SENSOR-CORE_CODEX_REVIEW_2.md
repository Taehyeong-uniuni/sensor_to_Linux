# CODEX CC-SENSOR-CORE REVIEW 2

## 1. 검증 대상과 provenance

- SESSION_ID: `CC-SENSOR-CORE`
- MODE: `CODEX_REVIEW_2`
- Branch: `feature/sensor-core`
- BASE_SHA: `07809cb1f3f2b86a8e92ade661c48cb3adb97b52`
- STARTING_HEAD_SHA: `c5921c02acd93c13eb675043ff8a6a76cb04b7a8`
- ORIGINAL_IMPLEMENTATION_SHA: `800207949dc28a6e18a3eafe4399f8cb0eb3d811`
- REVIEW_1_ARTIFACT_SHA: `2b1c3d5e8751c9d91431f47b8e9bfa2b4245ec3b`
- FIX_IMPLEMENTATION_SHA: `0f01c25f3c7ae55ca0e5650ce69015006ab1ef6e`
- FIX_REPORT_SHA: `c5921c02acd93c13eb675043ff8a6a76cb04b7a8`
- REREVIEW_TARGET_SHA: `0f01c25f3c7ae55ca0e5650ce69015006ab1ef6e`
- REREVIEW_REVIEWED_HEAD: `c5921c02acd93c13eb675043ff8a6a76cb04b7a8`
- Fix result 경로: `session_results/wave1/review/CC-SENSOR-CORE-CODEX-FIX-RESULT.md`
- Android Sensor 근거: `savvy_sensor@48e2d1442cd867cc60f8ff3186d53fce1c08f308`
- Android MGR 근거: `savvy_mgr@ad83cabebe7643e9eec5c0e75c1c797af30d357a`

검증은 `git archive`로 REREVIEW_TARGET_SHA의 exact tree를 `/tmp`에 분리한 뒤 수행했다. Production 및 test source는 수정하지 않았다. Android 근거도 위 pinned Git object에 대한 `git show`/`git grep`만 사용했다.

## 2. 시작 전 전제조건

| 항목 | 결과 | 근거 |
|---|---|---|
| `pwd -P` / toplevel | PASS | 둘 다 `/Users/juganghyeon/Desktop/uniuni/projects/worktrees/sensor-core` |
| branch | PASS | `feature/sensor-core` |
| tracked staged/modified | PASS | 없음 |
| Fix result 제목 검색 | PASS | 정확히 1개 |
| BASE → original | PASS | ancestor 확인 |
| original → Review 1 | PASS | ancestor 확인 |
| Review 1 → fix | PASS | ancestor 확인 |
| fix → fix report | PASS | ancestor 확인 |
| fix report → starting HEAD | PASS | 동일 commit |
| fix 이후 production/test 변경 | PASS | session result와 Fix result Markdown만 변경 |

## 3. Scope 검사

`800207949dc28a6e18a3eafe4399f8cb0eb3d811..0f01c25f3c7ae55ca0e5650ce69015006ab1ef6e`에는 17개 경로가 나타난다. 이 중 Review 1 문서는 중간 문서 commit에서 추가됐고, fix commit 자체는 Review 1을 변경하지 않았다. fix commit 자체의 변경은 allowed path 16개다.

| 검사 | 결과 |
|---|---:|
| Allowed path violations | 0 |
| Contract changes (`contracts/**`) | 0 |
| Foundation changes (`src/core/**`, `src/protocol/**`, platform interface/IPC, `third_party/**`) | 0 |
| Root CMake / preset / `cmake/**` changes | 0 |
| Other session changes | 0 |
| Review 1 changed by fix commit | 0 |
| Fix result changed by target fix commit | 0; fix report commit에서 신규 추가 |
| Generated/binary artifacts committed | 0 |
| Symlink/path escape | 0 |
| Production mock linkage | 0 |
| `git diff --check` | PASS |

## 4. Pinned Android 대조

- Sensor `MainActivity.onCreate()`는 `ThreadPirOut.start()`를 211~212행에서 먼저 실행하고 Config/Device cache를 214~215행에서 읽는다.
- Sensor `actionConfig()`는 수신 JSON을 저장한 뒤 `gson.fromJson()`이 만든 새 DTO를 대입한다. `actionDevice()`도 같은 방식으로 새 Device DTO를 대입한다.
- startup Device만 9개 상태 필드를 0으로 만들고 runtime Device에는 그 reset을 적용하지 않는다.
- Sensor는 service 연결 직후 CONNECT를 송신한다.
- MGR `actionConnectIpc()`는 Config를 먼저, Device를 다음으로 송신한다.

Fix의 Config/Device default 기반 full replacement, raw JSON snapshot, startup-only 9-field reset 및 Config→Device 수신 순서는 이 근거와 일치한다.

## 5. Review 1 finding 재판정

### 요약

| Finding | 기존 severity | 판정 | 핵심 근거 |
|---|---|---|---|
| `CDX-W1-SENSOR-CORE-001` | Critical | **PARTIALLY_RESOLVED** | 일반 stop/destroy pinning은 개선됐지만 callback-stop 뒤 외부 destroy가 detached worker cleanup보다 먼저 free할 수 있음 |
| `CDX-W1-SENSOR-CORE-002` | High | **RESOLVED** | Config/Device가 defaults에서 새로 parse되고 typed/raw snapshot을 원자적으로 publish |
| `CDX-W1-SENSOR-CORE-003` | High | **RESOLVED** | send/recv/close가 원 transport에 대해 `state_lock -> io_lock`으로 직렬화 |
| `CDX-W1-SENSOR-CORE-004` | High | **PARTIALLY_RESOLVED** | 실패 close/retry와 callback 억제는 구현됐지만 handshake 전 `connected=true`가 공개됨 |
| `CDX-W1-SENSOR-CORE-005` | High | **PARTIALLY_RESOLVED** | real action order/child status 등은 강화됐지만 required assertion 일부가 여전히 간접적/누락 |
| `CDX-W1-SENSOR-CORE-006` | Medium | **RESOLVED** | hook snapshot 후 mutex를 풀고 callback 호출; start/config/shutdown 재진입 CTest 통과 |
| `CDX-W1-SENSOR-CORE-007` | Medium | **PARTIALLY_RESOLVED** | test 수/Android/sanitizer는 재현됐지만 결과 문서 SHA placeholder와 coverage 과장 잔존 |

### CDX-W1-SENSOR-CORE-001 — PARTIALLY_RESOLVED

확인된 개선:

- global registry의 API pin과 destroy claim이 일반 concurrent stop/stop, stop/destroy, destroy/destroy 중 storage 조기 free를 방지한다.
- 외부 stop은 worker join 뒤 transport를 닫는다.
- Docker ASan 전체 feature 15/15, 그중 mgr_ipc 5/5가 통과했다.

남은 Critical 경로:

1. worker callback이 `stop()`을 호출하면 `stop_impl()`은 worker를 detach하고 `worker_detached=true`로 만든 뒤 즉시 callback으로 돌아간다(`mgr_ipc_client.c:183-216`).
2. worker terminal path는 `started=false`, `stop_complete=true`를 publish하고 condition을 먼저 깨운다(`mgr_ipc_client.c:145-158`).
3. 그 뒤에야 cancel source를 destroy한다(`mgr_ipc_client.c:160-162`).
4. callback-stop과 동시에 외부 `destroy()`가 대기 중이면 `stop_complete`를 본 외부 thread가 `finalize_destroy()`로 mutex/condition/storage를 파괴할 수 있다(`mgr_ipc_client.c:378-406,539-545`). detached worker는 아직 `client->cancel_source`를 사용 중이므로 UAF/teardown race가 성립한다.
5. non-detached concurrent stop에서도 waiting stop caller가 `stop_complete`를 보고 반환한 뒤 다른 thread가 `start()`하면, 기존 cancel source destroy와 새 init이 경쟁할 수 있다(`mgr_ipc_client.c:222-232`).

기존 test의 callback-stop case는 외부 destroy 전에 고정 `100 ms`를 기다려 이 경로를 피한다(`test_mgr_ipc_client.c:558-585`). 따라서 ASan PASS는 이 interleaving의 반증이 아니다.

필요 재검증: callback-stop과 즉시 concurrent external destroy, concurrent stop 반환 직후 restart, terminal cleanup 완료 barrier를 ASan/TSan 가능 환경에서 반복 검증.

### CDX-W1-SENSOR-CORE-002 — RESOLVED

- Config와 Device runtime apply는 매번 Foundation defaults로 scratch를 초기화한 뒤 parse한다.
- 성공한 typed value와 byte-exact raw JSON을 동일 snapshot allocation에 담는다.
- parse/publish 실패 시 기존 snapshot과 `out_result`를 유지한다.
- full→partial, raw JSON, invalid/duplicate, Device full→partial 기존 CTest가 직접 assertion하고 macOS/Docker에서 통과했다.

### CDX-W1-SENSOR-CORE-003 — RESOLVED

- public send, worker recv, close가 transport 복사본을 사용하지 않고 동일 원본 transport를 `state_lock -> io_lock` 아래 사용한다.
- send/shutdown 250-cycle stress가 stop과 destroy 양쪽을 실행하며 macOS/Docker 및 Docker ASan/UBSan에서 통과했다.
- 이 판정은 finding 001의 별도 terminal cleanup race를 해결됐다고 간주하지 않는다.

### CDX-W1-SENSOR-CORE-004 — PARTIALLY_RESOLVED

확인된 개선:

- CONNECT timeout/closed/I/O 실패는 close와 reconnect로 이어지고 `on_connected` 및 recv polling 전에 차단된다.
- 세 실패 종류와 retry/close-count CTest가 통과했다.

남은 High 동작:

- `set_connected()`가 `client->connected=true`를 먼저 publish한다(`mgr_ipc_client.c:435-439`).
- worker는 그 다음에 CONNECT를 송신한다(`mgr_ipc_client.c:485-495`).
- 따라서 handshake send가 진행 중인 동안 `is_connected()`는 true를 반환할 수 있고, public application send도 같은 transport로 CONNECT보다 먼저 전송될 수 있다.
- test fake는 handshake 실패를 즉시 반환하며, handshake를 block한 상태에서 `is_connected()`/일반 send를 호출하지 않는다.

기대 동작은 CONNECT 성공 전까지 public connected state와 일반 outbound send를 노출하지 않는 것이다. 필요 재검증은 handshake send를 제어 가능하게 block한 뒤 state/send 순서를 직접 assertion하는 것이다.

### CDX-W1-SENSOR-CORE-005 — PARTIALLY_RESOLVED

확인된 개선:

- mock MGR가 CONNECT action과 `{}` payload를 parse/assert한다.
- real integration이 첫 연결과 reconnect 모두 Config→Device action 순서 및 child exit code 0을 assert한다.
- malformed, wrong-direction, invalid-payload, oversized-record 뒤 valid recovery를 fake transport에서 검증한다.
- stop 후 fd가 baseline tolerance로 돌아오는 assertion이 추가됐다.

남은 assertion 공백:

- 65,536/65,537 test는 `savvy_ipc_envelope_build()`만 직접 호출한다. `sensor_mgr_ipc_client_send()`나 real transport를 통과하지 않으며, 사용한 CONNECT payload도 public send의 action payload validation을 통과할 형태가 아니다.
- pre-connect drop test는 reconnect하지 않는다. real reconnect test는 pre-connect send를 먼저 시도하지 않는다. 따라서 “drop된 state가 reconnect 뒤 미재전송”을 하나의 경로에서 직접 assertion하지 않는다.
- real integration의 “unexpected Sensor message 없음”은 client에 cached Config/Device나 dropped outbound를 준비하지 않은 상태에서 검사한다.
- fd는 측정하지만 worker thread count의 baseline/최종 복귀를 측정하지 않는다.

따라서 CTest 6/6은 실행상 PASS지만 Review 1에 명시된 required IPC test 범위를 전부 충족한 PASS로 인정할 수 없다.

### CDX-W1-SENSOR-CORE-006 — RESOLVED

- registry mutex 아래 hook 배열을 value snapshot으로 복사한 뒤 mutex를 풀고 callback을 호출한다.
- start/config/shutdown callback 재진입과 callback-time registration이 기존 health CTest에서 반환 및 순서를 직접 assertion한다.
- 새로 발견한 `callback_depth` 초기화 문제는 deadlock 자체와 구분해 신규 finding으로 기록한다.

### CDX-W1-SENSOR-CORE-007 — PARTIALLY_RESOLVED

정정되어 독립 재현된 내용:

- original diff 37 files
- feature-local 기본 CTest 15개
- real mgr_ipc variant 6개
- Android startup 순서
- Docker mgr_ipc ASan/UBSan 5/5 및 TSan runtime 제약

남은 불일치:

- current session result의 `Fix-result report SHA`가 `PENDING_FIX_REPORT_COMMIT`이다.
- Fix result 자체의 `Report SHA`도 `PENDING_FIX_REPORT_COMMIT`이다.
- session result는 65,536 acceptance/65,537 pre-send rejection과 unexpected replay 검증을 완료한 것으로 서술하지만 실제 assertion 범위는 finding 005와 같다.
- `fd/thread lifecycle` 서술과 달리 thread count는 측정하지 않는다.

## 6. 신규 finding

### CDX-W1-SENSOR-CORE-R2-001

- Severity: **High**
- 제목: lifecycle의 `callback_depth`가 초기화되지 않아 fan-out과 destroy가 미정 값에 의존함
- Affected file/function:
  - `src/features/health/sensor_lifecycle.h:61-67` (`sensor_lifecycle_t`)
  - `src/features/health/sensor_lifecycle.c:8-20` (`snapshot_modules`, `finish_module_snapshot`)
  - `src/features/health/sensor_lifecycle.c:24-40` (`sensor_lifecycle_init`)
  - `src/features/health/sensor_lifecycle.c:43-57` (`sensor_lifecycle_destroy`)
- 확인 근거: struct에는 `size_t callback_depth`가 추가됐지만 init은 modules와 `module_count`만 초기화한다. 기존 tests도 `sensor_lifecycle_t lc;` 자동 객체를 별도 zero-init 없이 init 함수에 전달한다.
- 관찰된 동작: 첫 fan-out의 `callback_depth += 1`이 미정 값을 read-modify-write하고, destroy는 그 미정 값에서 유래한 값으로 active callback 여부를 판정한다. 이는 C 의미상 정의되지 않은 동작이다. 현재 CTest 통과는 해당 stack bytes가 우연히 기대값처럼 보인 실행 결과일 뿐이다.
- 기대 동작: init 성공 시 `callback_depth`가 항상 0이어야 하며, 모든 fan-out depth와 destroy 판단이 그 정의된 초기 상태에서 시작해야 한다.
- 영향: destroy가 영구적으로 no-op가 되어 lifecycle/mutex 자원이 남거나, 잘못된 depth 판단으로 active snapshot 중 lock/base lifecycle을 파괴할 수 있다.
- 최소 수정 범위: `sensor_lifecycle_init()`의 필드 초기화와 health test의 non-zero-poisoned storage init 검증. 필요하면 callback-time destroy 정책도 명시적으로 assertion한다.
- 필요한 재검증: start/config/shutdown 재진입, nested fan-out, callback-time destroy, 일반 destroy를 macOS와 Docker에서 반복 실행. 가능한 uninitialized-memory detector가 있으면 함께 실행한다.

## 7. Required 동작 및 기존 test assertion 판정

| 영역/Test ID | CTest 실행 | 독립 판정 | 비고 |
|---|---|---|---|
| SNS-CORE-001 | PASS | PASS | startup Device 9-field reset과 state report |
| SNS-CORE-002 | PASS | PASS | Config full replacement/raw JSON |
| SNS-CORE-002a | PASS | PASS | raw mode 변환 |
| SNS-CORE-003 | PASS | PASS | mode state 및 IPC 역할 |
| SNS-CORE-003a | PASS | PASS | 연결 전 drop |
| SNS-CORE-003b | PASS | INVALID_TEST_EXPECTATION | drop→reconnect→미재전송을 한 경로에서 assertion하지 않음 |
| CT-IPC-002 fake/real | PASS | INVALID_TEST_EXPECTATION | action order 등은 개선됐지만 boundary/replay/thread 요구 일부 누락 |
| SNS-CORE-004 | PASS | PASS | update guard |
| SNS-CORE-005 | PASS | PASS | malformed Config/Device atomicity와 full replacement |
| SNS-CORE-006 | PASS | INVALID_TEST_EXPECTATION | post-stop fd는 확인, thread count는 미측정 |
| SNS-CORE-007 | PASS | FAIL | callback stop/destroy cleanup race와 신규 lifecycle 초기화 결함 잔존 |

## 8. macOS arm64 실행 결과

환경: Darwin arm64, AppleClang 17.0.0, CMake 4.4.0.

| 항목 | 결과 |
|---|---|
| 6개 feature-local Debug configure/build | PASS |
| feature-local `ctest -N` | 15 tests |
| feature-local CTest | 15/15 PASS |
| root Debug regression | 5/5 PASS |
| 6개 feature-local Release tests-off build | PASS |
| root Release tests-off build | PASS |
| `-Wall -Wextra -Werror` | PASS, production warning 0 |
| Sensor-core consolidated target/script | NOT_RUN — repository에 존재하지 않음 |
| real AF_UNIX `SOCK_SEQPACKET` | NOT_RUN — Darwin 미지원 |
| UBSan feature-local | 15/15 PASS |
| ASan feature-local | NOT_PERFORMED — Config CTest runtime hang, 임시 프로세스 종료 |
| TSan | NOT_PERFORMED |

macOS sanitizer configure에서 third-party cJSON의 deprecated `sprintf` warning이 보였지만 sanitizer CFLAGS를 전역 주입한 경로의 third-party warning이며 target production source warning은 아니다. 일반 `-Werror` build는 통과했다.

## 9. Docker Ubuntu 22.04 arm64 실행 결과

환경: local `savvy-foundation-test:ubuntu22.04-arm64-v1`, Linux aarch64, GCC 11.4.0, CMake 3.22.1. Exact fix archive를 read-only mount했다.

| 항목 | 결과 |
|---|---|
| 6개 feature-local Debug configure/build | PASS |
| feature-local `ctest -N` | 15 tests |
| feature-local CTest | 15/15 PASS |
| root Debug real-transport regression | 9/9 PASS |
| root Release real-transport regression | 9/9 PASS |
| 6개 feature-local Release tests-off build | PASS |
| root Release tests-off real-transport build | PASS |
| mock MGR + real mgr_ipc variant | 6/6 PASS |
| `-Wall -Wextra -Werror` | PASS, warning 0 |
| Sensor-core consolidated target/script | NOT_RUN — repository에 존재하지 않음 |

## 10. Sanitizer

| 환경 | ASan | UBSan | TSan | 근거 |
|---|---|---|---|---|
| macOS arm64 | NOT_PERFORMED | PASS (15/15) | NOT_PERFORMED | ASan Config CTest가 hang하여 종료; UBSan 6 feature 실행 |
| Ubuntu 22.04 arm64 Docker | PASS (15/15) | PASS (15/15) | NOT_PERFORMED | TSan binary build는 성공했으나 5/5 모두 테스트 코드 진입 전 `personality(... ADDR_NO_RANDOMIZE) ... -1` runtime CHECK로 종료 |

ASan/UBSan은 uninitialized scalar read를 보장해 검출하는 도구가 아니므로 신규 `callback_depth` finding의 반증으로 사용하지 않는다. TSan은 test body를 실행하지 못했으므로 PASS로 간주하지 않는다.

## 11. 실행하지 못한 항목

- Sensor-core consolidated build/test: repository에 해당 target이나 session-specific script가 없음
- macOS real `SOCK_SEQPACKET`: platform 미지원
- macOS ASan 전체 suite: runtime hang
- macOS TSan: 미수행
- Docker TSan test body: runtime 초기화 제약
- `RV1106_CROSS_BUILD`: `NOT_PERFORMED`
- `RV1106_BOARD_RUNTIME`: `NOT_PERFORMED`
- `HARDWARE_QA`: `NOT_PERFORMED`

## 12. 최종 판정

- EXISTING_FINDINGS:
  - RESOLVED: 3 (`002`, `003`, `006`)
  - PARTIALLY_RESOLVED: 4 (`001`, `004`, `005`, `007`)
  - UNRESOLVED: 0
  - NOT_REPRODUCED: 0
  - BLOCKED: 0
- NEW_FINDINGS:
  - Critical: 0
  - High: 1 (`CDX-W1-SENSOR-CORE-R2-001`)
  - Medium: 0
  - Low: 0
- VERDICT: **FAIL**
- MERGE_CANDIDATE: **NO**

판정 사유는 기존 Critical finding 001과 기존 High findings 004/005가 partially resolved 상태이고, 신규 High finding 1개가 존재하기 때문이다. 모든 일반 CTest 통과는 확인했지만 required lifecycle/IPC 동작과 assertion 범위가 PASS 조건을 충족하지 않는다.

## 13. 최종 권고

1. detached worker가 cancel-source cleanup과 terminal handoff를 끝내기 전에는 `stop_complete`를 공개하거나 외부 destroy/start를 허용하지 않도록 lifecycle state를 보완한다.
2. CONNECT 성공 후에만 public `connected=true`를 publish하고, handshake 중 application send가 CONNECT를 앞설 수 없게 한다.
3. `callback_depth=0`을 init에서 명시하고 non-zero storage 기반 회귀 test를 추가한다.
4. pre-connect drop→reconnect→no replay, client send/real transport 65,536/65,537, 최종 thread count를 직접 assertion한다.
5. 결과 문서의 pending SHA와 assertion 범위 설명을 실제 근거에 맞게 갱신한 뒤 재검증한다.
