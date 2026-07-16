# CC-SENSOR-CORE Codex Review 4 재검증 결과

- 세션 ID: `CC-SENSOR-CORE`
- 모드: `CODEX_REVIEW_4`
- 독립 재검증 대상: Fix 3 구현 커밋 `78e3c40d8dbd82a14ae7f3583a7f1b98cdde283f`
- 검증 시작 HEAD: `5a3343d07c51f03a513e0f58bc65056887414d06`
- 최종 판정: `PASS`

## 1. Worktree Gate

엄격 gate를 다른 검증보다 먼저 실행했다.

| 항목 | 관찰값 | 판정 |
|---|---|---|
| worktree | `/Users/juganghyeon/Desktop/uniuni/projects/worktrees/sensor-core` | 기대값 일치 |
| repository top-level | `/Users/juganghyeon/Desktop/uniuni/projects/worktrees/sensor-core` | 기대값 일치 |
| origin | `https://github.com/Taehyeong-uniuni/sensor_to_Linux.git` | 기대값 일치 |
| branch | `feature/sensor-core` | 기대값 일치 |
| 시작 HEAD | `5a3343d07c51f03a513e0f58bc65056887414d06` | 기대값 일치 |
| 시작 상태 | clean | 통과 |

gate 실패 항목은 없다.

## 2. Exact SHA 원장

각 값은 `git rev-parse '<sha>^{commit}'`, 해당 파일의 최초/최신 commit 조회, Fix 3 결과 문서의 명시값을 서로 대조했다.

| 구분 | 40자리 SHA |
|---|---|
| Base | `07809cb1f3f2b86a8e92ade661c48cb3adb97b52` |
| Original | `800207949dc28a6e18a3eafe4399f8cb0eb3d811` |
| Review 1 artifact | `2b1c3d5e8751c9d91431f47b8e9bfa2b4245ec3b` |
| Fix 1 implementation | `0f01c25f3c7ae55ca0e5650ce69015006ab1ef6e` |
| Fix 1 report | `c5921c02acd93c13eb675043ff8a6a76cb04b7a8` |
| Review 2 artifact | `74b4096713e465ef0855fcf0fd707847c142101c` |
| Fix 2 starting HEAD | `e135d6aa08f57341087946c412ca8d9cccc324ae` |
| Fix 2 implementation | `0aef477915b1481b4a93aa0e09670375612b58d9` |
| Fix 2 report artifact | `6d24de19386feb5ca1e17b4fef257e5d47d345d4` |
| Fix 2 report ledger | `0fd163d0055332a300dc234f5138d81b04ab0c05` |
| Review 3 artifact | `d2710a2f362caf54ebe30f358e613207c67e97a7` |
| Review 3 최신 파일 | `d2710a2f362caf54ebe30f358e613207c67e97a7` |
| Fix 3 implementation | `78e3c40d8dbd82a14ae7f3583a7f1b98cdde283f` |
| Fix 3 report artifact | `02549647fe89c84e73ea8c94b64c86b57594ab70` |
| Fix 3 report ledger | `5a3343d07c51f03a513e0f58bc65056887414d06` |
| Review 4 starting HEAD | `5a3343d07c51f03a513e0f58bc65056887414d06` |

Fix 3 결과가 명시한 구현 SHA는 실제 commit이고, `git log --all --grep='fix(sensor-core): resolve Codex review 3 findings'`의 후보도 이 한 건뿐이다.

## 3. SHA lineage

아래 모든 인접 쌍에 `git merge-base --is-ancestor <left> <right>`를 실행했고 모두 exit 0이었다.

```text
07809cb1 -> 80020794 -> 2b1c3d5e -> 0f01c25f -> c5921c02
-> 74b40967 -> e135d6aa -> 0aef4779 -> 6d24de19 -> 0fd163d0
-> d2710a2f -> 78e3c40d -> 02549647 -> 5a3343d0
```

Fix 3 구현, 결과 artifact, 결과 ledger가 선형 계보에 있고 시작 HEAD에서 모두 도달 가능하다.

## 4. Exact Fix 3 target

검증용 임시 clone을 `/tmp/cc-sensor-core-review4.ablY1T/sensor-core-review4`에 만들고 `78e3c40d8dbd82a14ae7f3583a7f1b98cdde283f`를 detached checkout했다. 검증 시작과 종료 시 clone은 clean이었다. 모든 source/build/test 판단은 이 exact tree를 기준으로 했으며, Fix 3 이후 두 문서 commit은 원장 감사에만 사용했다.

## 5. Scope 검사

Review 3에서 Fix 3 구현까지의 변경은 9개 파일, 914 insertions, 93 deletions이다.

```text
M session_results/wave1/CC-SENSOR-CORE.md
M session_results/wave1/review/CC-SENSOR-CORE_CODEX_FIX_RESULT_2.md
M src/features/mgr_ipc/CMakeLists.txt
M src/features/mgr_ipc/mgr_ipc_client.c
M src/features/mgr_ipc/mgr_ipc_client.h
M tests/unit/sensor_core/mgr_ipc/fake_transport.c
M tests/unit/sensor_core/mgr_ipc/fake_transport.h
A tests/unit/sensor_core/mgr_ipc/mgr_ipc_test_hooks.h
M tests/unit/sensor_core/mgr_ipc/test_mgr_ipc_client.c
```

Fix 3 결과 artifact까지는 결과 문서 1개를 더해 10개 경로다. 결과 ledger commit은 그 결과 문서만 수정했다.

- 허용 경로 위반: 0
- Foundation 변경: 0
- contract 변경: 0
- root `CMakeLists.txt` 변경: 0
- 다른 feature source 변경: 0
- Review 1·2·3 artifact 변경: 0
- symlink 변경: 0
- binary diff: 0
- `git diff --check` 오류: 0

## 6. Review 3 Finding 재판정

| Finding | Review 3 상태 | Review 4 상태 | 독립 근거 |
|---|---|---|---|
| `CDX-W1-SENSOR-CORE-001` | `PARTIALLY_RESOLVED` | `RESOLVED` | authoritative destroy 확인부터 cancel init, worker 생성, `started=true` 공개까지 같은 lifecycle lock으로 직렬화; 결정적 500회 세 묶음 및 기존 500회 stress 통과 |
| `CDX-W1-SENSOR-CORE-007` | `PARTIALLY_RESOLVED` | `RESOLVED` | 과거 SHA placeholder 정정, 실제 SHA·경로 수·test 수·barrier·sanitizer 역사를 Git 및 실행 결과로 대조 |
| `CDX-W1-SENSOR-CORE-R3-001` | `NEW LOW` | `RESOLVED` | TLS 기반 client worker 신호, non-sanitized `1→2→1`, TSan `2→3→2`, tests-off production 계측 노출 0 확인 |

Review 3에서 해결된 `002`, `003`, `004`, `005`, `006`, `R2-001`도 전체 회귀 행렬에서 해결 상태를 유지했다.

## 7. start/destroy lifecycle

`mgr_ipc_client.c:132-160`의 destroy claim은 `lifecycle_lock → g_registry_lock` 순서로 `destroy_requested`를 설정한다. `start()`는 `mgr_ipc_client.c:393-450`에서 lifecycle lock을 다시 잡은 상태로 authoritative destroy 확인을 하고, 그 lock을 cancel source 초기화, reconnect tracker 초기화, `pthread_create()`, `started=true` 공개까지 유지한다.

따라서 가능한 순서는 둘뿐이다.

1. destroy claim이 먼저면 start의 authoritative 확인이 true를 관찰하고 side effect 없이 종료한다.
2. start transition이 먼저면 destroy가 lifecycle lock에서 기다린 뒤 완성된 worker를 관찰하고 stop/join/cancel cleanup을 수행한 다음 storage를 해제한다.

`pthread_create()` 실패 경로도 같은 lock 아래 cancel source를 파괴하고 상태를 되돌린다. `client_unclaim_destroy()` 역시 같은 lock 순서를 사용한다. registry pin drain은 lifecycle lock을 잡지 않으므로 반대 순서의 중첩 lock은 발견되지 않았다. 기존 callback terminal ownership 및 join handoff도 변경되지 않았다.

이 구조에서는 Review 3의 `destroy_requested=false` 확인과 worker 생성 사이에 destroy가 storage를 해제하던 원자성 공백을 만들 수 없다.

## 8. barrier test

`mgr_ipc_test_hooks.h:9-21`은 최초 destroy 확인 후, cancel init 직전, worker 생성 후, worker enter/finish, destroy transition wait/claim, join claim/wait, cancel init/destroy event를 정의한다. probe는 condition variable과 2초 deadline을 사용하며 고정 sleep으로 interleaving을 추정하지 않는다.

`SNS-CORE-007-mgr-ipc`가 직접 assertion한 묶음은 다음과 같다.

| 묶음 | source 범위 | 강제한 관찰 순서 | 회수/종료 assertion |
|---|---|---|---|
| destroy 우선 500회 | `test_mgr_ipc_client.c:960-1027` | start 최초 확인 후 block → destroy claim 관찰 → start release | start invalid, worker/cancel/connect/close 각 0, batch fd/thread exact 복귀 |
| start 우선 500회 | `test_mgr_ipc_client.c:1030-1097` | cancel init 직전 또는 worker 생성 후 block → destroy transition wait 관찰 → release | start 성공, worker enter/finish와 cancel init/destroy 각 1, batch fd/thread exact 복귀 |
| callback-stop 이후 start/destroy 500회 | `test_mgr_ipc_client.c:1100-1186` | callback stop block → start join claim → destroy claim 및 join wait → callback release | start invalid, worker start/exit 각 1·active 0, connect/close 각 1, cancel init/destroy 각 1, batch 복귀 |
| 기존 stopped-client stop/destroy stress 500회 | `test_mgr_ipc_client.c:1234-1269` | stop/stop, stop/destroy, destroy/destroy 교대 | timeout·abort 없이 합법 terminal 상태 수렴 |

필수 반복 수 관점에서 stopped-client start/destroy는 destroy 우선 500회와 start 우선 500회로 양방향 총 1,000회다. 기존 stopped-client stop/destroy 500회는 별도 추가 회귀다.

단일 Debug 실행뿐 아니라 portable과 real build에서 006·007을 각각 `ctest --repeat until-fail:10`으로 실행해 각 build tree 20회씩 모두 통과했다. Docker ASan·UBSan·unconfined TSan에서도 007이 통과했고 unconfined TSan 007 추가 3회도 통과했다. 완료된 이 실행들에서 timeout, double init/destroy, cancel source 잔류, worker 잔류는 관찰되지 않았다. 별도의 macOS ASan timeout은 12·14절에서 실행 불가로 분리했다.

## 9. TSan worker 계측

`fake_transport.c:84-91,315-378,406-407`은 test-only pthread TLS destructor와 start/exit/active counter로 client-owned worker를 runtime helper와 분리한다. `test_mgr_ipc_client.c:708-755`는 dummy pthread를 생성·join해 sanitizer runtime을 warm-up한 뒤 20개 연속 1ms 표본이 같은 안정 baseline일 때만 측정을 시작한다.

- production-linked `SNS-CORE-006`은 testable library가 아니라 `sensor_core_mgr_ipc`에 링크된다.
- lifecycle probe가 필요한 `SNS-CORE-007-mgr-ipc`만 `sensor_core_mgr_ipc_testable`에 링크된다.
- tests-off macOS 및 Docker production archive의 `nm` 결과에서 test hook, fake transport, mock MGR 관련 전역 심볼은 0이었다.
- tests-off build tree에서 testable library, test executable, fake/mock 산출물은 0이었다.
- unconfined TSan mgr_ipc 5/5와 전체 feature 15/15가 통과했고 `WARNING: ThreadSanitizer`는 0건이었다.

그러므로 TSan helper thread를 worker로 오인하지 않으면서 production worker의 생성·종료를 직접 확인했고, 계측은 production artifact에 노출되지 않았다.

## 10. fd·thread lifecycle

Ubuntu arm64 Docker verbose 관찰값은 다음과 같다.

```text
non-sanitized SNS-CORE-006: fd=5/9/6 threads=1/2/1 cycles=500
unconfined TSan SNS-CORE-006: fd=5/9/6 threads=2/3/2 cycles=500
```

non-sanitized에서는 baseline 1, client worker 실행 중 2, stop/join 후 1을 직접 assertion했다. TSan에서는 warm-up baseline 2, worker 실행 중 3, 종료 후 2로 복귀했고 TLS counter도 start 1, exit 1, active 0을 확인했다. final fd 6은 `/proc/self/fd` 측정용 directory descriptor 허용치인 baseline + 1 이내이며 500 cycle에 비례한 증가가 없다. 세 신규 500회 batch는 fd와 thread 모두 exact baseline 복귀를 별도로 assertion한다.

## 11. Regression

6개 feature-local Debug/UBSan, Docker ASan/TSan 및 real transport 회귀를 실행했다. 등록된 ID는 다음과 같다.

```text
config: SNS-CORE-001-config, SNS-CORE-002, SNS-CORE-005-config
mgr_ipc: SNS-CORE-003a, SNS-CORE-003b-mgr-ipc, CT-IPC-002,
         SNS-CORE-006, SNS-CORE-007-mgr-ipc
state_report: SNS-CORE-001-state-report, SNS-CORE-003b
update_guard: SNS-CORE-004
health: SNS-CORE-007-health, SENSOR-CORE-LIFECYCLE-IDEMPOTENT
mode_state: SNS-CORE-002a, SNS-CORE-003-mode-state
```

| 기존 Finding | 상태 | 근거 |
|---|---|---|
| `002` Config·Device full replacement/raw JSON | `RESOLVED` 유지 | config/state_report/mode_state 회귀 통과 |
| `003` transport lifetime | `RESOLVED` 유지 | mgr_ipc send/shutdown 250회, stop/destroy 500회 및 sanitizer 통과 |
| `004` CONNECT handshake | `RESOLVED` 유지 | `CT-IPC-002`, real transport 6/6 통과 |
| `005` no-replay·boundary·recovery | `RESOLVED` 유지 | config 및 real transport 경계·recovery 회귀 통과 |
| `006` lifecycle callback lock | `RESOLVED` 유지 | callback lifecycle 500회와 재진입 회귀 통과 |
| `R2-001` callback depth 초기화 | `RESOLVED` 유지 | health Debug·Release 및 sanitizer 회귀 통과 |

## 12. macOS 결과

환경은 macOS 26.5.2 arm64, AppleClang 17.0.0, CMake 4.4.0이다. exact Fix 3 clone 밖의 `/tmp/cc-sensor-core-review4-macos.Tn9l0a`에 build tree를 만들었다.

공통 Debug 명령은 다음과 같다. `feature`와 `build`를 표의 각 tree로 치환했다.

```bash
cmake -S "$REVIEW_REPO/src/features/$feature" -B "$build" \
  -DCMAKE_BUILD_TYPE=Debug -DSAVVY_BUILD_TESTS=ON \
  -DCMAKE_C_FLAGS='-Wall -Wextra -Werror'
cmake --build "$build" --parallel 4
ctest --test-dir "$build" -N
ctest --test-dir "$build" --output-on-failure --timeout 180
```

Release tests-off는 `-DCMAKE_BUILD_TYPE=Release -DSAVVY_BUILD_TESTS=OFF`, UBSan은 `-fsanitize=undefined -fno-sanitize-recover=all -fno-omit-frame-pointer`, ASan은 `-fsanitize=address -fno-omit-frame-pointer`를 사용했다.

| build tree | 구성 | 등록 | 실행 | PASS | FAIL |
|---|---|---:|---:|---:|---:|
| `debug-config` | Debug tests-on | 3 | 3 | 3 | 0 |
| `debug-mgr_ipc` | Debug tests-on | 5 | 5 | 5 | 0 |
| `debug-state_report` | Debug tests-on | 2 | 2 | 2 | 0 |
| `debug-update_guard` | Debug tests-on | 1 | 1 | 1 | 0 |
| `debug-health` | Debug tests-on | 2 | 2 | 2 | 0 |
| `debug-mode_state` | Debug tests-on | 2 | 2 | 2 | 0 |
| `release-off-config` | Release tests-off | 0 | 0 | build | 0 |
| `release-off-mgr_ipc` | Release tests-off | 0 | 0 | build | 0 |
| `release-off-state_report` | Release tests-off | 0 | 0 | build | 0 |
| `release-off-update_guard` | Release tests-off | 0 | 0 | build | 0 |
| `release-off-health` | Release tests-off | 0 | 0 | build | 0 |
| `release-off-mode_state` | Release tests-off | 0 | 0 | build | 0 |
| `release-health-on` | Release tests-on | 2 | 2 | 2 | 0 |
| `ubsan-config` | UBSan | 3 | 3 | 3 | 0 |
| `ubsan-mgr_ipc` | UBSan | 5 | 5 | 5 | 0 |
| `ubsan-state_report` | UBSan | 2 | 2 | 2 | 0 |
| `ubsan-update_guard` | UBSan | 1 | 1 | 1 | 0 |
| `ubsan-health` | UBSan | 2 | 2 | 2 | 0 |
| `ubsan-mode_state` | UBSan | 2 | 2 | 2 | 0 |
| `asan-mgr_ipc` | ASan, 선택 007 | 5 | 1 | 0 | timeout 1 |

macOS ASan `SNS-CORE-007-mgr-ipc`는 sanitizer report 없이 60.02초 timeout되어 `NOT_PERFORMED`로 판정했다. Darwin real `SOCK_SEQPACKET`은 지원 대상이 아니어서 `NOT_RUN`이다.

## 13. Docker arm64 결과

환경은 Ubuntu 22.04.5 aarch64, GCC 11.4.0, CMake 3.22.1, Docker server arm64이다. image는 `savvy-foundation-test:ubuntu22.04-arm64-v1`, digest는 `sha256:73c8a9709607d1910231efb4648510e4d72052072629901fa28fd5c9f39753e7`이다. exact clone을 `/src`에 read-only mount했다.

공통 실행 형식과 feature-local 명령은 다음과 같다.

```bash
docker run --rm --platform linux/arm64 \
  --mount type=bind,src="$REVIEW_REPO",dst=/src,readonly \
  savvy-foundation-test:ubuntu22.04-arm64-v1 sh -lc '
    cmake -S /src/src/features/$feature -B "$build" \
      -DCMAKE_BUILD_TYPE=Debug -DSAVVY_BUILD_TESTS=ON \
      -DCMAKE_C_FLAGS="-Wall -Wextra -Werror"
    cmake --build "$build" --parallel 4
    ctest --test-dir "$build" -N
    ctest --test-dir "$build" --output-on-failure --timeout 240
  '
```

root real은 `cmake -S /src ... -DSAVVY_IPC_REAL_TRANSPORT=ON`, mgr real은 mock MGR을 별도 build한 뒤 `-DSENSOR_MGR_IPC_REAL_TRANSPORT=ON -DMOCK_MGR_BINARY=/tmp/review4/mock-mgr/mock_mgr`로 구성했다. sanitizer tree는 아래 14절의 flag를 같은 feature-local 명령에 추가했다. TSan unconfined만 Docker 명령에 `--security-opt seccomp=unconfined`을 추가했다.

| build tree | 구성 | 등록 | 실행 | PASS | FAIL |
|---|---|---:|---:|---:|---:|
| `debug-config` | Debug | 3 | 3 | 3 | 0 |
| `debug-mgr_ipc` | Debug | 5 | 5 | 5 | 0 |
| `debug-state_report` | Debug | 2 | 2 | 2 | 0 |
| `debug-update_guard` | Debug | 1 | 1 | 1 | 0 |
| `debug-health` | Debug | 2 | 2 | 2 | 0 |
| `debug-mode_state` | Debug | 2 | 2 | 2 | 0 |
| `release-off-config` | Release tests-off | 0 | 0 | build | 0 |
| `release-off-mgr_ipc` | Release tests-off | 0 | 0 | build | 0 |
| `release-off-state_report` | Release tests-off | 0 | 0 | build | 0 |
| `release-off-update_guard` | Release tests-off | 0 | 0 | build | 0 |
| `release-off-health` | Release tests-off | 0 | 0 | build | 0 |
| `release-off-mode_state` | Release tests-off | 0 | 0 | build | 0 |
| `release-health-on` | Release tests-on | 2 | 2 | 2 | 0 |
| `root-Debug-real` | root Debug real | 9 | 9 | 9 | 0 |
| `root-Release-real` | root Release real | 9 | 9 | 9 | 0 |
| `mgr-real` | mock MGR + real mgr_ipc | 6 | 6 | 6 | 0 |
| `asan-config` | ASan | 3 | 3 | 3 | 0 |
| `asan-mgr_ipc` | ASan | 5 | 5 | 5 | 0 |
| `asan-state_report` | ASan | 2 | 2 | 2 | 0 |
| `asan-update_guard` | ASan | 1 | 1 | 1 | 0 |
| `asan-health` | ASan | 2 | 2 | 2 | 0 |
| `asan-mode_state` | ASan | 2 | 2 | 2 | 0 |
| `ubsan-config` | UBSan | 3 | 3 | 3 | 0 |
| `ubsan-mgr_ipc` | UBSan | 5 | 5 | 5 | 0 |
| `ubsan-state_report` | UBSan | 2 | 2 | 2 | 0 |
| `ubsan-update_guard` | UBSan | 1 | 1 | 1 | 0 |
| `ubsan-health` | UBSan | 2 | 2 | 2 | 0 |
| `ubsan-mode_state` | UBSan | 2 | 2 | 2 | 0 |
| `tsan-default-config` | TSan 기본 seccomp | 3 | 3 | 0 | runtime 3 |
| `tsan-default-mgr_ipc` | TSan 기본 seccomp | 5 | 5 | 0 | runtime 5 |
| `tsan-default-state_report` | TSan 기본 seccomp | 2 | 2 | 0 | runtime 2 |
| `tsan-default-update_guard` | TSan 기본 seccomp | 1 | 1 | 0 | runtime 1 |
| `tsan-default-health` | TSan 기본 seccomp | 2 | 2 | 0 | runtime 2 |
| `tsan-default-mode_state` | TSan 기본 seccomp | 2 | 2 | 0 | runtime 2 |
| `tsan-config` | TSan unconfined | 3 | 3 | 3 | 0 |
| `tsan-mgr_ipc` | TSan unconfined | 5 | 5 | 5 | 0 |
| `tsan-state_report` | TSan unconfined | 2 | 2 | 2 | 0 |
| `tsan-update_guard` | TSan unconfined | 1 | 1 | 1 | 0 |
| `tsan-health` | TSan unconfined | 2 | 2 | 2 | 0 |
| `tsan-mode_state` | TSan unconfined | 2 | 2 | 2 | 0 |

stress 재실행은 다음 명령을 portable 및 mgr real build에 각각 적용했고 각 tree에서 006 10회와 007 10회, 총 20회가 통과했다.

```bash
ctest --test-dir "$build" \
  -R '^(SNS-CORE-006|SNS-CORE-007-mgr-ipc)$' \
  --repeat until-fail:10 --output-on-failure --timeout 240
```

## 14. Sanitizer

| 환경 | ASan | UBSan | TSan | 근거 |
|---|---|---|---|---|
| macOS arm64 | `NOT_PERFORMED` | 15/15 PASS | `NOT_PERFORMED` | ASan 007이 report 없이 60.02초 timeout; UBSan 6개 feature 직접 실행; TSan 미지원 |
| Ubuntu 22.04 arm64 Docker | 15/15 PASS | 15/15 PASS | 기본 seccomp `NOT_PERFORMED`, unconfined 15/15 PASS | 기본 환경은 15개 모두 test body 전 runtime CHECK; unconfined는 CTest 전부 통과하고 race report 0 |

사용 flag와 runtime option은 다음과 같다.

```text
ASan: -fsanitize=address -fno-omit-frame-pointer
      ASAN_OPTIONS=abort_on_error=1:detect_leaks=1
UBSan: -fsanitize=undefined -fno-sanitize-recover=all -fno-omit-frame-pointer
       UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1
TSan: -fsanitize=thread -fno-omit-frame-pointer
      TSAN_OPTIONS=halt_on_error=1
```

기본 seccomp TSan의 모든 실패는 `personality(... ADDR_NO_RANDOMIZE) ... -1`에 따른 `ThreadSanitizer CHECK failed`로 test body 전에 발생했다. 따라서 functional FAIL이나 data-race로 해석하지 않고 실행 불가로 분류했다. seccomp를 해제한 동일 exact tree에서는 15/15, mgr_ipc 5/5, 006과 007, 007 추가 3/3이 모두 통과했고 sanitizer report는 0건이었다.

## 15. 결과 문서 감사

- Fix 2 결과의 ledger는 실제 `0fd163d0055332a300dc234f5138d81b04ab0c05`로 정정되어 있다.
- Review 3 artifact와 최신 파일 commit은 모두 `d2710a2f362caf54ebe30f358e613207c67e97a7`이다.
- Fix 3 시작점은 Review 3 artifact이고, 구현 SHA는 `78e3c40d8dbd82a14ae7f3583a7f1b98cdde283f`다.
- Fix 3 결과 artifact는 `02549647fe89c84e73ea8c94b64c86b57594ab70`, 실제 결과 ledger는 `5a3343d07c51f03a513e0f58bc65056887414d06`다.
- 구현 변경은 9개 파일이고 결과 artifact까지는 10개 파일이다. 문서의 수치와 일치한다.
- Fix 3 결과 문서 32행과 153행에는 자기 ledger를 뜻하는 `PENDING_LEDGER_COMMIT`이 남아 있다. 문서가 자기 commit SHA를 스스로 확정할 수 없어 다음 Review의 Git 조회로 넘긴다는 설명과 함께 있으며, 실제 값은 위에서 확정했다. 허용된 self-reference다.
- 세션 결과 101행의 같은 문자열은 Fix 2의 과거 placeholder를 실제 SHA로 교체했다는 이력 문장이지 미확정 값이 아니다.
- Fix 2 문서는 callback action barrier와 외부 operation claim/wait를 구분하며 종전 과장을 정정했다. Fix 3 barrier 설명은 실제 event 관찰과 source assertion에 일치한다.
- 기본 seccomp TSan, Review 3 unconfined 14/15, Fix 3 unconfined 15/15가 서로 다른 시점·조건으로 분리되어 있다.
- macOS ASan timeout은 PASS로 계산되지 않았다.
- RV1106 cross-build, 실제 보드 및 하드웨어 실행은 수행하지 않았다고 명시되어 있고 수행한 것으로 과장한 표현은 없다.

`CDX-W1-SENSOR-CORE-007`을 `RESOLVED`로 판정한다.

## 16. 신규 Finding

신규 Critical 0, High 0, Medium 0, Low 0이다. 재현 가능한 신규 결함은 발견하지 못했다.

## 17. 검증 한계

- macOS ASan 007은 60초 내 완료되지 않아 macOS ASan 실행 성공 근거로 사용하지 않았다. Linux arm64 ASan 전체 15/15로 sanitizer 요구를 보완했다.
- Docker 기본 seccomp TSan은 runtime 제약으로 test body에 진입하지 못했다. 동일 이미지와 exact tree의 seccomp unconfined 실행으로 전체 15/15와 race report 0을 확인했다.
- Darwin real `SOCK_SEQPACKET`, RV1106 cross-build, 실제 보드·센서·장시간 현장 운용은 이번 재검증 범위에서 실행하지 않았다.
- 결과는 exact Fix 3 commit의 source와 명시된 macOS/Docker 환경에 대한 검증이다.

## 18. 최종 판정과 권고

최종 판정은 `PASS`다.

- 기존 Critical·High Finding은 모두 `RESOLVED`다.
- 신규 Critical·High Finding은 0이다.
- start/destroy 원자성 공백은 lifecycle lock으로 닫혔고 결정적 barrier가 두 합법 순서를 실제로 강제했다.
- 필수 test, real transport, ASan, UBSan, unconfined TSan, fd/thread baseline이 통과했다.
- 허용 경로 위반과 Foundation·contract 변경은 0이다.
- 남은 두 환경 제약은 정확히 실행 불가/미실행으로 기록했으며 이번 판정을 뒤집는 product failure 근거는 아니다.

따라서 `78e3c40d8dbd82a14ae7f3583a7f1b98cdde283f`를 현재 Wave 1 integration 후보로 진행해도 된다. 후속 단계에서는 실제 결과 ledger `5a3343d07c51f03a513e0f58bc65056887414d06`와 이 Review 4 artifact commit을 Git에서 조회해 원장에 연결할 것을 권고한다.
