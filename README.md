# sensor_to_Linux
 
SAVVY **Sensor** Android 애플리케이션(`savvy_sensor`)의 Linux 포팅 버전입니다. ToF/PIR 기반 감지, 백엔드로의 오디오 스트리밍, MGR이 지시하는 설정 반영을 담당하는 온디바이스 센서 프로세스를, Android/Java에서 이식 가능한 ANSI C11 코드로 옮긴 것입니다.
 
RV1106(실제 임베디드 타겟) cross-build 및 보드 bring-up은 별도 문서에서 다루며, 이 README에서는 의도적으로 제외했습니다. 여기서는 지금까지 검증된 소프트웨어 아키텍처와 Linux(macOS 호스트 + Docker Ubuntu 22.04 arm64) 빌드만 다룹니다.
 
## 상태
 
| 마일스톤 | 상태 | 참조 |
|---|---|---|
| Foundation (core/protocol/platform, 고정 baseline) | 완료 | 태그 `contract-v1` (`07809cb1f3f2b86a8e92ade661c48cb3adb97b52`) |
| Wave 1 (SENSOR-CORE + SENSOR-STREAM, 통합 및 재검증 완료) | **검증 완료** | 브랜치 `integration/wave1` (`309036420fe2ff8844ab77e4787ba7364d186468`) |
| Wave 2 | 시작 전 | — |
 
Critical/High 미해결 finding: **0**. 비차단(Low) 항목 2건이 내부적으로 추적 중입니다 — macOS에서 발생하는 정적 라이브러리 중복 링크 경고 하나, 그리고 send queue에 항목이 남아 있는 상태로 `stop()`/`destroy()`가 호출될 때 발생하는 (무제한 누수도 use-after-free도 아닌) bounded 메모리 누수 하나입니다. 둘 다 이 브랜치를 Wave 2 기준으로 사용하는 데는 영향이 없습니다.
 
## 아키텍처
 
### Foundation — `src/core/`, `src/protocol/`, `src/platform/`
 
`mgr_to_Linux`와 모양이 동일한, 공용·고정 인프라입니다.
 
- **`src/core/`** — `lifecycle`, `queue`, `clock`, `error`, `snapshot`.
- **`src/protocol/packet/`** — `packet_codec`, `stream_parser`(레거시 14바이트 serial framing).
- **`src/protocol/json/`** — `json_codec`, `config_codec`, `device_codec`, `data_result_codec`, `field_table` — cJSON을 직접 다루는 유일한 계층입니다.
- **`src/protocol/ipc/`** — `ipc_envelope_codec`, `ipc_action_catalog` — MGR↔Sensor 메시지 계약입니다.
- **`src/platform/interfaces/`** — portable reconnect 로직(host-mac mock transport / host-linux real transport).
- **`src/platform/linux/ipc/`** — 실제 `AF_UNIX SOCK_SEQPACKET` client(`ipc_client`, `ipc_transport_common`), `SAVVY_IPC_REAL_TRANSPORT=ON`일 때만 컴파일됩니다.
- **`src/platform/linux/tcp_8141/`** — `tcp_channel`: SENSOR-STREAM이 소유하는 TCP 8141 스트리밍 adapter입니다.
- **`third_party/cJSON/`** — vendor된 cJSON `v1.7.19`(commit `c859b25`), 정적 컴파일이며 수정하지 않습니다.
### CC-SENSOR-CORE (Wave 1)
 
`src/features/`:
 
- **`config/`** — `config_store`, `device_store`: 저장되는 Config/Device JSON 상태입니다.
- **`health/`** — `sensor_health`, `sensor_lifecycle`: 프로세스/lifecycle health 보고입니다.
- **`mgr_ipc/`** — `mgr_ipc_client`, `real_connector`, `cancel_source_portable`: MGR↔Sensor IPC 채널의 Sensor 측(client 역할: connect; 재접속 시 Config/Device를 자동으로 재동기화합니다).
- **`mode_state/`** — 동작 모드 상태 머신입니다.
- **`state_report/`** — MGR로 보내는 state/property/alert 보고입니다.
- **`update_guard/`** — Sensor 측 update/OTA 호환성 가드입니다.
### CC-SENSOR-STREAM (Wave 1)
 
- **`compression/`** — `bzip_codec`(libbz2 기반).
- **`result_policy/`** — 탐지 결과 정책/판단 로직입니다.
- **`wav/`** — `wav_encoder`.
- **`stream/`** — `session`: TCP 8141로의 전송을 구동하는 streaming session 상태 머신입니다(bounded send queue, depth 4).
- **`src/platform/linux/tcp_8141/`** — TCP 8141 wire adapter입니다(위 Foundation 절 참고 — 물리적으로는 foundation에 인접해 있지만 논리적으로는 SENSOR-STREAM 소유입니다).
ToF adapter / baseline / PIR / RKNN frame pipeline 모듈은 Wave 2A 범위이며 이 문서에서는 다루지 않습니다.
 
## 저장소 구조
 
```
contracts/            저장소 간 공유되는 고정 계약(manifest, IPC action catalog, JSON field policy, IPC schema)
include/savvy/         core/protocol/platform 공개 헤더
src/core/              Foundation 기본 요소
src/protocol/          Packet / JSON / IPC envelope codec
src/platform/          Portable interface + Linux AF_UNIX IPC + TCP 8141 adapter
src/features/          CC-SENSOR-CORE, CC-SENSOR-STREAM feature 모듈
tests/contract/        루트 수준 Foundation contract test (CT-PKT-*, CT-JSON-*, CT-IPC-*)
tests/unit/            feature별 unit test
tests/integration/      feature별 integration test
third_party/cJSON/     vendor된 JSON 라이브러리
tools/                 mock_mgr, mock_streaming_server test double
session_results/       세션별 구현/리뷰 기록 (Wave 1 이력)
```
 
`src/features/` 아래 각 feature(그리고 CI에서 실행되는 각 standalone unit, 예: `config`, `mode_state`, `mgr_ipc`, `tcp_8141`)는 root build에 등록되는 대신 Foundation 소스를 로컬에 vendoring하는 **독립 CMake subproject**입니다 — root `CMakeLists.txt`는 Foundation 자체와 그 contract test만 빌드합니다. 이 구조 덕분에 공유 global build graph 없이도 각 모듈을 독립적으로 configure/build/sanitize할 수 있습니다. 패턴은 아무 `src/features/*/CMakeLists.txt`에서나 확인할 수 있습니다.
 
## Contracts
 
`contracts/`는 `mgr_to_Linux`와 `sensor_to_Linux` 사이에 byte-identical하게 커밋되며, 그 해시는 `contracts/contract-manifest.sha256`에 고정되어 있습니다(현재 `a69536c2...268fa6e`, 매 merge/review 단계마다 재확인됨).
 
- `ipc_action_catalog.md` — MGR↔Sensor action/key catalog(방향, key, type).
- `json_field_policy.md` — 필드별 JSON 정책. 특히 `DataResult`는 의도적인 예외가 하나 있습니다: result 필드 누락·`null`은 `0`으로 처리하고, numeric string은 허용하며, fractional number는 거부하고, duplicate key는 **마지막** 값을 사용합니다 — Android 앱에서 실측한 Gson 2.8.2 동작에 의도적으로 맞춘 것입니다. 그 외 schema로 관리되는 모든 객체(Config/Device/IPC envelope)는 duplicate key를 엄격히 거부합니다.
- `mgr_sensor_ipc.schema.json` — IPC envelope JSON schema.
- `bt_spp.md`, `tcp_8140.md` — 아직 채워지지 않은 예약 placeholder입니다(`tcp_8140`은 MGR의 transport이며, manifest 일치를 위해 이 repo에도 동일하게 보존됩니다).
IPC transport: `AF_UNIX SOCK_SEQPACKET`, Sensor가 client(connect), MGR이 server입니다. 64 KiB 메시지 상한은 애플리케이션 계층에서 강제합니다. 연결 전 송신은 큐잉하지 않고 그대로 drop합니다(Android의 unbound service 동작과 동일합니다).
 
## 빌드 & 테스트
 
CMake preset 2개(`CMakePresets.json`):
 
```
cmake --preset host-mac    # macOS, Debug, mock IPC transport (Darwin은 AF_UNIX SOCK_SEQPACKET 미지원)
cmake --preset host-linux  # Linux, Debug, 실제 AF_UNIX SOCK_SEQPACKET transport
cmake --build --preset <preset>
ctest --preset <preset>
```
 
Feature 모듈은 `src/features/<name>/` 안에서 동일한 방식으로 개별 실행합니다.
 
```
cd src/features/mode_state
cmake -B build -DCMAKE_BUILD_TYPE=Release   # Release 빌드는 assert() 대신 항상 평가되는
cmake --build build                          # CHECK() 매크로로 테스트를 컴파일합니다
ctest --test-dir build --output-on-failure
```
 
참고: 이 저장소의 테스트 코드 일부는 `assert()` 대신 항상 평가되는 `CHECK(cond, msg)` 매크로(실패 시 abort)를 사용합니다. `assert()`가 `-DNDEBUG`(Release)에서 컴파일 제거되면서 `-Werror` 빌드 실패와, Release+테스트 빌드에서 side effect가 조용히 사라지는 문제가 있었기 때문입니다. Wave 1 통합 검증 과정에서 이 문제가 발견된 뒤 `config`, `mode_state`, `mgr_ipc` 세 모듈을 이 방식으로 전환했습니다.
 
Wave 1 검증은 모듈별로 Debug·Release(테스트 포함/미포함 양쪽)·ASan·UBSan을 Docker `ubuntu22.04-arm64`에서(67개 build/test 구성), 그리고 동일한 macOS 네이티브 패스(38개 구성)로 수행했습니다. TCP 8141은 Wave 1 통합 과정에서 발견하고 수정했던 fd-lifecycle race를 방지하기 위해 ASan을 20회 반복 실행(100/100 개별 테스트)했습니다. TSan은 현재 sandbox Docker 환경의 기본 seccomp profile에 막혀 사용할 수 없으며, PASS로 추정하지 않고 그대로 미사용으로 기록합니다.
 
## 비차단(non-blocking) 이슈
 
- **macOS 정적 라이브러리 중복 링크 경고**(`ld: warning: ignoring duplicate libraries`)가 stream 테스트 타겟 링크 시 발생합니다 — `savvy_core`/`savvy_protocol` archive는 하나뿐이며 직접 링크와 전이적(transitive) 링크가 겹쳐서 나오는 무해한 경고이고, merge를 막지 않습니다.
- **`src/features/stream/session.c`의 bounded 메모리 누수**: 아직 시작되지 않은 채 큐에 대기 중인 send가 있는 상태에서 `stop()`/`destroy()`가 호출되면 해당 `pending_send_t`가 해제되지 않습니다 — 시작되지 않은 채 drop된 send는 계약상 `on_complete` 콜백 자체를 받지 못하기 때문입니다. send queue depth(4)로 bounded되어 있으며, use-after-free도 무제한 누수도 아닙니다. 향후 fix 세션에서 처리할 항목으로 기록해 두었습니다.
## 이 문서의 범위 밖
 
- RV1106 cross-build / 보드 bring-up(별도 문서에서 관리).
- Wave 2 / Wave 2A(ToF adapter, baseline/PIR, RKNN frame pipeline, input aggregation) — 별도 feature 브랜치에서 구현이 이미 일부 시작됐지만, 이 문서가 다루는 Wave 1 상태에는 포함되지 않습니다.
