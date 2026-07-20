# CODEX CC-SENSOR-CORE WAVE 1 REVIEW

## 1. Target

- Session: `CC-SENSOR-CORE`
- Branch: `feature/sensor-core`
- Base SHA: `07809cb1f3f2b86a8e92ade661c48cb3adb97b52`
- Implementation SHA: `800207949dc28a6e18a3eafe4399f8cb0eb3d811`
- Original implementation report: `session_results/wave1/CC-SENSOR-CORE.md`
- Review artifact: `session_results/wave1/review/CC-SENSOR-CORE-CODEX-REVIEW.md`
- Original worktree status at review start: clean
- Exact commit verification: repository 밖 `/tmp` local clone에서 implementation SHA를 detached checkout하여 빌드·테스트

## 2. Verdict

- Verdict: **FAIL**
- Critical: **1**
- High: **4**
- Medium: **2**
- Low: **0**

Critical UAF, Android Config/Device parity 실패, IPC lifecycle 결함 및 required IPC test의 거짓 PASS가 확인됐다.

## 3. Provenance

- `git branch --show-current`: `feature/sensor-core`
- `git rev-parse HEAD`: implementation SHA와 일치
- `contract-v1^{commit}`: base SHA와 일치
- `contract-v1` object type: annotated `tag`
- Base ancestor check: PASS
- `git show --check`: PASS
- Contract manifest SHA256: `a69536c286839c97e05ed7f54b5834d843f94eae4a9221ad6213de93d268fa6e`
- Dependency manifest SHA256: `9934277d3a8d1dabd1c2632d3501743f8d2a57218c6dd6f3635b2b3844296ad2`
- Changed files: 37
- Added/Modified/Deleted: 37/0/0
- Allowed path violations: 0
- Symlink/path escape: 0
- Binary/generated artifact: 0
- Foundation/contract/root CMake/root `SESSION_RESULT.md` changes: 0
- Dirty worktree/post-commit drift at review start: 없음

Claude Code 결과 보고서의 `35 files` 주장은 실제 Git diff의 37개와 불일치한다.

## 4. Android Traceability

### SNC-01

- Config→Device cached load 순서와 Device 상태 필드 9개 reset은 pinned source와 일치한다.
- reset 대상 이외 필드는 보존된다.
- 결과 보고서의 “모든 module init 전에 cached Config/Device load” 주장은 pinned `MainActivity.onCreate()`와 다르다. `ThreadPirOut.start()`가 211~212행, Config/Device load가 214~215행이다.
- lifecycle hook callback 재진입 시 deadlock이 재현됐다.

### SNC-02

- `useRknn` 및 `dataCollection` startup/live raw 변환 규칙은 일치한다.
- Config/Device invalid parse 시 latest-good snapshot 보존은 정상이다.
- runtime Config/Device 적용은 Android parity에 실패한다. Android는 수신 JSON 저장 후 `gson.fromJson()`으로 새 DTO를 할당하지만 C 구현은 현재 `working` 구조체에 partial merge한다.
- raw JSON 자체를 저장하지 않으므로 Android의 전체 JSON 저장 의미도 제공하지 않는다.

### SNC-03

- Sensor client/MGR server 역할, CONNECT 송신, EOF reconnect 기본 경로는 확인됐다.
- concurrent stop/destroy UAF, send/close fd lifetime race, CONNECT send 실패 무시가 존재한다.
- real integration test가 필수 scenario 전부를 assertion하지 않는다.

### SNC-04

- APK update 후 guard가 true가 되고 process lifetime 동안 reset API가 없다.
- repeated update는 idempotent이며 후속 PIR-in query는 차단된다.
- 실제 APK 설치/update handoff/ToF worker는 추가하지 않았다.

## 5. Code Review

- 정상적인 snapshot publish/acquire/release read/write 경로는 Foundation lifetime 규칙을 따른다.
- duplicate key strict reject와 invalid parse atomicity는 유지된다.
- production Stream/Voice/RKNN/Mic/ToF worker가 추가되지 않았다.
- durable/offline/disk queue가 없다.
- production mock/dummy linkage가 없다.
- compiler 검증:
  - macOS exact SHA feature-local build: `-Wall -Wextra -Wpedantic` 및 project `-Werror`, warning 0
  - Ubuntu arm64 exact SHA build: PASS, warning 0
- Claude Code 과정에서 언급된 “10 diagnostics in 1 file”은 committed report에 파일명과 내용이 남아 있지 않아 개별 진단을 식별할 수 없었다. production compiler diagnostic은 재현되지 않았다.

## 6. Test Results

기본 실행 패턴:

```bash
cmake -S src/features/<name> -B <temporary-build>/<name>
cmake --build <temporary-build>/<name>
ctest --test-dir <temporary-build>/<name> --output-on-failure
```

| Test ID | macOS | Ubuntu arm64 | Independent result | Assertion/coverage notes |
|---|---|---|---|---|
| SNS-CORE-001 | PASS | PASS | PASS | config 18 + state_report 6 static assert sites |
| SNS-CORE-002 | PASS | PASS | FAIL | 16; 잘못된 partial merge 동작을 허용 |
| SNS-CORE-002a | PASS | PASS | PASS | 15 |
| SNS-CORE-003 | PASS | PASS | PASS | 12 |
| SNS-CORE-003a | PASS | PASS | PASS | 4; pre-connect transport send 0 |
| SNS-CORE-003b | PASS | PASS | INSUFFICIENT | state_report 6 + mgr_ipc 5; 실제 drop과 dedup을 하나의 경로에서 검증하지 않음 |
| CT-IPC-002 | PASS | PASS | FAIL | fake 24, real 10; 필수 scenario 일부 미검증 |
| SNS-CORE-004 | PASS | PASS | PASS | static 6, loop 포함 dynamic 10 |
| SNS-CORE-005 | PASS | PASS | FAIL | 16; Android와 다른 partial merge를 PASS 조건으로 고정 |
| SNS-CORE-006 | PASS | PASS | INSUFFICIENT | 7; thread count와 stop 후 최종 fd count 미측정 |
| SNS-CORE-007 | PASS | PASS | FAIL | health 26 + mgr_ipc 14; concurrent/reentrant teardown 미검증 |

실제 CTest 구성:

- macOS feature-local: 15/15 PASS
- Ubuntu arm64 feature-local: 15/15 PASS
- Ubuntu real-transport mgr_ipc variant: 6/6 PASS(기존 5개 재실행 + `CT-IPC-002-real`)
- Claude Code 결과 보고서의 `18/18`, `19/19` 집계는 exact commit의 실제 CTest 구성과 일치하지 않는다.

## 7. Sanitizers

| Environment | ASan | UBSan | TSan | Notes |
|---|---|---|---|---|
| macOS arm64 | NOT_PERFORMED | PASS | NOT_PERFORMED | ASan 실행 hang으로 중단. UBSan은 6개 feature PASS. TSan mgr_ipc는 runtime 초기 segfault |
| Ubuntu arm64 Docker | FAIL | PASS | NOT_PERFORMED | 기존 suite ASan+UBSan PASS. 추가 concurrent stop/destroy harness에서 heap-use-after-free 검출. TSan은 `personality(ADDR_NO_RANDOMIZE)` 제약으로 실행 전 실패 |

## 8. Leak and Shutdown

- 기존 SNS-CORE-006의 60회 connect/disconnect fd 검사는 실행상 PASS했다.
- 기준은 cycle 5 시점이며 `+4` slack을 허용하고 stop 완료 뒤 baseline 회복을 측정하지 않는다.
- worker thread count는 실제 측정하지 않았다.
- 정상적인 blocked connect/recv stop은 1초 이내 반환했다.
- sequential double-stop은 PASS했다.
- concurrent stop/destroy는 ASan UAF로 FAIL했다.
- lifecycle callback 재진입은 반환하지 않아 중단했다.

## 9. Findings

### CDX-W1-SENSOR-CORE-001

- Severity: **Critical**
- Title: concurrent stop/destroy가 client를 조기 free하여 heap-use-after-free 발생
- Affected: `src/features/mgr_ipc/mgr_ipc_client.c:158-217`
- Foundation evidence: cancel source와 client storage는 waiter/stop 완료 전 파괴하면 안 된다.
- Reproduction: Ubuntu arm64 ASan build에서 blocking connector를 사용하고 한 thread가 `stop()`, 다른 thread가 `destroy()` 실행
- Observed: ASan이 192행에서 heap-use-after-free 검출. 217행에서 먼저 free된 객체를 stop thread가 읽었다.
- Expected: concurrent destroy가 진행 중 stop 완료를 기다리거나 명시적으로 차단해야 한다.
- Impact: memory corruption/UAF, mutex·cancel source 파괴 경쟁
- Minimum fix scope: `src/features/mgr_ipc/mgr_ipc_client.c/.h`, mgr_ipc concurrency tests
- Reverification: concurrent stop/stop, stop/destroy, callback-triggered stop/destroy ASan stress

### CDX-W1-SENSOR-CORE-002

- Severity: **High**
- Title: runtime Config/Device가 Android의 완전 교체 대신 이전 값에 merge됨
- Affected:
  - `src/features/config/config_store.c:90`
  - `src/features/config/device_store.c:105`
  - `tests/unit/sensor_core/config/test_config_store.c:118`
- Android evidence: pinned `MainActivity.actionConfig/actionDevice`는 JSON 저장 후 `gson.fromJson()`으로 새 DTO를 할당한다.
- Reproduction: 먼저 `serverIp=10.0.0.9, ftpIp=old` 적용 후 `{"decibel":70}` 적용
- Observed: `serverIp=10.0.0.9 ftpIp=old decibel=70`
- Expected: 누락 필드는 새 DTO/default 기준이어야 하며 원본 전체 JSON 저장 의미가 보존돼야 한다.
- Impact: stale Config/Device 상태와 잘못된 change notification
- Minimum fix scope: Config/Device stores와 SNS-CORE-002/005 tests
- Reverification: full→partial Config/Device 교체, raw JSON 보존, invalid parse latest-good 유지

### CDX-W1-SENSOR-CORE-003

- Severity: **High**
- Title: send가 transport 사본을 사용해 disconnect/close와 fd reuse race를 일으킬 수 있음
- Affected: `src/features/mgr_ipc/mgr_ipc_client.c:133-142,254-260`
- Foundation evidence: Foundation transport close도 concurrent close 후 fd reuse 위험을 명시한다.
- Reproduction: send의 state lock 범위와 disconnect close 경로 대조
- Observed: lock 안에서 fd transport를 값 복사한 뒤 unlock하고 send한다. 그 사이 원본 transport가 shutdown/close되고 fd가 재사용될 수 있다.
- Expected: send 종료까지 transport lifetime을 pin하거나 close와 직렬화해야 한다.
- Impact: 재사용된 무관한 fd로 IPC payload를 보낼 가능성
- Minimum fix scope: mgr IPC transport ownership/locking
- Reverification: concurrent send/disconnect/stop stress와 fd reuse instrumentation

### CDX-W1-SENSOR-CORE-004

- Severity: **High**
- Title: CONNECT handshake 전송 실패를 무시하고 connected callback/recv 단계로 진행
- Affected: `src/features/mgr_ipc/mgr_ipc_client.c:304-318`
- Android/Foundation evidence: 연결 직후 CONNECT가 성공해야 MGR의 Config→Device replay가 시작된다.
- Reproduction: send가 `SAVVY_ERR_TIMEOUT`, recv가 timeout을 반환하는 transport 주입
- Observed: `connected=1 callback=1`; CONNECT 실패에도 성공 상태와 callback이 노출됐다.
- Expected: CONNECT 실패 시 close/disconnect/reconnect 처리
- Impact: MGR replay 없이 client가 recv polling에 머물 수 있음
- Minimum fix scope: worker handshake error handling과 tests
- Reverification: CONNECT timeout/IO/closed injection 후 callback·reconnect 순서

### CDX-W1-SENSOR-CORE-005

- Severity: **High**
- Title: CT-IPC-002-real 및 lifecycle/leak tests가 필수 요구를 assertion하지 않아 거짓 PASS
- Affected:
  - `tests/integration/sensor_core/mgr_ipc/test_real_transport_integration.c:113-171`
  - `tools/mock_mgr/mock_mgr.c:73-109,177-190`
  - `tests/unit/sensor_core/mgr_ipc/test_mgr_ipc_client.c:178-362`
- Required evidence: CONNECT action/payload, reverse Config/Device 부재, dropped state replay 부재, 65,536/65,537 boundary, malformed/oversized, thread/fd stability
- Observed:
  - mock MGR는 CONNECT를 로그만 남기고 action/payload를 assert하지 않는다.
  - real test는 수신 action의 이름·순서를 확인하지 않고 callback count만 확인한다.
  - cached Config/Device 역송신 및 dropped state replay 부재를 확인하지 않는다.
  - 64 KiB 경계와 malformed/oversized test가 없다.
  - child exit status와 thread count를 assert하지 않는다.
- Expected: 각 required scenario에 직접적인 assertion
- Impact: 필수 test가 PASS해도 IPC contract 위반을 검출하지 못함
- Minimum fix scope: sensor_core IPC tests와 `tools/mock_mgr`
- Reverification: 강화된 CT-IPC-002-real 및 SNS-CORE-003b/006/007

### CDX-W1-SENSOR-CORE-006

- Severity: **Medium**
- Title: lifecycle hook을 registry mutex 보유 상태에서 호출하여 재진입 deadlock
- Affected: `src/features/health/sensor_lifecycle.c:63-69,79-85,99-105`
- Required evidence: callback reentrancy와 shutdown deadlock 방지
- Reproduction: `on_config_applied` callback에서 `sensor_lifecycle_notify_config_applied()` 재호출
- Observed: 실행이 반환하지 않아 중단
- Expected: callback 호출 전 registry snapshot을 만들고 mutex를 해제
- Impact: 재진입 callback이 daemon lifecycle을 정지시킬 수 있음
- Minimum fix scope: lifecycle fan-out와 reentrancy tests
- Reverification: start/config/shutdown callback 각각의 재진입 test

### CDX-W1-SENSOR-CORE-007

- Severity: **Medium**
- Title: Claude Code 결과 보고서의 provenance/test/sanitizer/Android evidence가 실제 결과와 불일치
- Affected: `session_results/wave1/CC-SENSOR-CORE.md`
- Reproduction: Git diff, `ctest -N`, pinned Android `git show`, macOS UBSan 결과 대조
- Observed:
  - implementation SHA를 exact 값으로 기록하지 않음
  - 35 files 주장, 실제 37
  - 18/18·19/19 주장, exact 기본 CTest 구성은 15개
  - macOS sanitizer 전체 미수행 주장과 달리 UBSan 6개 feature PASS
  - cached load가 모든 module init보다 먼저라는 Android 인용 불일치
  - “10 diagnostics” 파일/내용/판정 누락
- Expected: exact SHA와 실제 command output, pinned line evidence를 정확히 기록
- Impact: 독립 재현성과 승인 판단 신뢰도 저하
- Minimum fix scope: Claude Code session result와 필요한 test evidence
- Reverification: report-to-command-output audit

## 10. Non-findings / Confirmed Claims

- branch/tag/base/manifest provenance 정상
- 변경 37개 모두 allowed path의 신규 파일
- Foundation/contract/root CMake/root session result 변경 0
- duplicate-key reject 및 invalid parse atomicity 정상
- startup Device 9개 상태 필드 reset 및 나머지 필드 보존 정상
- useRknn/dataCollection 변환 정상
- pre-connect drop 및 durable replay queue 부재
- Sensor client/MGR server 역할 정상
- 기본 reconnect와 real AF_UNIX `SOCK_SEQPACKET` 연결 실행 성공
- update guard 정상
- production mock/worker/scope creep 없음
- compiler warning 0

## 11. Required Fix Scope

- `001`: mgr IPC stop/destroy synchronization과 concurrency tests
- `002`: Config/Device full-replace 및 raw JSON semantics
- `003`: transport send/close lifetime 직렬화
- `004`: CONNECT handshake failure 처리
- `005`: IPC real/fake integration assertions와 leak/thread 검사
- `006`: lifecycle callback lock 분리
- `007`: Claude Code 결과 보고서 정확성 보정

수정 금지 범위: Foundation, contracts, root CMake, 다른 Wave 1 session 경로.

## 12. Reverification Plan

1. `001` ASan concurrent stop/destroy harness 및 반복 stress
2. `002` full→partial Config/Device parity test
3. `003` concurrent send/disconnect fd reuse test
4. `004` CONNECT send error injection test
5. CT-IPC-002-real 전체 필수 scenario 및 65,536/65,537 boundary
6. lifecycle callback reentrancy test
7. macOS/Ubuntu 전체 feature-local regression
8. Ubuntu ASan+UBSan 및 가능한 환경의 TSan
9. Claude Code 결과 보고서와 actual Git/CTest output 재대조

## 13. Final Recommendation

- Merge candidate: **아님**
- Separate Codex fix session: **필요**
- Priority: `CDX-W1-SENSOR-CORE-001` → `002` → `003/004/005` → `006/007`
