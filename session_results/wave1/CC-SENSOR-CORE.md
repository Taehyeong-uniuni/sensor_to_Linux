# CC-SENSOR-CORE 세션 결과

## 1. 계보와 기준

- 세션: `CC-SENSOR-CORE-FIX-2`
- 브랜치: `feature/sensor-core`
- 작업 트리: `/Users/juganghyeon/Desktop/uniuni/projects/worktrees/sensor-core`
- Foundation 기준: `07809cb1f3f2b86a8e92ade661c48cb3adb97b52` (`contract-v1`)
- 원 구현: `800207949dc28a6e18a3eafe4399f8cb0eb3d811`
- Review 1 artifact: `2b1c3d5e8751c9d91431f47b8e9bfa2b4245ec3b`
- Fix 1 구현: `0f01c25f3c7ae55ca0e5650ce69015006ab1ef6e`
- Fix 1 결과 보고: `c5921c02acd93c13eb675043ff8a6a76cb04b7a8`
- Review 2 artifact 및 최신 파일 SHA: `74b4096713e465ef0855fcf0fd707847c142101c`
- Fix 2 시작 HEAD: `e135d6aa08f57341087946c412ca8d9cccc324ae`
- Fix 2 구현 commit: 이 문서와 코드·테스트 10개 파일을 함께 담는 Production/Test Fix commit이다. 자체 commit SHA는 이 파일 안에 순환 참조로 넣을 수 없으므로, Git에서 조회한 정확한 40자리 값은 `CC-SENSOR-CORE_CODEX_FIX_RESULT_2.md`의 Exact SHA 원장에 기록한다.
- 원 구현 diff(`contract-v1...8002079`): 37 files, 37 added / 0 modified / 0 deleted. 이전의 `35 files` 표기는 잘못된 값이다.

Production/Test Fix commit의 실제 변경 파일 수는 이 문서를 포함해 11개다. 이후 Fix Result 2 신규 문서까지 포함한 전체 Fix 2 고유 변경 파일 수는 12개다.

## 2. Finding 수정 결과

- `CDX-W1-SENSOR-CORE-001`: callback `stop()`은 self-join하지 않고 worker를 joinable 상태로 남긴다. 외부 `stop()`·`start()`·`destroy()` 중 하나만 join을 소유하며, worker 실행·transport close·cancel-source destroy가 끝난 뒤에만 terminal completion을 공개한다. callback `destroy()`는 외부 join과 경합하지 않을 때만 worker terminal path로 지연된다.
- `CDX-W1-SENSOR-CORE-004`: 연결 상태를 `DISCONNECTED → TRANSPORT_CONNECTED → HANDSHAKING → CONNECTED → STOPPING`으로 분리했다. CONNECT build/send 성공 전에는 public connected 상태와 callback을 공개하지 않고 일반 application send를 거부한다.
- `CDX-W1-SENSOR-CORE-005`: 시작 전 State/Property/Alert/Upload 실제 drop, local state dedup, connect/reconnect 뒤 CONNECT·Config·Device 순서와 replay 부재를 하나의 경로에서 직접 검증한다. 65,536/65,537 경계는 유효한 Upload payload로 `sensor_mgr_ipc_client_send()`를 통과시켜 transport send 횟수까지 검사한다. Linux thread 수는 `/proc/self/task`로 직접 잰다.
- `CDX-W1-SENSOR-CORE-007`: 이 문서의 Fix 1 SHA placeholder, 파일 수, CTest 수, sanitizer 범위를 실제 Git·CTest 결과로 바로잡았다. Fix 2 exact SHA는 별도 Fix Result 2 원장에서 확정한다.
- `CDX-W1-SENSOR-CORE-R2-001`: `sensor_lifecycle_init()`이 primitive 초기화 전에 hook storage, `module_count`, `callback_depth`를 0으로 정의한다. 실패 시 destroy 가능 범위를 문서화하고, `0xA5` storage 100회 반복 및 nested fan-out/callback-time destroy 정책을 Debug·Release에서 검사한다.

Review 2에서 해결 판정된 `002` Config/Device full replacement·raw JSON, `003` transport 수명 고정, `006` lifecycle callback lock 분리는 regression 범위로 유지했다.

## 3. 동시성과 IPC 직접 검증

- `SNS-CORE-007-mgr-ipc`: callback-stop + 즉시 외부 destroy, callback-stop + 즉시 restart, callback-destroy + concurrent stop을 observable barrier로 강제하며 500회 반복한다. 별도로 blocked connect/recv destroy, stop/stop, stop/destroy, destroy/destroy, send/close 경합도 반복한다. callback race를 피하기 위한 고정 `sleep(100ms)`는 사용하지 않는다.
- handshake blocking fake transport에서 CONNECT send 중 `is_connected == false`, connected callback 0, application send 거부를 확인하고, release 후 CONNECT가 먼저 전송된 뒤 application send가 가능함을 transport send count로 확인한다.
- CONNECT timeout/closed/I/O/protocol 실패 각각에 대해 close 후 reconnect 성공을 검사한다.
- `SNS-CORE-003b-mgr-ipc`는 실제 pre-connect drop과 state dedup을 같은 process/path에 연결하고, 두 연결에서 outbound transport send가 CONNECT 2회뿐임을 직접 검사한다.
- 실제 mock MGR는 CONNECT 뒤 예상하지 않은 Sensor outbound를 실패 처리한다. child exit 0은 pre-connect drop과 cached Config/Device reverse send가 connect/reconnect 뒤 replay되지 않았다는 통합 근거다.
- 정확히 65,536 bytes인 application envelope은 public send와 transport send를 통과하고, 65,537 bytes는 transport send count 증가 없이 `SAVVY_ERR_OVERFLOW`로 거부된다. wrong-direction, invalid payload, malformed envelope, oversized-record 뒤 정상 record 복구도 검사한다.
- `SNS-CORE-006`은 Ubuntu에서 시작 전·worker 실행 중·500회 cycle 뒤의 `/proc/self/task` 수와 fd baseline을 직접 비교한다.

## 4. 테스트 결과

| 환경 / 구성 | `ctest -N` | 실제 실행 | 결과 |
|---|---:|---:|---|
| macOS arm64, 6개 feature-local Debug, `-Wall -Wextra -Werror` | 15 | 15 | 15 PASS / 0 FAIL |
| macOS arm64, 6개 feature-local Release tests-off | 0 | 0 | 6개 build PASS |
| macOS arm64, health Release tests-on | 2 | 2 | 2 PASS / 0 FAIL |
| macOS arm64, 6개 feature-local UBSan | 15 | 15 | 15 PASS / 0 FAIL |
| Ubuntu 22.04 arm64 Docker, 6개 feature-local Debug | 15 | 15 | 15 PASS / 0 FAIL |
| Ubuntu 22.04 arm64 Docker, 6개 feature-local Release tests-off | 0 | 0 | 6개 build PASS |
| Ubuntu 22.04 arm64 Docker, health Release tests-on | 2 | 2 | 2 PASS / 0 FAIL |
| Ubuntu Docker, root Debug real-transport | 9 | 9 | 9 PASS / 0 FAIL |
| Ubuntu Docker, root Release real-transport | 9 | 9 | 9 PASS / 0 FAIL |
| Ubuntu Docker, mock MGR + real mgr_ipc | 6 | 6 | 6 PASS / 0 FAIL |
| Ubuntu Docker, 6개 feature-local ASan | 15 | 15 | 15 PASS / 0 FAIL |
| Ubuntu Docker, 6개 feature-local UBSan | 15 | 15 | 15 PASS / 0 FAIL |

기본 feature-local CTest 구성은 총 15개이며, real-transport mgr_ipc 구성은 6개다. 서로 다른 build tree의 수를 합쳐 단일 CTest 수로 표기하지 않는다.

## 5. Sanitizer와 검증 경계

- macOS ASan: `NOT_PERFORMED`. LeakSanitizer 활성 시 `detect_leaks is not supported on this platform`으로 본문 전 중단되었고, 비활성 재시도도 `config` 및 `mgr_ipc` 첫 테스트가 본문 출력 전 정지해 수동 중단했다. PASS로 간주하지 않는다.
- Ubuntu arm64 ASan: 6개 기능 15/15 PASS.
- macOS 및 Ubuntu arm64 UBSan: 각각 6개 기능 15/15 PASS.
- Ubuntu arm64 TSan: `NOT_PERFORMED`. 빌드는 성공했으나 모든 mgr_ipc 테스트가 본문 진입 전에 `ThreadSanitizer CHECK failed ... personality(old_personality | ADDR_NO_RANDOMIZE) ... -1`로 종료했다.
- RV1106 cross-build, board runtime, hardware QA: `NOT_PERFORMED`.

## 6. 범위

Fix 2는 허용된 Health, Mgr IPC, unit/integration test 및 session-result 경로만 변경한다. Foundation, contract, root CMake, Review 1·2, 다른 세션 production 경로는 변경하지 않는다.

## 7. 상태

- `FIX_IMPLEMENTATION_FINISHED`
- `AWAITING_CODEX_REVERIFY`
