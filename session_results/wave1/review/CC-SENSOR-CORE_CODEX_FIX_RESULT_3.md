# CC-SENSOR-CORE Codex Fix Result 3

## 1. 검증 기준

- Session: `CC-SENSOR-CORE-FIX-3`
- Branch: `feature/sensor-core`
- Repository: `https://github.com/Taehyeong-uniuni/sensor_to_Linux.git`
- Worktree: `/Users/juganghyeon/Desktop/uniuni/projects/worktrees/sensor-core`
- Governing Review 3: `session_results/wave1/review/CC-SENSOR-CORE_CODEX_REVIEW_3.md`
- Production/Test commit 변경 파일: 9개
- Fix Result 3 신규 파일까지 포함한 Fix 3 고유 변경 파일: 10개

## 2. Exact SHA 원장

| 단계 | Exact SHA | 상태 |
|---|---|---|
| BASE | `07809cb1f3f2b86a8e92ade661c48cb3adb97b52` | 확인 |
| 원 구현 | `800207949dc28a6e18a3eafe4399f8cb0eb3d811` | 확인 |
| Review 1 artifact | `2b1c3d5e8751c9d91431f47b8e9bfa2b4245ec3b` | 확인 |
| Fix 1 구현 | `0f01c25f3c7ae55ca0e5650ce69015006ab1ef6e` | 확인 |
| Fix 1 보고 | `c5921c02acd93c13eb675043ff8a6a76cb04b7a8` | 확인 |
| Review 2 artifact | `74b4096713e465ef0855fcf0fd707847c142101c` | 확인 |
| Fix 2 시작 HEAD | `e135d6aa08f57341087946c412ca8d9cccc324ae` | 확인 |
| Fix 2 구현 | `0aef477915b1481b4a93aa0e09670375612b58d9` | 확인 |
| Fix 2 보고 artifact | `6d24de19386feb5ca1e17b4fef257e5d47d345d4` | 확인 |
| Fix 2 보고 ledger | `0fd163d0055332a300dc234f5138d81b04ab0c05` | 확인 |
| Fix 3 시작 HEAD | `d2710a2f362caf54ebe30f358e613207c67e97a7` | 확인 |
| Review 3 artifact | `d2710a2f362caf54ebe30f358e613207c67e97a7` | 확인 |
| Review 3 최신 파일 | `d2710a2f362caf54ebe30f358e613207c67e97a7` | 확인 |
| Fix 3 구현 | `78e3c40d8dbd82a14ae7f3583a7f1b98cdde283f` | 확인 |
| Fix 3 보고 artifact | `02549647fe89c84e73ea8c94b64c86b57594ab70` | 확인 |
| Fix 3 보고 ledger | `PENDING_LEDGER_COMMIT` | 다음 Review에서 Git으로 확정 |

## 3. Finding 상태

| Finding ID | Review 3 상태 | Fix 3 상태 | 직접 검증 |
|---|---|---|---|
| `CDX-W1-SENSOR-CORE-001` | `PARTIALLY_RESOLVED` | `FIXED` | 3종 결정적 lifecycle race 각 500회, portable/real 006·007 각 10회 반복 |
| `CDX-W1-SENSOR-CORE-007` | `PARTIALLY_RESOLVED` | `FIXED` | Fix Result 2 SHA·barrier·TSan 역사 정정 및 Git 대조 |
| `CDX-W1-SENSOR-CORE-R3-001` | `NEW LOW` | `FIXED` | production-linked 006, TLS worker signal, 비-TSan 1→2→1, TSan 2→3→2 |

Review 3에서 `RESOLVED`였던 `002`, `003`, `004`, `005`, `006`, `R2-001`은 전체 Debug·Release·real transport·sanitizer 회귀로 해결 상태를 유지했다.

## 4. start/destroy lifecycle 수정

- `start()`는 advisory destroy 확인 뒤 `lifecycle_lock`을 재획득하고 `destroy_requested`를 authoritative하게 다시 확인한다.
- 이 확인부터 cancel-source init, reconnect state init, `pthread_create()`, `started=true` 공개까지 lifecycle lock을 유지한다.
- `destroy()` claim도 lifecycle lock을 먼저 잡고 같은 critical section에서 self-worker 여부와 destroy claim을 확정한다.
- nested lock order는 `lifecycle_lock → g_registry_lock`이며 API pin drain은 lifecycle lock을 잡지 않는다.
- destroy 우선이면 start는 worker/cancel side effect 없이 abort한다. start 우선이면 destroy는 transition 완료를 기다린 뒤 stop/join/cancel cleanup 후 storage를 해제한다.

## 5. barrier test

production archive와 분리된 `sensor_core_mgr_ipc_testable` target에서 lifecycle event를 관찰한다.

- stopped start/destroy destroy-wins: 500회. 최초 destroy false 확인 뒤 start를 막고 destroy claim을 확인한 후 release한다. worker/cancel/connect/close 0을 assert한다.
- stopped start/destroy start-wins: 500회. cancel init 직전 또는 worker create 직후 start를 막고 destroy가 transition wait에 들어간 것을 확인한다. worker 및 cancel init/destroy가 각각 한 번이고 terminal cleanup 뒤 destroy됨을 assert한다.
- callback-stop start/destroy overlap: 500회. start join claim, destroy claim, destroy join-wait를 차례로 관찰한 뒤 callback을 release한다. 하나의 worker와 cancel source가 정확히 한 번 종료됨을 assert한다.
- 기존 callback terminal barrier 500회, stop/destroy stress 500회, send/shutdown race 250회도 유지한다.
- portable과 real transport에서 006·007을 각각 10회 반복해 총 네 묶음 10/10 PASS를 확인했다.

## 6. TSan thread baseline 수정

- client-owned worker는 fake connector의 test-only pthread TLS key로 식별한다. worker start 1, exit 1, active 0을 직접 확인한다.
- `SNS-CORE-006`은 testable archive가 아닌 production `sensor_core_mgr_ipc` archive에 연결된다.
- TSan은 pthread runtime warm-up 후 안정된 process thread baseline을 측정하고 stop/join 뒤 같은 값으로 복귀하는지 확인한다.
- 비-TSan Linux의 직접 `1→2→1` process thread assertion은 유지한다.
- `/proc/self/task`의 task-entry 제거 반영 간격은 최대 2초 동안 실제 count를 관찰하고, baseline으로 수렴하지 않으면 exact assertion이 실패한다.
- test hook은 production archive의 symbol 및 branch에 포함되지 않는다. tests-off build에는 testable target과 instrumentation artifact가 없다.

## 7. 결과 문서 정정

- Fix Result 2의 ledger placeholder를 `0fd163d0055332a300dc234f5138d81b04ab0c05`로 교체했다.
- Fix 2 barrier가 callback action의 진입/release만 강제했고 외부 operation claim/wait 진입은 강제하지 못했음을 명시했다.
- Fix 2 기본 seccomp TSan은 test body 전 runtime CHECK으로 `NOT_PERFORMED`, Review 3 unconfined 재검증은 14/15 PASS와 `SNS-CORE-006` assertion 1 FAIL이었다고 분리했다.
- Fix 3의 최종 unconfined TSan 15/15 PASS 및 data-race report 0은 별도 결과로 기록했다.

## 8. Test 결과

| 환경 | 구성 | `ctest -N` | 실행 | PASS | FAIL |
|---|---|---:|---:|---:|---:|
| macOS arm64 | 6개 feature-local Debug, `-Wall -Wextra -Werror` | 15 | 15 | 15 | 0 |
| macOS arm64 | 6개 feature-local Release tests-off | 0 | 0 | 6개 build | 0 |
| macOS arm64 | health Release tests-on | 2 | 2 | 2 | 0 |
| macOS arm64 | 6개 feature-local UBSan | 15 | 15 | 15 | 0 |
| Ubuntu 22.04 arm64 Docker | 6개 feature-local Debug | 15 | 15 | 15 | 0 |
| Ubuntu 22.04 arm64 Docker | 6개 feature-local Release tests-off | 0 | 0 | 6개 build | 0 |
| Ubuntu 22.04 arm64 Docker | health Release tests-on | 2 | 2 | 2 | 0 |
| Ubuntu arm64 Docker | root Debug real transport | 9 | 9 | 9 | 0 |
| Ubuntu arm64 Docker | root Release real transport | 9 | 9 | 9 | 0 |
| Ubuntu arm64 Docker | mock MGR + real mgr_ipc | 6 | 6 | 6 | 0 |
| Ubuntu arm64 Docker | 6개 feature-local ASan | 15 | 15 | 15 | 0 |
| Ubuntu arm64 Docker | 6개 feature-local UBSan | 15 | 15 | 15 | 0 |
| Ubuntu arm64 Docker | 6개 feature-local TSan unconfined | 15 | 15 | 15 | 0 |

feature-local 합계는 `3 + 5 + 2 + 1 + 2 + 2 = 15`다. 최종 test-only 보완 뒤 영향받은 mgr_ipc 5개는 각 구성에서 다시 build/run했다.

## 9. Sanitizer

| 환경 | ASan | UBSan | TSan |
|---|---|---|---|
| macOS arm64 | `NOT_PERFORMED (timeout)`: 최종 007이 report 없이 60.03초 timeout | 15/15 PASS | `NOT_PERFORMED` |
| Ubuntu 22.04 arm64 Docker | 15/15 PASS | 15/15 PASS | 기본 seccomp `NOT_PERFORMED`; unconfined 15/15 PASS |

- 기본 seccomp TSan은 15개 모두 test body 전 `personality(... ADDR_NO_RANDOMIZE) ... -1` runtime CHECK으로 종료했다.
- unconfined TSan은 `TSAN_OPTIONS=halt_on_error=1`에서 data-race report 0이다.
- 최종 Ubuntu UBSan·unconfined TSan 007 추가 3회도 각각 3/3 PASS다.
- macOS ASan timeout은 PASS로 세지 않았다.

## 10. fd·thread

- non-sanitized Ubuntu production 006: `fd=5/9/6 threads=1/2/1 cycles=500`, PASS.
- unconfined TSan production 006: `fd=5/9/6 threads=2/3/2 cycles=500`, PASS.
- fd final은 측정용 directory descriptor 허용치인 baseline + 1 이내이며 500회에 비례한 증가가 없다.
- 세 신규 500회 batch는 각각 fd exact baseline과 thread exact baseline 복귀를 검사한다.
- detached callback worker는 TLS exit signal과 active 0을 확인한 뒤 fake context를 파괴한다.
- 최종 후보 이전의 real/UBSan 간헐 baseline assertion은 task-entry 제거 반영을 즉시 샘플링한 중간 후보 결과였다. stable baseline/final 관찰을 추가한 뒤 portable/real 반복 및 sanitizer 재검증이 모두 통과했다.

## 11. Regression

- Config·Device full replacement/raw JSON 및 no replay: PASS.
- transport I/O lifetime과 close ownership: PASS.
- CONNECT handshake public-state gate와 reconnect: PASS.
- 65,536/65,537 application envelope 경계: PASS.
- lifecycle callback lock 분리 및 `callback_depth` 초기화: Debug·Release PASS.
- real AF_UNIX SOCK_SEQPACKET mock MGR integration: 6/6 PASS.
- production archive test symbol: portable, real, Release tests-off 모두 0.

## 12. Scope

- Allowed path violations: 없음.
- Foundation, contract, root CMake, 다른 feature production 변경: 없음.
- Review 1·2·3 변경: 없음.
- Production/Test commit 변경 파일: 9개.
- 이 Fix Result 3 신규 파일을 포함한 Fix 3 고유 변경 파일: 10개.

## 13. 검증 한계

- RV1106 cross-build: `NOT_PERFORMED`.
- Board runtime: `NOT_PERFORMED`.
- Hardware QA: `NOT_PERFORMED`.
- macOS ASan 및 기본-seccomp TSan은 환경별 위 결과 때문에 PASS로 간주하지 않는다.

## 14. 최종 상태

- `FIX_IMPLEMENTATION_FINISHED`
- `AWAITING_CODEX_REVERIFY`

FIX_3_REPORT_ARTIFACT_SHA:
`02549647fe89c84e73ea8c94b64c86b57594ab70`

FIX_3_REPORT_LEDGER_SHA:
`PENDING_LEDGER_COMMIT`
