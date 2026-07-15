# CC-SENSOR-CORE Codex Review 3 재검증 결과

## 1. 검증 대상과 Worktree Gate

- 세션 ID: `CC-SENSOR-CORE`
- 모드: `CODEX_REVIEW_3`
- 관찰 작업 트리: `/Users/juganghyeon/Desktop/uniuni/projects/worktrees/sensor-core`
- 관찰 저장소: `sensor_to_Linux` (`https://github.com/Taehyeong-uniuni/sensor_to_Linux.git`)
- 관찰 브랜치: `feature/sensor-core`
- 시작 상태: clean
- 시작 HEAD: `0fd163d0055332a300dc234f5138d81b04ab0c05`
- Gate 판정: PASS

다음 명령을 production·test·기존 문서 조사 전에 실행했다.

```bash
pwd -P
git rev-parse --show-toplevel
git remote -v
git branch --show-current
git rev-parse HEAD
git status --short
```

production·test 검증은 repository 밖 temporary clone인
`/tmp/cc-sensor-core-review3.MqMXgC/sensor-core-review3`에서 수행했다. 이 clone은
`0aef477915b1481b4a93aa0e09670375612b58d9`를 detached HEAD로 checkout했고 시작과 종료 시 모두 clean이었다.

## 2. Exact SHA 원장

| 단계 | 40자리 SHA | 조회 결과 |
|---|---|---|
| Base (`contract-v1`) | `07809cb1f3f2b86a8e92ade661c48cb3adb97b52` | 일치 |
| 원 구현 | `800207949dc28a6e18a3eafe4399f8cb0eb3d811` | 일치 |
| Review 1 artifact | `2b1c3d5e8751c9d91431f47b8e9bfa2b4245ec3b` | 일치 |
| Fix 1 구현 | `0f01c25f3c7ae55ca0e5650ce69015006ab1ef6e` | 일치 |
| Fix 1 보고 | `c5921c02acd93c13eb675043ff8a6a76cb04b7a8` | 일치 |
| Review 2 artifact | `74b4096713e465ef0855fcf0fd707847c142101c` | 일치 |
| Fix 2 시작 HEAD | `e135d6aa08f57341087946c412ca8d9cccc324ae` | 일치 |
| Fix 2 구현 | `0aef477915b1481b4a93aa0e09670375612b58d9` | 일치 |
| Fix 2 보고 artifact | `6d24de19386feb5ca1e17b4fef257e5d47d345d4` | 일치 |
| Fix 2 보고 ledger | `0fd163d0055332a300dc234f5138d81b04ab0c05` | Git에서 확인 |
| Review 3 시작 HEAD | `0fd163d0055332a300dc234f5138d81b04ab0c05` | 일치 |

`FIX_2_REPORT_LEDGER_SHA`는 다음 명령으로 조회했으며 40자리 형식과 artifact→ledger ancestor 관계가 모두 통과했다.

```bash
git log -1 --format='%H' -- session_results/wave1/review/CC-SENSOR-CORE_CODEX_FIX_RESULT_2.md
git merge-base --is-ancestor 6d24de19386feb5ca1e17b4fef257e5d47d345d4 0fd163d0055332a300dc234f5138d81b04ab0c05
```

## 3. SHA lineage

다음 모든 ancestor 관계가 PASS였다.

```text
07809cb1f3f2b86a8e92ade661c48cb3adb97b52
→ 800207949dc28a6e18a3eafe4399f8cb0eb3d811
→ 2b1c3d5e8751c9d91431f47b8e9bfa2b4245ec3b
→ 0f01c25f3c7ae55ca0e5650ce69015006ab1ef6e
→ c5921c02acd93c13eb675043ff8a6a76cb04b7a8
→ 74b4096713e465ef0855fcf0fd707847c142101c
→ e135d6aa08f57341087946c412ca8d9cccc324ae
→ 0aef477915b1481b4a93aa0e09670375612b58d9
→ 6d24de19386feb5ca1e17b4fef257e5d47d345d4
→ 0fd163d0055332a300dc234f5138d81b04ab0c05
```

## 4. Scope 검사

`e135d6aa08f57341087946c412ca8d9cccc324ae...0aef477915b1481b4a93aa0e09670375612b58d9`를 검사했다.

- Production/Test Fix commit 변경 파일: 11개
- 삽입/삭제: 730/149
- Fix Result 2 포함 Fix 2 고유 변경 파일: 12개
- Added/Modified/Deleted: 구현 commit은 11개 모두 Modified, Fix Result 2는 별도 artifact commit에서 Added
- `git diff --check`: PASS
- Allowed path violation: 0
- Foundation·contract 변경: 0
- root `CMakeLists.txt`, `CMakePresets.json`, `cmake/**` 변경: 0
- Review 1·2 변경: 0
- 다른 session production 변경: 0
- generated binary: 0
- symlink/path escape: 0
- production mock linkage: 0

구현 commit의 실제 경로는 다음과 같다.

```text
M session_results/wave1/CC-SENSOR-CORE.md
M src/features/health/sensor_lifecycle.c
M src/features/health/sensor_lifecycle.h
M src/features/mgr_ipc/CMakeLists.txt
M src/features/mgr_ipc/mgr_ipc_client.c
M src/features/mgr_ipc/mgr_ipc_client.h
M tests/integration/sensor_core/mgr_ipc/test_real_transport_integration.c
M tests/unit/sensor_core/health/test_lifecycle.c
M tests/unit/sensor_core/mgr_ipc/fake_transport.c
M tests/unit/sensor_core/mgr_ipc/fake_transport.h
M tests/unit/sensor_core/mgr_ipc/test_mgr_ipc_client.c
```

## 5. Review 2 Finding 재판정

| Finding | Severity | Review 3 판정 | 근거 요약 |
|---|---|---|---|
| `CDX-W1-SENSOR-CORE-001` | Critical | `PARTIALLY_RESOLVED` | callback terminal cleanup은 개선됐지만 stopped client의 concurrent `start()`/`destroy()`에 worker 생성 후 free 가능한 원자성 공백이 남음 |
| `CDX-W1-SENSOR-CORE-004` | High | `RESOLVED` | CONNECT 성공 전 public state·callback·application send 차단과 오류별 close/retry를 직접 확인 |
| `CDX-W1-SENSOR-CORE-005` | High | `RESOLVED` | 같은 process/path의 no-replay, public 65,536/65,537, recovery, Linux fd/thread를 직접 assertion |
| `CDX-W1-SENSOR-CORE-007` | Medium | `PARTIALLY_RESOLVED` | 실제 SHA·test 수는 대조됐으나 ledger placeholder 2개와 terminal barrier 과장 서술이 남음 |
| `CDX-W1-SENSOR-CORE-R2-001` | High | `RESOLVED` | 초기화 코드와 0xA5 storage 100회, nested fan-out, Debug·Release test를 확인 |

Review 2에서 해결됐던 회귀 항목은 `002`, `003`, `006` 모두 `RESOLVED`를 유지한다.

## 6. Terminal cleanup과 concurrency

확인된 개선은 다음과 같다.

- worker가 cancel source를 destroy한 뒤 `terminal_cleanup_complete`를 공개한다(`mgr_ipc_client.c:179-204`).
- callback `stop()`은 worker를 즉시 detach하지 않고, 다음 외부 terminal operation이 join하도록 한다.
- `join_in_progress`로 join 소유자를 하나로 제한하고 join 뒤에만 `started=false`, `stop_complete=true`를 공개한다(`mgr_ipc_client.c:214-279`).
- callback `destroy()`는 외부 join이 없을 때만 detach terminal ownership을 취한다(`mgr_ipc_client.c:459-490`).
- stop/stop, stop/destroy, destroy/destroy, send/shutdown, callback terminal stress의 기존 CTest가 macOS·Docker 및 Docker ASan·UBSan에서 통과했다.

그러나 외부 `start()`와 `destroy()`의 결합은 파괴 요청 확인과 worker 생성을 하나의 원자적 lifecycle 전이로 만들지 않는다.

| 순서 | `start()` thread | `destroy()` thread |
|---:|---|---|
| 1 | `client_enter()`로 API pin을 잡고 `started == false`를 확인 |  |
| 2 | `client_destroy_is_requested()`가 false를 반환(`mgr_ipc_client.c:353-356`) |  |
| 3 | lifecycle lock을 아직 잡지 않음 | destroy claim을 잡고 `stop_impl()` 호출 |
| 4 |  | `stop_impl()`은 `started == false`라 즉시 반환(`mgr_ipc_client.c:218-221`) |
| 5 | lifecycle lock을 잡고 cancel source 초기화, `pthread_create()`, `started=true` 공개(`mgr_ipc_client.c:358-387`) | 기존 API pin이 빠지기를 대기(`mgr_ipc_client.c:498-500`) |
| 6 | `client_leave()` | 대기가 끝나면 mutex·condition·storage를 파괴하고 free(`mgr_ipc_client.c:674-680`) |

따라서 새 worker가 시작된 뒤 그 worker를 stop/join하지 않은 채 storage가 해제될 수 있다. 이는 worker UAF, 동기화 primitive 파괴 중 접근, fd/cancel-source 누수 또는 비정상 종료로 이어질 수 있다. API pin drain 자체는 수행되지만, destroy가 terminal state를 관찰한 뒤 이미 진입한 `start()`가 새 side effect를 만들지 못하게 막지 못한다.

테스트도 이 interleaving을 반증하지 않는다.

- `test_concurrent_stop_destroy_stress()`는 stop/stop, stop/destroy, destroy/destroy만 실행하며 start/destroy가 없다(`test_mgr_ipc_client.c:736-771`).
- callback-stop 뒤 external destroy/restart test는 operation thread를 만든 직후 callback을 release한다(`test_mgr_ipc_client.c:800-817`). operation thread가 실제로 destroy claim 또는 start의 terminal wait에 진입했음을 확인하는 두 번째 barrier가 없어 요구 interleaving이 강제되지 않는다.
- 이 test는 500회 반복되지만 scheduler가 선택하지 않은 interleaving을 반복 횟수만으로 보장하지 못한다.

따라서 `CDX-W1-SENSOR-CORE-001`은 `PARTIALLY_RESOLVED`다.

## 7. CONNECT handshake

상태는 `DISCONNECTED → TRANSPORT_CONNECTED → HANDSHAKING → CONNECTED → STOPPING`으로 분리됐다. `send_connect_handshake()`는 CONNECT send 성공과 상태 일치를 모두 만족한 뒤에만 `CONNECTED`를 공개한다(`mgr_ipc_client.c:538-571`). public send는 `CONNECTED` 이외 상태에서 `SAVVY_ERR_NOT_CONNECTED`를 반환한다(`mgr_ipc_client.c:392-427`).

blocking handshake test에서 다음을 직접 확인했다.

- CONNECT가 block된 동안 `is_connected == false`
- connected callback 수 0
- application send는 `SAVVY_ERR_NOT_CONNECTED`
- release 전 transport send는 CONNECT 1건뿐
- release와 CONNECT 성공 뒤 connected callback 발생
- 이후 application send 허용, transport send 수 2
- CONNECT timeout, closed, I/O, protocol status 각각 close 후 reconnect 성공

`CT-IPC-002`는 macOS Debug/UBSan, Docker Debug/ASan/UBSan/TSan에서 통과했다. `CDX-W1-SENSOR-CORE-004`는 `RESOLVED`다.

## 8. IPC no-replay·boundary·recovery

`test_003b_repeated_drop_is_safe()` 한 process/path에서 다음을 직접 assertion한다(`test_mgr_ipc_client.c:225-286`).

- 시작 전 State/Property/Alert/Upload가 모두 `SAVVY_ERR_NOT_CONNECTED`
- transport send 수 0
- local State dedup tracker가 동일 두 번째 상태를 억제
- 첫 connect의 outbound는 CONNECT 1건뿐
- Config→Device 수신 뒤 disconnect
- reconnect의 누적 outbound는 CONNECT 2건뿐
- reconnect에서도 Config→Device 순서
- dropped outbound 및 cached Config/Device reverse send 0

public boundary test는 유효한 Upload payload를 만들어 `sensor_mgr_ipc_client_send()`를 통과시킨다(`test_mgr_ipc_client.c:352-425`).

- encoded 65,536 bytes: `SAVVY_OK`, transport send +1, MGR 수신 길이 65,536
- encoded 65,537 bytes: `SAVVY_ERR_OVERFLOW`, transport send +0
- wrong-direction action: `SAVVY_ERR_INVALID_ARGUMENT`, transport send +0
- invalid payload: `SAVVY_ERR_PROTOCOL`, transport send +0

inbound recovery test는 malformed envelope, wrong-direction action, invalid payload, oversized record를 버린 뒤 다음 정상 Config record 한 건을 callback으로 전달한다. real integration은 pre-connect 네 종류 drop 뒤 두 connect cycle에서 Config→Device 순서와 child exit 0을 확인했다. Docker real `mgr_ipc` 6/6이 통과했다. `CDX-W1-SENSOR-CORE-005`는 `RESOLVED`다.

## 9. Lifecycle callback_depth

`sensor_lifecycle_init()`은 primitive 초기화 전에 hook storage, `module_count`, `callback_depth`를 0으로 만든다(`sensor_lifecycle.c:24-44`). header는 destroy가 init 성공 뒤에만 유효함을 명시한다.

`test_poisoned_storage_initialization()`은 전체 storage를 `0xA5`로 오염한 뒤 100회 반복하여 다음을 확인한다.

- `callback_depth == 0`
- `module_count == 0`
- start/config/shutdown fan-out
- nested config fan-out
- callback 중 register
- callback 중 destroy 거부
- normal stop/destroy

test source는 `<assert.h>` 전에 `NDEBUG`를 해제한다. macOS와 Docker에서 Debug 및 health Release tests-on 각 2/2가 통과했다. `CDX-W1-SENSOR-CORE-R2-001`은 `RESOLVED`다.

## 10. fd·thread lifecycle

Ubuntu arm64 Docker의 non-sanitized verbose 실행 결과는 다음과 같다.

```text
SNS-CORE-006: OK (fd=5/9/6 threads=1/2/1 cycles=500)
```

- 시작 전 fd 5, 500-cycle 뒤 측정 9, stop/join 뒤 6
- 시작 전 thread 1, worker 중 2, stop/join 뒤 1
- fd final은 측정용 directory descriptor 허용치 이내
- thread final은 baseline과 정확히 일치
- 500회 disconnect callback 도달 assertion PASS
- real integration child는 `waitpid()` 뒤 정상 exit 및 status 0 assertion PASS

일반 Debug, ASan, UBSan에서 fd/thread lifecycle test가 통과했다. TSan의 별도 계측 thread 문제는 신규 finding에 기록한다.

## 11. Regression

| 항목 | 판정 | 근거 |
|---|---|---|
| `CDX-W1-SENSOR-CORE-002` Config·Device full replacement/raw JSON | `RESOLVED` 유지 | 매 apply가 Foundation defaults에서 시작하며 typed/raw를 한 snapshot으로 publish; full→partial, invalid parse latest-good test PASS |
| `CDX-W1-SENSOR-CORE-003` transport lifetime | `RESOLVED` 유지 | send/recv/close가 원 transport를 `state_lock → io_lock` 순서로 고정; send/shutdown stress PASS |
| `CDX-W1-SENSOR-CORE-006` lifecycle callback lock 분리 | `RESOLVED` 유지 | hook value snapshot 뒤 lock 밖 callback; start/config/shutdown 재진입과 callback-time registration PASS |

## 12. macOS 결과

환경: macOS 26.5.2 arm64, AppleClang 17.0.0, CMake 4.4.0.

기본 실행 명령은 다음과 같고 `feature`와 `build`를 아래 표의 각 build tree로 바꿔 독립 실행했다.

```bash
cmake -S "$REVIEW_REPO/src/features/$feature" -B "$build" \
  -DCMAKE_BUILD_TYPE=Debug -DSAVVY_BUILD_TESTS=ON
cmake --build "$build" --parallel 4
ctest --test-dir "$build" -N
ctest --test-dir "$build" --output-on-failure --timeout 120
```

| build tree | 구성 | `ctest -N` | 실행 | PASS | FAIL |
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
| `asan-mgr_ipc` | ASan, `detect_leaks=0` | 5 | 선택 실행 1 | 0 | timeout 1 |

Release tests-off는 다음 option으로 각각 configure/build하고 `ctest -N` 0을 확인했다.

```bash
cmake -S "$REVIEW_REPO/src/features/$feature" -B "$build" \
  -DCMAKE_BUILD_TYPE=Release -DSAVVY_BUILD_TESTS=OFF
cmake --build "$build" --parallel 4
ctest --test-dir "$build" -N
```

UBSan은 `-fsanitize=undefined -fno-sanitize-recover=all -fno-omit-frame-pointer`와 `UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1`로 실행했다. ASan은 build와 test 등록까지 성공했지만 `SNS-CORE-007-mgr-ipc`가 15초에 timeout되어 완료된 sanitizer 실행으로 인정하지 않았다. Darwin real `SOCK_SEQPACKET`은 `NOT_RUN`이다.

## 13. Docker arm64 결과

환경: Ubuntu 22.04 aarch64, GCC 11.4.0, CMake 3.22.1, image `sha256:73c8a9709607d1910231efb4648510e4d72052072629901fa28fd5c9f39753e7`. Exact Fix 2 clone은 `/src`에 read-only mount했다.

공통 실행 형식은 다음과 같다.

```bash
docker run --rm --platform linux/arm64 \
  --mount type=bind,src="$REVIEW_REPO",dst=/src,readonly \
  savvy-foundation-test:ubuntu22.04-arm64-v1 sh -lc '<아래 구성별 cmake/build/ctest 명령>'
```

| build tree | 구성 | `ctest -N` | 실행 | PASS | FAIL |
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
| `root-Debug-real` | root Debug real transport | 9 | 9 | 9 | 0 |
| `root-Release-real` | root Release real transport | 9 | 9 | 9 | 0 |
| `mgr-real` | mock MGR + real `mgr_ipc` | 6 | 6 | 6 | 0 |
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
| `tsan-config` | TSan, seccomp unconfined | 3 | 3 | 3 | 0 |
| `tsan-mgr_ipc` | TSan, seccomp unconfined | 5 | 5 | 4 | assertion 1 |
| `tsan-state_report` | TSan, seccomp unconfined | 2 | 2 | 2 | 0 |
| `tsan-update_guard` | TSan, seccomp unconfined | 1 | 1 | 1 | 0 |
| `tsan-health` | TSan, seccomp unconfined | 2 | 2 | 2 | 0 |
| `tsan-mode_state` | TSan, seccomp unconfined | 2 | 2 | 2 | 0 |

feature-local configure/build/test 명령은 macOS 표의 명령과 같되 source root를 `/src`로 사용했다. real transport는 다음 명령으로 실행했다.

```bash
cmake -S /src -B /tmp/review3/root-Debug-real \
  -DCMAKE_BUILD_TYPE=Debug -DSAVVY_BUILD_TESTS=ON -DSAVVY_IPC_REAL_TRANSPORT=ON
cmake --build /tmp/review3/root-Debug-real --parallel 4
ctest --test-dir /tmp/review3/root-Debug-real -N
ctest --test-dir /tmp/review3/root-Debug-real --output-on-failure --timeout 120

cmake -S /src -B /tmp/review3/root-Release-real \
  -DCMAKE_BUILD_TYPE=Release -DSAVVY_BUILD_TESTS=ON -DSAVVY_IPC_REAL_TRANSPORT=ON
cmake --build /tmp/review3/root-Release-real --parallel 4
ctest --test-dir /tmp/review3/root-Release-real -N
ctest --test-dir /tmp/review3/root-Release-real --output-on-failure --timeout 120

cmake -S /src/tools/mock_mgr -B /tmp/review3/mock-mgr -DCMAKE_BUILD_TYPE=Debug
cmake --build /tmp/review3/mock-mgr --parallel 4
cmake -S /src/src/features/mgr_ipc -B /tmp/review3/mgr-real \
  -DCMAKE_BUILD_TYPE=Debug -DSAVVY_BUILD_TESTS=ON \
  -DSENSOR_MGR_IPC_REAL_TRANSPORT=ON \
  -DMOCK_MGR_BINARY=/tmp/review3/mock-mgr/mock_mgr
cmake --build /tmp/review3/mgr-real --parallel 4
ctest --test-dir /tmp/review3/mgr-real -N
ctest --test-dir /tmp/review3/mgr-real --output-on-failure --timeout 120
```

## 14. Sanitizer

| 환경 | ASan | UBSan | TSan | 근거 |
|---|---|---|---|---|
| macOS arm64 | `NOT_PERFORMED` | 15/15 PASS | `NOT_PERFORMED` | ASan build·등록 후 `SNS-CORE-007-mgr-ipc`가 15초 timeout; UBSan 6개 feature 직접 실행 |
| Ubuntu 22.04 arm64 Docker | 15/15 PASS | 15/15 PASS | 전체 PASS 아님 | 기본 seccomp에서는 test body 전 personality CHECK 실패; seccomp unconfined 재실행은 14/15 PASS, `SNS-CORE-006` assertion 1 FAIL, TSan data-race report 0 |

Docker sanitizer flag는 각각 다음과 같다.

```text
ASan: -fsanitize=address -fno-omit-frame-pointer
ASAN_OPTIONS=abort_on_error=1:detect_leaks=1

UBSan: -fsanitize=undefined -fno-sanitize-recover=all -fno-omit-frame-pointer
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1

TSan: -fsanitize=thread -fno-omit-frame-pointer
TSAN_OPTIONS=halt_on_error=1
```

기본 Docker TSan은 다음 runtime CHECK로 test body에 진입하지 못했다.

```text
FATAL: ThreadSanitizer CHECK failed: ... personality(old_personality | ADDR_NO_RANDOMIZE) ... -1
```

`--security-opt seccomp=unconfined`으로 runtime 진입을 허용하자 concurrency 핵심 `SNS-CORE-007-mgr-ipc`를 포함한 14개 test는 통과했지만, `SNS-CORE-006`이 thread count assertion에서 실패했다. sanitizer error는 관찰되지 않았으나 suite가 전부 통과하지 않았으므로 TSan PASS로 판정하지 않는다.

## 15. 결과 문서 감사

감사 대상은 `session_results/wave1/CC-SENSOR-CORE.md`와 `session_results/wave1/review/CC-SENSOR-CORE_CODEX_FIX_RESULT_2.md`다.

일치한 내용:

- Base부터 Fix 2 report artifact까지 SHA
- 구현 11개, Fix Result 포함 고유 12개 파일
- feature-local 15개, real `mgr_ipc` 6개 CTest 수
- macOS/Docker Debug·Release·ASan·UBSan 표
- no-replay, public boundary, `/proc/self/task`, fd baseline source assertion
- RV1106 cross-build, board runtime, hardware QA 미수행 범위

남은 문서 문제:

1. Fix Result 2의 26행과 124행에 `PENDING_LEDGER_COMMIT`이 남아 있다. 실제 ledger SHA는 Git에서 `0fd163d0055332a300dc234f5138d81b04ab0c05`로 확인했다.
2. session result 32행과 Fix Result 2의 50·53행은 callback-stop과 external operation interleaving을 observable/condition-variable barrier로 강제했다고 서술한다. 실제 barrier는 callback action 완료와 callback release만 제어하며, 새 external operation thread가 destroy claim 또는 restart wait에 진입했는지는 관찰하지 않는다.
3. Fix 2 report의 TSan 미수행 서술은 기본 Docker seccomp 결과와 일치한다. Review 3에서 seccomp unconfined 재시도를 추가했지만 1개 assertion 실패로 전체 PASS는 아니었다.

따라서 `CDX-W1-SENSOR-CORE-007`은 `PARTIALLY_RESOLVED`다.

## 16. 신규 Finding

### CDX-W1-SENSOR-CORE-R3-001

- Severity: Low
- 제목: TSan 계측 실행의 추가 thread를 client worker로 오인해 fd/thread test가 실패함
- Affected file: `tests/unit/sensor_core/mgr_ipc/test_mgr_ipc_client.c:608-626`
- 재현 명령:

```bash
docker run --rm --platform linux/arm64 --security-opt seccomp=unconfined \
  --mount type=bind,src="$REVIEW_REPO",dst=/src,readonly \
  savvy-foundation-test:ubuntu22.04-arm64-v1 sh -lc '
    build=/tmp/review3/tsan-mgr_ipc
    cmake -S /src/src/features/mgr_ipc -B "$build" \
      -DCMAKE_BUILD_TYPE=Debug -DSAVVY_BUILD_TESTS=ON \
      -DCMAKE_C_FLAGS="-fsanitize=thread -fno-omit-frame-pointer" \
      -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=thread"
    cmake --build "$build" --parallel 4
    TSAN_OPTIONS=halt_on_error=1 ctest --test-dir "$build" \
      -R "^SNS-CORE-006$" --output-on-failure --timeout 180
  '
```

- 관찰 결과: `test_mgr_ipc_client.c:626`의 `threads_running == threads_baseline + 1` assertion에서 abort했다. 외부 `/proc/<pid>/task` 표본 1,069회에서 TSan process는 최소 1, 최대 3 threads였고, 같은 방식의 non-sanitized process는 최대 2 threads이며 test 내부 출력도 `threads=1/2/1`이었다. 즉 TSan 계측 실행에서 생긴 세 번째 thread 때문에 정확히 `baseline + 1`이라는 가정이 깨졌다. 같은 TSan build의 나머지 mgr_ipc 4개 test는 통과했고 TSan data-race report는 없었다.
- 기대 결과: 계측 runtime 자체의 보조 thread와 client worker를 구분하면서 client 시작 전/실행 중/stop 뒤의 worker 및 fd baseline을 검증해야 한다.
- 영향: seccomp 제약을 해제해도 TSan 15개 suite를 끝까지 PASS시킬 수 없고 concurrency 검증 결과가 불완전하게 남는다. non-sanitized production 동작 실패 근거는 아니다.
- 최소 수정 범위: test-only. baseline 전에 계측 runtime thread를 안정적으로 초기화해 baseline에 포함하거나, client-owned worker의 생성·종료를 별도 observable signal로 확인하고 process thread baseline은 final 복귀 검증으로 분리한다.
- 재검증 방법: Ubuntu arm64 Docker에서 기본 seccomp 실패를 별도 기록한 뒤 seccomp unconfined TSan 6개 feature 15개를 모두 실행하고, 15/15 CTest와 TSan report 0을 함께 확인한다. non-sanitized `/proc/self/task`의 1→2→1 및 fd final baseline assertion도 유지해야 한다.

신규 finding 집계:

- Critical: 0
- High: 0
- Medium: 0
- Low: 1

## 17. 검증 한계

- macOS real AF_UNIX `SOCK_SEQPACKET`: `NOT_RUN`
- macOS ASan test body 완료: `NOT_PERFORMED` (timeout)
- macOS TSan: `NOT_PERFORMED`
- Ubuntu TSan 전체 suite PASS: 미달성(14/15 PASS, assertion 1 FAIL)
- RV1106 cross-build: `NOT_PERFORMED`
- board runtime: `NOT_PERFORMED`
- hardware QA: `NOT_PERFORMED`
- read-only Review 범위에 따라 발견한 `start()`/`destroy()` 경쟁 조건을 수정하거나 별도 test harness를 추가하지 않았다. 판정은 exact source의 합법적인 interleaving과 기존 test assertion 범위를 근거로 한다.

## 18. 최종 판정과 권고

- 기존 finding: `RESOLVED` 6개(`002`, `003`, `004`, `005`, `006`, `R2-001`), `PARTIALLY_RESOLVED` 2개(`001`, `007`)
- 신규 finding: Low 1개
- Allowed path violation: 0
- Foundation·contract 변경: 0
- VERDICT: `FAIL`
- MERGE_CANDIDATE: `NO`

일반 CTest, real transport, ASan, UBSan 결과는 모두 양호했지만 기존 Critical `CDX-W1-SENSOR-CORE-001`의 concurrent `start()`/`destroy()` UAF 가능 경로가 남아 있어 PASS 조건을 충족하지 않는다.

권고:

1. destroy claim과 start 전이를 같은 lifecycle protocol에서 직렬화하여, destroy가 terminal state를 관찰한 뒤 어떤 기존 API pin도 새 worker/cancel source를 만들 수 없게 한다.
2. stopped client 및 callback-stop 직후의 start/destroy 경합을 두 번째 observable barrier로 강제하고 500회 이상 ASan·UBSan·가능한 TSan에서 반복한다.
3. TSan helper thread를 고려하도록 fd/thread test의 baseline 측정을 test-only로 보완하되, non-sanitized 1→2→1 직접 assertion은 약화하지 않는다.
4. Fix Result 2 ledger placeholder와 terminal barrier 서술을 실제 Git 및 test 범위에 맞게 정정한 뒤 재검증한다.
