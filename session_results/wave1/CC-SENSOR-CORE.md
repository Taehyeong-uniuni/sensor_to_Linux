# CC-SENSOR-CORE 세션 결과

## 1. 계보와 기준

- 세션: `CC-SENSOR-CORE-FIX-3`
- 브랜치: `feature/sensor-core`
- 저장소: `https://github.com/Taehyeong-uniuni/sensor_to_Linux.git`
- 작업 트리: `/Users/juganghyeon/Desktop/uniuni/projects/worktrees/sensor-core`
- Foundation 기준: `07809cb1f3f2b86a8e92ade661c48cb3adb97b52` (`contract-v1`)
- 원 구현: `800207949dc28a6e18a3eafe4399f8cb0eb3d811`
- Review 1 artifact: `2b1c3d5e8751c9d91431f47b8e9bfa2b4245ec3b`
- Fix 1 구현: `0f01c25f3c7ae55ca0e5650ce69015006ab1ef6e`
- Fix 1 결과 보고: `c5921c02acd93c13eb675043ff8a6a76cb04b7a8`
- Review 2 artifact: `74b4096713e465ef0855fcf0fd707847c142101c`
- Fix 2 시작 HEAD: `e135d6aa08f57341087946c412ca8d9cccc324ae`
- Fix 2 구현: `0aef477915b1481b4a93aa0e09670375612b58d9`
- Fix 2 보고 artifact: `6d24de19386feb5ca1e17b4fef257e5d47d345d4`
- Fix 2 보고 ledger: `0fd163d0055332a300dc234f5138d81b04ab0c05`
- Fix 3 시작 HEAD: `d2710a2f362caf54ebe30f358e613207c67e97a7`
- Review 3 artifact 및 최신 파일 SHA: `d2710a2f362caf54ebe30f358e613207c67e97a7`
- Fix 3 구현 SHA는 이 문서와 코드·테스트·Fix Result 2 정정을 함께 담는 commit 자체라 순환 참조로 이 파일 안에 넣지 않는다. 정확한 40자리 SHA는 `CC-SENSOR-CORE_CODEX_FIX_RESULT_3.md`의 Exact SHA 원장에 기록한다.

## 2. Finding 수정 결과

- `CDX-W1-SENSOR-CORE-001`: `FIXED`. `start()`의 최종 destroy 확인부터 cancel-source 초기화, worker 생성, `started=true` 공개까지 `lifecycle_lock`으로 묶었다. `destroy()` claim도 같은 lock을 먼저 획득하므로 destroy가 먼저 claim하면 start side effect는 0이고, start transition이 먼저면 destroy가 transition 완료 뒤 stop/join/cleanup한다.
- `CDX-W1-SENSOR-CORE-007`: `FIXED`. Fix Result 2 ledger SHA를 확정하고, Fix 2 barrier가 실제로 강제하지 못한 외부 operation 진입을 과장하지 않도록 바로잡았으며, 기본 seccomp와 Review 3 unconfined TSan 결과를 분리했다. Fix 3 barrier 설명은 실제 관찰 event만 기록한다.
- `CDX-W1-SENSOR-CORE-R3-001`: `FIXED`. client-owned worker는 fake connector의 test-only TLS start/exit/active counter로 직접 확인한다. process thread는 pthread runtime warm-up 뒤 baseline을 잡고 stop/join 뒤 같은 baseline으로 복귀하는지 검사한다. 비-TSan Linux의 직접 `1→2→1` assertion은 유지했다.

Review 3에서 해결 유지된 `002`, `003`, `004`, `005`, `006`, `R2-001`은 설계를 다시 열지 않고 전체 회귀 행렬로 재검증했다.

## 3. start/destroy lifecycle

- 잠금 순서는 nested 구간에서 항상 `lifecycle_lock → g_registry_lock`이다. API pin drain은 lifecycle lock을 보유하지 않는다.
- 정지 상태 start는 먼저 advisory destroy 확인을 수행한 뒤 lifecycle lock을 다시 획득하고 authoritative destroy 확인을 한다.
- authoritative 확인이 false인 동안 lifecycle lock을 유지한 채 cancel source 초기화, reconnect state 초기화, `pthread_create()`, worker handle 및 `started=true`를 공개한다.
- destroy claim은 lifecycle lock을 획득한 상태에서 worker caller 여부와 `destroy_requested`를 한 번에 확정한다.
- destroy가 transition 중이면 lock에서 대기하고, start가 완료된 뒤 기존 stop/join/terminal-cleanup 경로를 거쳐 storage를 해제한다.
- callback destroy가 이미 외부 join owner와 경합하면 destroy claim을 되돌리고 stop-only로 축소하는 기존 terminal handoff는 유지한다.

## 4. 결정적 barrier test

`SNS-CORE-007-mgr-ipc`의 testable 전용 target에서 다음 event를 직접 관찰한다: 최초 destroy 확인 완료, start side-effect 직전, worker 생성 직후, destroy transition 대기, destroy claim, join claim, join wait, worker enter/finish, cancel init/destroy.

- stopped-client destroy 우선: 500회. start가 최초 false 확인 뒤 barrier에서 멈추고 destroy claim이 실제 관찰된 뒤 release한다. start는 `SAVVY_ERR_INVALID_ARGUMENT`으로 끝나며 worker/cancel/connect/close event는 모두 0이다.
- start transition 우선: 500회. 절반은 cancel init 직전, 절반은 worker create 직후 start를 막는다. destroy가 lifecycle transition wait에 실제 진입한 뒤 release하고, worker·cancel init/destroy·terminal cleanup이 각각 정확히 한 번 발생함을 검사한다.
- callback-stop 뒤 start/destroy overlap: 500회. callback stop을 막은 상태에서 external start가 join을 claim하고 external destroy가 claim 및 join wait에 들어간 것을 모두 관찰한 뒤 callback을 release한다. 최종 worker start/exit, cancel init/destroy, connect/close가 각각 한 번이고 UAF/deadlock 없이 destroy로 수렴한다.
- 기존 callback terminal 500회, concurrent stop/destroy 500회, send/shutdown 250회 회귀도 유지한다.
- interleaving은 condition-variable event로 강제하며 고정 sleep에 의존하지 않는다. Linux process-resource 수렴 확인만 `/proc/self/task`의 실제 값이 deadline 안에 baseline으로 돌아오는지 polling한다.

최종 후보에서 portable 및 real transport의 `SNS-CORE-006`, `SNS-CORE-007-mgr-ipc`를 각각 `--repeat until-fail:10`으로 실행해 네 묶음 모두 10/10 PASS를 확인했다.

## 5. TSan thread baseline과 production 격리

- `SNS-CORE-006`은 production `sensor_core_mgr_ipc` archive에 연결된 `test_sensor_core_mgr_ipc 006`을 실행한다.
- fake connector TLS destructor가 client worker의 start 1, exit 1, active 0을 독립적으로 증명한다.
- TSan은 runtime warm-up 뒤 안정된 process baseline을 측정하고 final exact 복귀를 검사한다. runtime helper 수를 상수로 가정하지 않는다.
- 비-TSan Linux는 process `threads_baseline == 1`, running `== 2`, final `== 1`을 그대로 직접 assert한다.
- lifecycle hook은 별도 `sensor_core_mgr_ipc_testable` archive와 `test_sensor_core_mgr_ipc_lifecycle`에만 컴파일된다.
- portable, real, Release tests-off production archive를 `nm`으로 검사한 결과 test hook/probe symbol은 각각 0개다.
- tests-off 구성에는 testable target, lifecycle test executable, instrumentation artifact가 없다.

## 6. Test 결과

| 환경 / 구성 | `ctest -N` | 실행 | PASS | FAIL |
|---|---:|---:|---:|---:|
| macOS arm64, 6개 feature-local Debug, `-Wall -Wextra -Werror` | 15 | 15 | 15 | 0 |
| macOS arm64, 6개 feature-local Release tests-off | 0 | 0 | 6개 build | 0 |
| macOS arm64, health Release tests-on | 2 | 2 | 2 | 0 |
| macOS arm64, 6개 feature-local UBSan | 15 | 15 | 15 | 0 |
| Ubuntu 22.04 arm64 Docker, 6개 feature-local Debug | 15 | 15 | 15 | 0 |
| Ubuntu 22.04 arm64 Docker, 6개 feature-local Release tests-off | 0 | 0 | 6개 build | 0 |
| Ubuntu 22.04 arm64 Docker, health Release tests-on | 2 | 2 | 2 | 0 |
| Ubuntu Docker, root Debug real transport | 9 | 9 | 9 | 0 |
| Ubuntu Docker, root Release real transport | 9 | 9 | 9 | 0 |
| Ubuntu Docker, mock MGR + real mgr_ipc | 6 | 6 | 6 | 0 |
| Ubuntu Docker, 6개 feature-local ASan | 15 | 15 | 15 | 0 |
| Ubuntu Docker, 6개 feature-local UBSan | 15 | 15 | 15 | 0 |
| Ubuntu Docker, 6개 feature-local TSan unconfined | 15 | 15 | 15 | 0 |

feature-local 기본 합계는 `config 3 + mgr_ipc 5 + state_report 2 + update_guard 1 + health 2 + mode_state 2 = 15`다. 최종 test-only 보완 뒤 영향받은 mgr_ipc 5개를 각 구성에서 다시 build/run했고, 변경 없는 나머지 feature 결과와 합산했다.

## 7. Sanitizer

- Ubuntu arm64 Docker ASan: 15/15 PASS, sanitizer report 0.
- macOS arm64 UBSan: 15/15 PASS. Ubuntu arm64 Docker UBSan: 15/15 PASS. 최종 Ubuntu UBSan `SNS-CORE-007-mgr-ipc` 추가 3회도 모두 PASS.
- Ubuntu arm64 Docker TSan 기본 seccomp: 6개 build 및 15개 test 등록은 성공했으나 15개 모두 test body 전 `personality(... ADDR_NO_RANDOMIZE) ... -1` runtime CHECK으로 종료했다. `NOT_PERFORMED`이며 PASS로 세지 않는다.
- Ubuntu arm64 Docker TSan `--security-opt seccomp=unconfined`: 15/15 PASS. `TSAN_OPTIONS=halt_on_error=1`에서 data-race report 0. 최종 `SNS-CORE-007-mgr-ipc` 추가 3회도 모두 PASS.
- macOS arm64 ASan: build는 성공했으나 최종 `SNS-CORE-007-mgr-ipc`가 ASan report 없이 60.03초 timeout했다. `NOT_PERFORMED (timeout)`이며 PASS가 아니다.

## 8. fd·thread 결과

- 비-sanitized Ubuntu production-linked `SNS-CORE-006`: `fd=5/9/6 threads=1/2/1 cycles=500`, PASS.
- unconfined TSan production-linked `SNS-CORE-006`: `fd=5/9/6 threads=2/3/2 cycles=500`, PASS.
- fd final은 `count_open_fds()` 자체의 directory descriptor 허용치인 baseline + 1 이내이며 500회에 비례한 증가가 없다.
- worker TLS counter는 start 1, exit 1, active 0으로 client-owned worker 종료를 직접 확인한다.
- 세 신규 500회 batch는 각 batch 전후 fd exact baseline과 thread exact baseline 복귀를 검사한다. task entry 제거가 비동기로 반영되는 짧은 간격은 최대 2초 동안 실제 count를 관찰하며, baseline으로 돌아오지 않으면 실패한다.
- 최종 보완 전 real/UBSan 반복에서 이 `/proc/self/task` 반영 간격을 즉시 샘플링해 간헐 assertion이 발생한 중간 후보가 있었다. stable baseline/final polling과 detached callback worker exit 관측을 추가한 최종 후보는 portable/real 006·007 각 10/10, UBSan 및 TSan 007 추가 각 3/3을 통과했다.

## 9. 결과 문서 정정

- Fix Result 2의 `PENDING_LEDGER_COMMIT`을 `0fd163d0055332a300dc234f5138d81b04ab0c05`로 교체했다.
- Fix 2 barrier는 callback action 진입/release만 강제했고 외부 operation의 destroy claim 또는 restart wait 진입은 강제하지 못했음을 명시했다.
- Fix 2 기본 seccomp TSan `NOT_PERFORMED`와 Review 3 unconfined 14/15 PASS·`SNS-CORE-006` assertion 1 FAIL을 역사적 결과로 분리했다.
- Fix 3 결과는 최종 후보의 unconfined TSan 15/15 PASS와 data-race report 0을 별도로 기록한다.

## 10. Scope와 검증 경계

- 허용 경로 위반: 없음.
- Foundation, contract, root CMake, 다른 feature production 경로 변경: 없음.
- Review 1·2·3 파일 변경: 없음.
- production/test commit 변경은 `src/features/mgr_ipc/**`, `tests/unit/sensor_core/mgr_ipc/**`, 이 세션 보고서, Fix Result 2 정정에 한정한다.
- RV1106 cross-build: `NOT_PERFORMED`.
- Board runtime: `NOT_PERFORMED`.
- Hardware QA: `NOT_PERFORMED`.

## 11. 상태

- `FIX_IMPLEMENTATION_FINISHED`
- `AWAITING_CODEX_REVERIFY`
