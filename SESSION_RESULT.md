# SESSION_RESULT

- SESSION_ID: `CC-FOUNDATION`
- Repository: `sensor_to_Linux`
- Branch: `foundation/contract-v1`
- Base SHA: `f476aea1ce2674e1a1a791154b2e830cbb87940b` (pinned, gate-verified before branch creation)
- Result SHA (this session's final commit): `c1eed05e07015437f785cc66d915d39a497a7a0e`
- Counterpart repo (`mgr_to_Linux`) base SHA: `9ae8b92d327487a3e0bdf0588449744d66b78c4e`
- Counterpart repo result SHA: `0d91db45ae7e80883af6a724ceec5de9fe26741a`
- Contract version: `1.0.0`
- Status: `IMPLEMENTATION_FINISHED` / `AWAITING_CODEX_GATE` (this session does not declare Foundation "PASS"/"COMPLETE" - see CC-FOUNDATION.md Exit Criteria B)

## Contract manifest comparison

`contracts/contract-manifest.sha256` (5 entries, covers `contracts/**` excluding itself) is **byte-for-byte identical** between `sensor_to_Linux` and `mgr_to_Linux` (verified via `diff`, exit 0):

```text
e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855  contracts/bt_spp.md
60bd04569266011a2a1c37fcee89154c7a5b76a2df87fd806e22be92e32c9874  contracts/ipc_action_catalog.md
f93bd483e606e0caca8d979d45b71a9b52c95614445c356f6c65f593102431b7  contracts/json_field_policy.md
913e373a93d71e243a4efdf95e6edd28b4f36cbcea75ca22d36cf43d85bebcb0  contracts/mgr_sensor_ipc.schema.json
e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855  contracts/tcp_8140.md
```

Generated deterministically via `cmake/generate_manifest.cmake` (uses CMake's own `file(SHA256 ...)`, not shell `shasum`/`sha256sum`, so macOS and Linux runs use the identical implementation). Verified reproducible: re-running the generator twice in a row produces byte-identical output.

## Dependency manifest comparison

`third_party/DEPENDENCY_MANIFEST.sha256` (cJSON v1.7.19 @ `c859b25da02955fef659d658b8f324b5cde87be3`, verified against GitHub's own commit API) is **byte-for-byte identical** between both repos:

```text
298581a04a36c0165da4b0aade235c23088cb2faa58651d720ea2f3706ed0b0d  third_party/cJSON/cJSON.c
25b0145150d500498e4d209cec69c18c42cf818bffcc54690be3b895a2a16dee  third_party/cJSON/cJSON.h
a36dda207c36db5818729c54e7ad4e8b0c6fba847491ba64f372c1a2037b6d5c  third_party/cJSON/LICENSE
```

## Implemented feature/test IDs

- **FND-01** Packet codec: 26-byte header (fixed offsets 0/1/2/3/4/8/22/26), Big-Endian length, CRC-32/ISO-HDLC (verified against the standard check value `CRC32("123456789")==0xCBF43926`, matching `java.util.zip.CRC32` semantics confirmed in Android source), strict 14B serial validation (no padding/truncation - explicit reject for empty/13B/15B/multibyte-non-14B), length-overflow guard, streaming parser scoped to TCP/BT only (not applied to MGR-Sensor IPC).
- **FND-02** JSON codec: `config_codec`/`device_codec`/`data_result_codec` in `src/protocol/json/`, isolated per DEC-20260714-01, using a generic field-descriptor-table helper (`field_table.c`) rather than hand-written per-field code across 53 total fields. `contracts/json_field_policy.md` fixes the full field matrix.
- **FND-03** IPC: `ipc_envelope_codec.c` (`{action, payload}` schema), `ipc_action_catalog.c` (confirmed Android action catalog lookup/validation), `src/platform/linux/ipc/ipc_client.c` (Sensor=client role, per DEC-20260714-02). `contracts/ipc_action_catalog.md` fixes the full action/payload-key/reconnect-replay matrix.
- **FND-04** Core interfaces: error codes, monotonic clock/timer, idempotent lifecycle, close/cancel queue, immutable-snapshot single-owner publish/acquire/release, platform adapter interfaces (`ipc_transport`, `serial_provider`) - scoped to the six items CC-FOUNDATION.md names, each tied to a named Wave 1 consumer (MGC-01~04, MGS-01/02, SNC-01~03, SNS-01/02).
- Third-party: cJSON v1.7.19 vendored per DEC-20260714-05.
- Tests, all passing (see below): `CT-PKT-001`, `CT-PKT-002`, `CT-PKT-003`, `CT-JSON-001`, `CT-JSON-002`, `CT-IPC-001`, `CT-IPC-002`, `CT-IPC-003`.

## Modified files

56 files changed, 6964 insertions(+), 0 deletions, across 7 commits on `foundation/contract-v1` (`e1adac6`..`c1eed05`). Full list (`git diff --stat f476aea..HEAD`, `.omc/` and `build/` excluded - both untracked and never staged):

```text
CMakeLists.txt
CMakePresets.json
cmake/generate_manifest.cmake
contracts/bt_spp.md
contracts/contract-manifest.sha256
contracts/ipc_action_catalog.md
contracts/json_field_policy.md
contracts/mgr_sensor_ipc.schema.json
contracts/tcp_8140.md
include/savvy/core/clock.h
include/savvy/core/error.h
include/savvy/core/lifecycle.h
include/savvy/core/queue.h
include/savvy/core/snapshot.h
include/savvy/platform/ipc_client.h
include/savvy/platform/ipc_transport.h
include/savvy/platform/serial_provider.h
include/savvy/protocol/config_codec.h
include/savvy/protocol/data_result_codec.h
include/savvy/protocol/device_codec.h
include/savvy/protocol/ipc_action_catalog.h
include/savvy/protocol/ipc_envelope.h
include/savvy/protocol/json_codec.h
include/savvy/protocol/packet_codec.h
include/savvy/protocol/stream_parser.h
src/core/CMakeLists.txt
src/core/clock.c
src/core/error.c
src/core/lifecycle.c
src/core/queue.c
src/core/snapshot.c
src/platform/linux/ipc/CMakeLists.txt
src/platform/linux/ipc/ipc_client.c
src/platform/linux/ipc/ipc_transport_common.c
src/platform/linux/ipc/ipc_transport_common.h
src/protocol/CMakeLists.txt
src/protocol/ipc/ipc_action_catalog.c
src/protocol/ipc/ipc_envelope_codec.c
src/protocol/json/config_codec.c
src/protocol/json/data_result_codec.c
src/protocol/json/device_codec.c
src/protocol/json/field_table.c
src/protocol/json/field_table.h
src/protocol/json/json_codec.c
src/protocol/packet/packet_codec.c
src/protocol/packet/stream_parser.c
tests/contract/CMakeLists.txt
tests/contract/test_ipc.c
tests/contract/test_json.c
tests/contract/test_packet.c
third_party/DEPENDENCY_MANIFEST.sha256
third_party/cJSON/CMakeLists.txt
third_party/cJSON/LICENSE
third_party/cJSON/UPSTREAM.md
third_party/cJSON/cJSON.c
third_party/cJSON/cJSON.h
```

`contracts/bt_spp.md` and `contracts/tcp_8140.md` are 0-byte backfills (see Blockers item 6) - this repo never received the original bootstrap skeleton `mgr_to_Linux` got. All paths fall within CC-FOUNDATION.md's "Allowed paths" list; nothing outside it (e.g. `AGENTS.md`, `CLAUDE.md`, `docs/**`, `src/app/main.c`-equivalents) was created.

## macOS host build/test commands and results

```bash
cmake --preset host-mac
cmake --build --preset host-mac
ctest --preset host-mac --output-on-failure
```

```text
100% tests passed, 0 tests failed out of 5
    CT-PKT-001 .......... Passed
    CT-PKT-002 .......... Passed
    CT-PKT-003 .......... Passed
    CT-JSON-001 ......... Passed
    CT-JSON-002 ......... Passed
```

This environment builds with `SAVVY_IPC_REAL_TRANSPORT=OFF` (the `host-mac` preset default) - `test_ipc` (CT-IPC-001~003) is not built here, since Darwin does not support `AF_UNIX SOCK_SEQPACKET`. Per CC-FOUNDATION.md, this environment's scope is packet/JSON/core, and PASS on this item is not required for the IPC layer.

macOS toolchain used: AppleClang 17.0.0.17000404, CMake 4.4.0 (installed via Homebrew for this session - was not present on the host beforehand).

## Docker Linux build/test commands and results

```bash
docker run --rm -v "$(pwd)":/work -w /work ubuntu:22.04 bash -c '
  apt-get update -qq && apt-get install -y -qq build-essential cmake
  cmake --preset host-linux
  cmake --build --preset host-linux
  ctest --preset host-linux --output-on-failure
'
```

```text
100% tests passed, 0 tests failed out of 8
    CT-PKT-001 .......... Passed
    CT-PKT-002 .......... Passed
    CT-PKT-003 .......... Passed
    CT-JSON-001 ......... Passed
    CT-JSON-002 ......... Passed
    CT-IPC-001 .......... Passed
    CT-IPC-002 .......... Passed
    CT-IPC-003 .......... Passed
```

Environment: Docker Desktop, `ubuntu:22.04` image (`linux/arm64`, matching `poc/ipc_seqpacket/result.md`'s environment), GCC 11.4.0, CMake 3.22.1 (apt default - `CMakePresets.json` schema version was lowered from 6 to 3 specifically so it works with this environment's actual CMake, rather than requiring a newer one). This is the environment that exercises the real `AF_UNIX SOCK_SEQPACKET` transport (`SAVVY_IPC_REAL_TRANSPORT=ON`, the `host-linux` preset default).

One portability bug was found and fixed by this repo's own build/test cycle: a missing direct `#include <stdint.h>` in `tests/contract/test_ipc.c` (this repo's `ipc_client.h` never needed `stdint` types itself, so - unlike `mgr_to_Linux`'s `ipc_server.h`, which needs `uint32_t` for a timeout parameter and so pulls it in transitively - the header here doesn't transitively supply it). The fd double-ownership bug found in `mgr_to_Linux`'s `ipc_server.c` does not apply to this repo's `ipc_client.c`, which holds no equivalent per-connection state across calls.

## Android source evidence / behavior notes

Both `savvy_mgr` (@ `ad83cabebe7643e9eec5c0e75c1c797af30d357a`) and `savvy_sensor` (@ `48e2d1442cd867cc60f8ff3186d53fce1c08f308`) were researched read-only (no modification, confirmed via `git status` before and after).

**Architecture correction discovered during this session**: the 26-byte packet codec is not used for MGR-Sensor IPC at all. On the Sensor side specifically, it carries only Sensor↔remote-server traffic (TCP 8141, Stream/Voice). MGR-Sensor IPC is pure Android Messenger/Bundle IPC (string action name + string-typed key/value payload) - no 26-byte header, no CRC. This does not change FND-01/FND-03's deliverables (both were already scoped correctly per CC-FOUNDATION.md's own envelope schema example), but corrects the mental model documented in `contracts/json_field_policy.md` §0 and `contracts/ipc_action_catalog.md` §0.

**Confirmed exactly as hypothesized**: Sensor's 8141 response inbound *does* verify CRC on receipt - though a CRC mismatch there is swallowed silently in Android (logged only as "Data CRC Check Error.", no NAK surfaced, `MSG_CMD.REASON_NAK_CRC_ERROR` is declared but never assigned/returned anywhere in this repo, confirmed by exhaustive grep). This is Android-side dead code/quirk, not something this session implements or fixes, since channel-policy enforcement is Wave 1's domain (CC-SENSOR-STREAM), not FND-01's.

**Also discovered**: `TransferBitConverter.getBytesAsLen()` returns a 0-length array (not a padded/truncated 14-byte one) when the input string exceeds 14 characters - a real packet-corruption risk in Android if `deviceSerial` (or a `BeaconName`) ever exceeds 14 chars, since nothing validates that length beforehand. FND-01's `savvy_packet_encode()` explicitly rejects any serial whose length isn't exactly 14 bytes (`SAVVY_ERR_INVALID_ARGUMENT`) rather than reproducing this Android bug, per S-002's safety decision ("Java의 비정상 malformed/null packet 생성 동작은 재현하지 않는다").

**Behavior differences from Android** (all deliberate and documented, not silent):
1. **MGR-Sensor IPC envelope payload shape**: this repo nests `payload` as a JSON *object*; real Android Bundles can only hold Strings, so the real wire value is a *string* containing serialized JSON (double-encoding). Justified by DEC-20260714-02's own text: this IPC's "External compatibility impact: 없음" (none). See `contracts/json_field_policy.md` §0.
2. **`DataResult` missing `"result"` key is a parse error**, not defaulted to `4`. Android's `DataResult` class has no no-arg constructor (only `DataResult(int)`), so Gson (2.8.2, confirmed via `app/build.gradle`) must use unsafe/no-constructor allocation to deserialize it, which bypasses the `= 4` field initializer - meaning a real Android `DataResult` missing that key could plausibly parse to `0` (danger) rather than `4` (normal). This was not independently re-verified by executing real Gson bytecode in this session. CT-JSON-002 already specifies "missing → parse error" as required Foundation behavior, so implementing exactly that is not a new judgment call - it happens to also be the safer of the two candidate real behaviors.

**Discovered drift (recorded, not resolved)**: `savvy_mgr`'s and `savvy_sensor`'s independently-duplicated `JsonConfigDto.keepServerIp` field defaults to different values (`15.165.113.212` vs `13.125.173.114`). `savvy_config_set_defaults()` in this codebase uses the MGR-side value (matching `mgr_to_Linux`, for consistency across both repos' Foundation code). This does not decide which is "correct" for production - see Blockers below.

Full citation detail (file/class/method for every claim above) is in `contracts/json_field_policy.md` and `contracts/ipc_action_catalog.md`.

## Blockers and incomplete items

1. **`keepServerIp` default mismatch** between `savvy_mgr` and `savvy_sensor` - not resolved by this session (see `contracts/json_field_policy.md` §5). Needs a `SCOPE_CHANGE_REQUEST` owner decision on which value (if either) is production-correct.
2. **`DataResult` Gson-unsafe-allocation risk** - flagged, not independently verified by executing real Android/Gson bytecode. Recommend empirical device/emulator confirmation before treating "missing result key never happens in practice" as settled.
3. **RV1106 cross-build**: `PENDING` - blocked on B-005 (SDK/toolchain, still `OPEN`). Out of this session's scope (macOS host + Docker Linux native build only, per CC-FOUNDATION.md's own build/test scope).
4. **`SO_PEERCRED` / socket peer authentication / production socket path** (`/run/savvy/mgr-sensor.sock`): explicitly `DEFERRED_PRODUCTION_SECURITY` per DEC-20260714-02 - not a Foundation completion condition, not implemented here. Dev/test path (`/tmp/...`) is injected via parameter in this codebase, never hardcoded in production code paths.
5. **`getBytesAsLen()` serial-overflow bug** (see above) - Android's behavior corrupts the outbound packet if triggered; FND-01 deliberately does not reproduce it (rejects instead, per S-002). Flagged for Wave 1 awareness (whoever owns the actual serial-source provider, e.g. CC-MGR-CORE MGC-02 / B-010), not fixed here since FND-01 never implements a concrete serial provider.
6. **This repo never received the original 0-byte bootstrap skeleton** that `mgr_to_Linux` got (only `README.md` was tracked before this session). Backfilled `contracts/bt_spp.md` and `contracts/tcp_8140.md` as empty files (within Allowed paths) so the `contracts/` tree matches `mgr_to_Linux`; did not create `AGENTS.md`/`CLAUDE.md`/`docs/**`/`src/app/main.c` equivalents since those paths are outside Allowed paths in both repos.
7. No `CONTRACT_CHANGE_REQUEST` was filed - see "Contract change" below.

## Contract change

None. The wire contract given in CC-FOUNDATION.md (26-byte packet layout, CRC policy table, serial policy, JSON field policy skeleton, IPC envelope schema) was not modified. Two new contract *files* were authored (`contracts/json_field_policy.md`'s field matrix, `contracts/ipc_action_catalog.md`) as the explicit, required output of FND-02 Objective 3 and FND-03 Objective 4 respectively - these fill in detail the document deferred to Android-source research, they do not change anything CC-FOUNDATION.md itself specified.

## Scope assertions

- [x] Allowed paths only (verified: full modified-file list above, cross-checked against CC-FOUNDATION.md "Allowed paths").
- [x] No new product feature (FND-01~04 objectives only; no Wave 1 feature work; `src/features/**` untouched).
- [x] No production mock/dummy (test-only raw-socket counterparts and mock transports live exclusively under `tests/contract/`, never under `src/`).
- [x] No hardware QA claim (no board/hardware interaction; RV1106 cross-build/board runtime explicitly `NOT_PERFORMED`).
- [x] ToF/RKNN engine internals not implemented (not touched; RKNN-related actions are cataloged as IPC actions only, per FND-03's action-catalog objective, with no engine logic).

## Next dependency / handoff

- Codex V0 (Foundation) independent verification (Exit Criteria B).
- User review and Gate approval; `contract-v1` tag creation happens only after that (this session does not create it).
- User/Cowork decision on the `keepServerIp` drift (item 1 above).
- Optional: empirical verification of `DataResult`'s actual Gson deserialization behavior on a real device/emulator (item 2 above).
- B-005 (RV1106 SDK/toolchain) resolution, whenever cross-build work is scheduled - separate from this session.
- Wave 1 sessions (`CC-SENSOR-CORE`, `CC-SENSOR-STREAM`, and `CC-MGR-CORE`/`CC-MGR-SERVER` in the counterpart repo) can now consume the FND-01~04 headers/codecs committed here.
