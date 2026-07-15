# CC-SENSOR-CORE Codex Fix Result 2

## 1. 검증 기준

- Session: `CC-SENSOR-CORE-FIX-2`
- Branch: `feature/sensor-core`
- Worktree: `/Users/juganghyeon/Desktop/uniuni/projects/worktrees/sensor-core`
- Governing Review 2: `session_results/wave1/review/CC-SENSOR-CORE_CODEX_REVIEW_2.md`
- Production/Test Fix commit 변경 파일: 11개
- Fix Result 2를 포함한 Fix 2 고유 변경 파일: 12개

## 2. Exact SHA 원장

| 단계 | Exact SHA | 조회 명령 | 상태 |
|---|---|---|---|
| BASE | `07809cb1f3f2b86a8e92ade661c48cb3adb97b52` | `git rev-parse 'contract-v1^{commit}'` | 확인 |
| 원 구현 | `800207949dc28a6e18a3eafe4399f8cb0eb3d811` | `git rev-parse '800207949dc28a6e18a3eafe4399f8cb0eb3d811^{commit}'` | 확인 |
| Review 1 artifact | `2b1c3d5e8751c9d91431f47b8e9bfa2b4245ec3b` | `git rev-parse '2b1c3d5e8751c9d91431f47b8e9bfa2b4245ec3b^{commit}'` | 확인 |
| Fix 1 구현 | `0f01c25f3c7ae55ca0e5650ce69015006ab1ef6e` | `git rev-parse '0f01c25f3c7ae55ca0e5650ce69015006ab1ef6e^{commit}'` | 확인 |
| Fix 1 보고 | `c5921c02acd93c13eb675043ff8a6a76cb04b7a8` | `git rev-parse 'c5921c02acd93c13eb675043ff8a6a76cb04b7a8^{commit}'` | 확인 |
| Review 2 최초 artifact | `74b4096713e465ef0855fcf0fd707847c142101c` | `git log --follow --diff-filter=A --format='%H' -- session_results/wave1/review/CC-SENSOR-CORE_CODEX_REVIEW_2.md \| tail -n 1` | 확인 |
| Review 2 최신 파일 | `74b4096713e465ef0855fcf0fd707847c142101c` | `git log -1 --format='%H' -- session_results/wave1/review/CC-SENSOR-CORE_CODEX_REVIEW_2.md` | 확인 |
| Fix 2 시작 HEAD | `e135d6aa08f57341087946c412ca8d9cccc324ae` | 세션 시작 시 `git rev-parse HEAD` | 확인 |
| Fix 2 구현 | `0aef477915b1481b4a93aa0e09670375612b58d9` | Production/Test commit 뒤 `git rev-parse HEAD` | 확인 |
| Fix 2 보고 artifact | `PENDING_REPORT_ARTIFACT_COMMIT` | artifact commit 뒤 `git rev-parse HEAD` | 다음 단계에서 확정 |

## 3. Finding 상태

| Finding ID | Review 2 상태 | 수정 상태 | 변경 파일 | 검증 test |
|---|---|---|---|---|
| `CDX-W1-SENSOR-CORE-001` | `PARTIALLY_RESOLVED` | `FIXED` | `mgr_ipc_client.c/.h`, mgr_ipc unit fake/test | `SNS-CORE-007-mgr-ipc` |
| `CDX-W1-SENSOR-CORE-004` | `PARTIALLY_RESOLVED` | `FIXED` | `mgr_ipc_client.c/.h`, mgr_ipc unit fake/test | `CT-IPC-002` |
| `CDX-W1-SENSOR-CORE-005` | `PARTIALLY_RESOLVED` | `FIXED` | mgr_ipc CMake/unit/integration test, fake transport | `SNS-CORE-003b-mgr-ipc`, `SNS-CORE-006`, `CT-IPC-002`, `CT-IPC-002-real` |
| `CDX-W1-SENSOR-CORE-007` | `PARTIALLY_RESOLVED` | `FIXED` | `session_results/wave1/CC-SENSOR-CORE.md`, 이 문서 | Git/CTest/sanitizer 결과 대조 |
| `CDX-W1-SENSOR-CORE-R2-001` | `NEW HIGH` | `FIXED` | `sensor_lifecycle.c/.h`, health unit test | `SNS-CORE-007-health` Debug·Release |

Review 2에서 `RESOLVED`였던 `002`, `003`, `006`은 설계를 다시 열지 않고 Config/Device replacement·raw JSON, transport lifetime, callback lock 분리 regression을 수행했다.

## 4. 코드 변경

- `001 terminal cleanup barrier`: callback stop은 self-join하지 않고 joinable worker를 남긴다. 외부 terminal operation 하나만 `join_in_progress`를 소유하며 worker 실행, transport close, cancel-source destroy가 끝난 뒤 `stop_complete`를 공개한다. callback destroy는 안전한 경우에만 detach terminal ownership을 취하며 외부 join과 경합하면 stop-only로 축소된다.
- `004 handshake state`: `DISCONNECTED`, `TRANSPORT_CONNECTED`, `HANDSHAKING`, `CONNECTED`, `STOPPING` 상태를 분리했다. CONNECT build/send 성공 뒤에만 public connected 및 callback을 공개한다.
- `005 direct assertions`: 실제 pre-connect drop과 state dedup을 같은 test path에서 연결하고, public send 경계와 Linux fd/thread baseline을 직접 측정한다.
- `007 report correction`: Fix 1 보고 SHA, Review 2 SHA, 파일 수, CTest 수, sanitizer 결과와 미수행 범위를 실제 결과로 교정했다.
- `R2-001 callback_depth`: primitive init 전 hook storage, `module_count`, `callback_depth`를 정의된 0으로 만들고 실패 시 destroy 가능 범위를 문서화했다.

## 5. Concurrency

- callback-stop + 즉시 concurrent external destroy, callback-stop + 즉시 external restart, callback-destroy + concurrent stop을 condition-variable barrier로 강제했다.
- callback terminal scenario를 500회 반복하고, stop/stop·stop/destroy·destroy/destroy를 별도 500회 반복했다.
- blocked connect와 blocked recv에서 외부 stop/destroy가 worker terminal cleanup 전 storage를 해제하지 않음을 검사했다.
- callback race 회피용 고정 `sleep(100ms)`는 제거했다. callback 진행과 release는 observable barrier로 제어한다.
- transport send/recv는 `state_lock → io_lock`으로 진입한 뒤 `io_lock`이 원 transport 수명을 I/O 반환까지 고정한다. close는 worker terminal path에서 I/O 종료 뒤 한 번 수행된다.
- Ubuntu `SNS-CORE-006`은 `/proc/self/task`로 baseline/worker/final thread 수를 직접 측정하고 500회 connect/disconnect 뒤 baseline 복귀를 assertion한다. fd도 시작 전·최종 baseline을 비교한다.

## 6. IPC

- blocking handshake 중 public `is_connected`는 false, connected callback 수는 0, application send는 `SAVVY_ERR_NOT_CONNECTED`이며 transport outbound는 CONNECT 한 건뿐임을 직접 검사했다.
- handshake release 및 성공 뒤 connected callback과 application send가 허용되며 CONNECT보다 앞선 application outbound가 없음을 send count와 fake MGR 수신으로 검사했다.
- CONNECT timeout, closed, I/O error, protocol error 각각은 transport close 뒤 reconnect 성공을 검사한다.
- 시작 전 State/Property/Alert/Upload 실제 send는 모두 drop되고, local State dedup은 두 번째 API 호출 자체를 억제한다. connect/reconnect에서 outbound send count는 CONNECT 2회뿐이고 Config·Device는 각 연결에서 MGR→Sensor 순서로만 수신된다.
- real mock MGR는 CONNECT 뒤 예상하지 않은 Sensor outbound를 실패 처리한다. child exit 0으로 pre-connect drop 및 cached Config/Device reverse replay 부재를 검사했다.
- 최종 encoded Upload가 정확히 65,536 bytes이면 `sensor_mgr_ipc_client_send()`와 transport send를 통과한다. 65,537 bytes이면 transport send count 증가 없이 `SAVVY_ERR_OVERFLOW`다.
- wrong-direction action, invalid payload, malformed envelope, oversized record 폐기 및 다음 정상 record 복구를 직접 검사한다.

## 7. Lifecycle

- `0xA5`로 오염한 `sensor_lifecycle_t`를 100회 초기화하고 `callback_depth == 0`, `module_count == 0`을 직접 검사한다.
- start/config/shutdown fan-out, nested config fan-out, callback 중 destroy 거부 정책, 정상 stop/destroy를 같은 반복에서 검증한다.
- Release에서도 test assertion이 제거되지 않도록 health unit test에서 `NDEBUG`를 해제하고 macOS·Docker에서 실제 2개 테스트를 실행했다.

## 8. Test 결과

| 환경 | 구성 | `ctest -N` | 실행 수 | PASS | FAIL |
|---|---|---:|---:|---:|---:|
| macOS arm64 | 6개 feature-local Debug, `-Wall -Wextra -Werror` | 15 | 15 | 15 | 0 |
| macOS arm64 | 6개 feature-local Release tests-off | 0 | 0 | 6개 build | 0 |
| macOS arm64 | health Release tests-on | 2 | 2 | 2 | 0 |
| macOS arm64 | 6개 feature-local UBSan | 15 | 15 | 15 | 0 |
| Ubuntu 22.04 arm64 Docker | 6개 feature-local Debug | 15 | 15 | 15 | 0 |
| Ubuntu 22.04 arm64 Docker | 6개 feature-local Release tests-off | 0 | 0 | 6개 build | 0 |
| Ubuntu 22.04 arm64 Docker | health Release tests-on | 2 | 2 | 2 | 0 |
| Ubuntu 22.04 arm64 Docker | root Debug real transport | 9 | 9 | 9 | 0 |
| Ubuntu 22.04 arm64 Docker | root Release real transport | 9 | 9 | 9 | 0 |
| Ubuntu 22.04 arm64 Docker | mock MGR + real mgr_ipc | 6 | 6 | 6 | 0 |
| Ubuntu 22.04 arm64 Docker | 6개 feature-local ASan | 15 | 15 | 15 | 0 |
| Ubuntu 22.04 arm64 Docker | 6개 feature-local UBSan | 15 | 15 | 15 | 0 |

기본 6개 feature-local CTest의 실제 합은 15개다. 서로 다른 build tree의 test 수를 하나의 CTest 수로 합산하지 않았다.

## 9. Sanitizer

| 환경 | ASan | UBSan | TSan | 근거 |
|---|---|---|---|---|
| macOS arm64 | `NOT_PERFORMED` | 15/15 PASS | `NOT_PERFORMED` | ASan은 LeakSanitizer 활성 시 미지원 오류, 비활성 시에도 `config`·`mgr_ipc` 첫 테스트가 본문 출력 전 정지해 수동 중단했다. |
| Ubuntu 22.04 arm64 Docker | 15/15 PASS | 15/15 PASS | `NOT_PERFORMED` | TSan build는 성공했으나 test body 전 `ThreadSanitizer CHECK failed ... personality(old_personality \| ADDR_NO_RANDOMIZE) ... -1`로 종료했다. |

## 10. Scope

- Allowed path violations: 없음.
- Foundation changes: 없음.
- Contract, root CMake, platform/Foundation API changes: 없음.
- Review 1·2 changes: 없음.
- 다른 session production changes: 없음.
- Production/Test Fix commit의 실제 변경 파일: 11개. 이 Fix Result 2 신규 파일을 포함한 전체 고유 변경 파일: 12개.

## 11. 검증 경계

- RV1106 cross-build: `NOT_PERFORMED`.
- Board runtime: `NOT_PERFORMED`.
- Hardware QA: `NOT_PERFORMED`.
- macOS ASan 및 Ubuntu TSan은 위 환경 제약 때문에 PASS로 간주하지 않는다.

## 12. 최종 상태

- `FIX_IMPLEMENTATION_FINISHED`
- `AWAITING_CODEX_REVERIFY`

FIX_2_REPORT_ARTIFACT_SHA:
`PENDING_REPORT_ARTIFACT_COMMIT`
