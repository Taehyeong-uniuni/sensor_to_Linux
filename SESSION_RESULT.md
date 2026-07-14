# SESSION_RESULT

- SESSION_ID: `CC-FOUNDATION` (this document covers the initial Foundation implementation and one subsequent Codex-review fix round, both on the same branch)
- Repository: `sensor_to_Linux`
- Branch: `foundation/contract-v1`
- Base SHA: `f476aea1ce2674e1a1a791154b2e830cbb87940b` (pinned, gate-verified before branch creation)
- **Implementation SHA (last code/contract/test commit, what was actually built and tested)**: `cde953b9e91b940865dd62cc4e2ba88e134f1739`
- **Report SHA**: not embedded here - this document's own commit necessarily lands *after* the Implementation SHA above, so a SHA claiming to be "this file's own commit" cannot appear inside that same commit's content (the previous SESSION_RESULT.md tried to do this anyway; Codex V0A F-12 correctly flagged the resulting mismatch - see "F-12 resolution" below). Run `git rev-parse HEAD` on `foundation/contract-v1` for the true current HEAD; it will be exactly one commit past the Implementation SHA above, and that one commit touches only this file.
- Counterpart repo (`mgr_to_Linux`) base SHA: `9ae8b92d327487a3e0bdf0588449744d66b78c4e`
- Counterpart repo Implementation SHA: `5fe5f9ec90a214416292e209968489f80584d19a`
- Contract version: `1.0.0`
- Status: `IMPLEMENTATION_FINISHED` / `AWAITING_CODEX_GATE` (this session does not declare Foundation "PASS"/"COMPLETE" - see CC-FOUNDATION.md Exit Criteria B)

## ⚠ Android baseline integrity - URGENT, not fixed, read this first

Both read-only Android reference repos currently have **uncommitted working-tree
changes**, independently re-confirmed via `git status`/`git diff`/`stat` immediately
before this commit (originally flagged by Codex V0A F-01/F-11 against an earlier
snapshot; still true now):

- **`savvy_mgr`** (pinned HEAD `ad83cabebe7643e9eec5c0e75c1c797af30d357a`, HEAD
  itself unchanged and still exactly the pinned commit):
  - `app/src/main/cpp/native-lib.cpp` line 14's JNI export symbol is corrupted in
    the *working tree* (uncommitted): the committed
    `Java_com_uniuni_savvymgr_IfComm_UdsSocketServer_createBoundFileDescriptor`
    has become `eJava_com_uniuni_savvymgr_IfComm_UdsSocketServer_createBoundFilDescriptor`
    on disk.
  - `README.md` is deleted (uncommitted, working tree only).
  - Untracked: `.omc/`, `READMEmgr.md`, and stray `.omc/` copies under
    `app/src/main/.omc/` and `app/src/main/java/com/uniuni/savvymgr/.omc/`.
- **`savvy_sensor`** (pinned HEAD `48e2d1442cd867cc60f8ff3186d53fce1c08f308`, HEAD
  itself unchanged and still exactly the pinned commit):
  - `README.md` is deleted (uncommitted, working tree only).
  - `.DS_Store` is modified (uncommitted).
  - Untracked: `.omc/`, `README_Sensor.md`, `SOURCE_FLOW_VERIFICATION.md`.

**Timing evidence** (not proof of causation, but the closest correlation available):
`savvy_mgr/native-lib.cpp`'s mtime (2026-07-14 17:40:16) and
`savvy_sensor/.DS_Store`'s mtime (2026-07-14 17:39:39) both fall within the same
one-minute window as this session's own prior commit (`b5204e8` in this repo /
`191647c` in the counterpart, both at 17:39:50) - suggestive that this happened
during this session's own earlier (Phase-4) activity, though no specific tool call
could be identified after the fact as the cause. By contrast, `READMEmgr.md` and
`README_Sensor.md` are both dated July 3 (11 days before this session), 3 minutes
apart from each other - clearly a separate, pre-existing, unrelated event. The
stray `.omc/` directories carry this same session's ID and match a known,
already-observed side effect of this environment's own tooling (identical stray
`.omc/` directories were found and removed from `mgr_to_Linux`/`sensor_to_Linux`
themselves earlier in this session) - their presence indicates the tooling ran
with a cwd inside these repos at some point, but is not itself evidence of
deliberate content editing.

**Nothing is unrecoverable**: both repos' HEAD commits are exactly the pinned
SHAs; the corruption is uncommitted working-tree drift only. `git checkout --
README.md app/src/main/cpp/native-lib.cpp` (in `savvy_mgr`) and `git checkout --
README.md` (in `savvy_sensor`) would restore the pinned committed content
exactly. **This session has not run any such command and will not without
explicit direction** - these repos are strictly read-only per CC-FOUNDATION.md,
and restoring them (even to "undo" apparent damage) is a decision for whoever
owns baseline integrity, not something to do unilaterally after the fact.

No FND work in this round or the prior one relied on the corrupted line or the
deleted files - all Android-source citations in this document and in
`contracts/*.md` were read and recorded before this drift was noticed, and cite
the *committed*, pinned-SHA content. This round's deliverable is not affected in
substance, but the baseline's integrity for any future research/comparison is
compromised until a separately-approved recovery restores it.

## F-12 resolution (SESSION_RESULT SHA/stat mismatch)

Codex V0A F-12: this document's previous revision claimed Result SHA `c1eed05`
(7 commits / 56 files / 6964 insertions), but the actual reviewed HEAD was
`b5204e8` (8 commits / 57 files / 7170 insertions) - the prior SESSION_RESULT.md
commit itself wasn't counted, because its own stats were computed *before* that
commit existed. Resolution applied here: this document now names an explicit
**Implementation SHA** (the last commit that changes code/contracts/tests) and
does not attempt to self-embed a "Report SHA" for its own commit (see header
block above for why that specific number can't be made self-consistent). All
stats below are `git rev-parse`/`git diff --stat`/`git log --oneline` output
captured against the Implementation SHA, not hand-typed.

## Contract manifest comparison

`contracts/contract-manifest.sha256` (5 entries, covers `contracts/**` excluding
itself) is **byte-for-byte identical** between `sensor_to_Linux` and
`mgr_to_Linux` (verified via `diff`, exit 0). Regenerated this round because
`json_field_policy.md`'s content changed (see "Codex review round" below):

```text
e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855  contracts/bt_spp.md
60bd04569266011a2a1c37fcee89154c7a5b76a2df87fd806e22be92e32c9874  contracts/ipc_action_catalog.md
d79c476354a70a89018fde64c938953c6056a26eb67da4b7ef19dcc7f99b2f9f  contracts/json_field_policy.md
913e373a93d71e243a4efdf95e6edd28b4f36cbcea75ca22d36cf43d85bebcb0  contracts/mgr_sensor_ipc.schema.json
e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855  contracts/tcp_8140.md
```

Generated deterministically via `cmake/generate_manifest.cmake` (uses CMake's own
`file(SHA256 ...)`, not shell `shasum`/`sha256sum`, so macOS and Linux runs use
the identical implementation). Verified reproducible: re-running the generator
twice in a row produces byte-identical output.

## Dependency manifest comparison

`third_party/DEPENDENCY_MANIFEST.sha256` (cJSON v1.7.19 @
`c859b25da02955fef659d658b8f324b5cde87be3`, verified against GitHub's own commit
API) is **byte-for-byte identical** between both repos and **unchanged this
round** (cJSON was not touched):

```text
298581a04a36c0165da4b0aade235c23088cb2faa58651d720ea2f3706ed0b0d  third_party/cJSON/cJSON.c
25b0145150d500498e4d209cec69c18c42cf818bffcc54690be3b895a2a16dee  third_party/cJSON/cJSON.h
a36dda207c36db5818729c54e7ad4e8b0c6fba847491ba64f372c1a2037b6d5c  third_party/cJSON/LICENSE
```

## Codex review round (V0A + V0B) - findings addressed

Two independent Codex reviews were run against the initial Foundation
implementation: `CODEX_V0A_CONTRACT_REVIEW.md` (13 findings, F-01~F-13, final
verdict **FAIL**) and `CODEX_V0B_STABILITY_REVIEW.md` (0 Critical / 3 High /
8 Medium / 3 Low = 14 findings, V0B-H-01~03/M-01~08/L-01~03). Combined
disposition:

- **Fixed in this round** (code + tests, verified by the build/test results
  below): F-02/L-02 (packet length overflow), F-03 (keepServerIp - see note
  below), F-04/M-06 (JSON number-to-int32 validation), F-05 (IPC payload type
  validation), F-06/M-05 (64KiB receive cap enforced independent of caller
  capacity), F-07/F-08 (test-quality: CT-PKT-001 boundary vectors, CT-IPC-001
  all 23 actions, CT-IPC-002 real reconnect-replay set, CT-IPC-003 real
  truncated-UTF-8 + transport-level cap boundary), F-10 (UTF-8 validation),
  F-13 (unknown-key logging callback), V0B-H-01 (SIGPIPE via MSG_NOSIGNAL),
  V0B-H-02 (poll-based deadlines on connect/send/recv - this repo's
  `savvy_ipc_client_connect` now uses non-blocking `connect()` +
  `poll(POLLOUT)` + `getsockopt(SO_ERROR)` so a dead/unreachable MGR peer
  can't hang the caller), V0B-H-03 (queue allocation overflow check),
  V0B-M-01 (fd invalidated after close), V0B-M-02/M-03 (queue try-push + item
  destructor), V0B-M-04 (snapshot finalizer moved outside the lock; OOM in
  publish() no longer double-frees or transfers ownership), V0B-M-08
  (SOCK_CLOEXEC), L-01/V0B-L-01 (explicit-length parse APIs for
  config/device/data-result codecs), V0B-L-03 (checked
  lifecycle/snapshot-owner init return values).
- **Not applicable to this repo's role**: V0B-M-07 (accept timeout/revents -
  `accept()` only exists in MGR's server role; this repo has no equivalent
  code path).
- **Correctly out of scope, not fixed** (both concern the read-only Android
  baselines, not this repo's own code): F-01, F-11 - see the dedicated section
  above.
- **Documentation-only, addressed**: F-12 - see "F-12 resolution" above.
- **Not a code defect, addressed as a build fix**: while re-verifying all of the
  above on Docker Linux (the environment that exercises the real transport
  layer for the first time since this round's rewrite), the build itself failed
  independent of any Codex finding: `undefined reference to 'floor'` - glibc
  requires `libm` to be linked explicitly for `floor()`/`isfinite()`
  (`json_codec.c`'s number validation, added for F-04/M-06), which macOS's
  libSystem provides implicitly. Fixed by linking `m` on UNIX targets in
  `src/protocol/CMakeLists.txt`.

## Implemented feature/test IDs

- **FND-01** Packet codec: 26-byte header (fixed offsets 0/1/2/3/4/8/22/26), Big-Endian length, CRC-32/ISO-HDLC (verified against the standard check value `CRC32("123456789")==0xCBF43926`, matching `java.util.zip.CRC32` semantics confirmed in Android source), strict 14B serial validation (no padding/truncation - explicit reject for empty/13B/15B/multibyte-non-14B), length-overflow guard (hardened further this round, F-02/L-02), streaming parser scoped to TCP/BT only (not applied to MGR-Sensor IPC).
- **FND-02** JSON codec: `config_codec`/`device_codec`/`data_result_codec` in `src/protocol/json/`, isolated per DEC-20260714-01, using a generic field-descriptor-table helper (`field_table.c`) rather than hand-written per-field code across 53 total fields. `contracts/json_field_policy.md` fixes the full field matrix. This round added number-range/UTF-8/unknown-key-logging validation and explicit-length parse signatures (F-04/F-10/F-13/L-01).
- **FND-03** IPC: `ipc_envelope_codec.c` (`{action, payload}` schema), `ipc_action_catalog.c` (confirmed Android action catalog lookup/validation, now with per-key JSON type checking - F-05), `src/platform/linux/ipc/ipc_client.c` (Sensor=client role, per DEC-20260714-02), now with poll-based non-blocking connect, MSG_NOSIGNAL, SOCK_CLOEXEC, and a single-owner fd-close discipline (V0B-H-01/H-02/M-01/M-08). `contracts/ipc_action_catalog.md` fixes the full action/payload-key/reconnect-replay matrix.
- **FND-04** Core interfaces: error codes, monotonic clock/timer, idempotent lifecycle, close/cancel queue, immutable-snapshot single-owner publish/acquire/release, platform adapter interfaces (`ipc_transport`, `serial_provider`) - scoped to the six items CC-FOUNDATION.md names, each tied to a named Wave 1 consumer (MGC-01~04, MGS-01/02, SNC-01~03, SNS-01/02). This round hardened queue allocation-overflow checking, added try-push and an item destructor, split snapshot's finalizer out from under its lock, and made init functions return a checked status (V0B-H-03/M-02/M-03/M-04/L-03).
- Third-party: cJSON v1.7.19 vendored per DEC-20260714-05, unchanged this round.
- Tests, all passing on both platforms (see below): `CT-PKT-001`, `CT-PKT-002`, `CT-PKT-003`, `CT-JSON-001`, `CT-JSON-002`, `CT-IPC-001`, `CT-IPC-002`, `CT-IPC-003`.

## Modified files

**Full session total** (base `f476aea1`..Implementation SHA `cde953b`): 57 files
changed, 7966 insertions(+), 0 deletions(-), across 9 commits (`git diff --stat`/
`git log --oneline`, run directly, not hand-typed):

```text
e1adac6 foundation: FND-01 packet codec, FND-04 core interfaces, cJSON vendor, CMake scaffolding
f490742 foundation: IPC envelope codec + FND-03 Sensor client transport
050b0cb foundation: CT-PKT-001~003 contract tests
bb51390 foundation: deterministic contract/dependency manifest generation
0639a2a foundation: CT-IPC-002~003 transport contract tests (Sensor client role)
9bac58a fix: real Docker Linux build/test failures found by CT-IPC-002/003
c1eed05 foundation: FND-02 typed JSON codecs + FND-03 action catalog (CT-JSON-001~002, CT-IPC-001)
b5204e8 foundation: SESSION_RESULT.md
cde953b fix: address Codex V0A/V0B foundation review findings
```

**This round only** (prior report commit `b5204e8`..Implementation SHA
`cde953b`): 31 files changed, 1071 insertions(+), 275 deletions(-), 1 commit:

```text
contracts/contract-manifest.sha256
contracts/json_field_policy.md
include/savvy/core/lifecycle.h
include/savvy/core/queue.h
include/savvy/core/snapshot.h
include/savvy/platform/ipc_client.h
include/savvy/platform/ipc_transport.h
include/savvy/protocol/config_codec.h
include/savvy/protocol/data_result_codec.h
include/savvy/protocol/device_codec.h
include/savvy/protocol/ipc_action_catalog.h
include/savvy/protocol/json_codec.h
src/core/clock.c
src/core/lifecycle.c
src/core/queue.c
src/core/snapshot.c
src/platform/linux/ipc/ipc_client.c
src/platform/linux/ipc/ipc_transport_common.c
src/platform/linux/ipc/ipc_transport_common.h
src/protocol/CMakeLists.txt
src/protocol/ipc/ipc_action_catalog.c
src/protocol/json/config_codec.c
src/protocol/json/data_result_codec.c
src/protocol/json/device_codec.c
src/protocol/json/field_table.c
src/protocol/json/field_table.h
src/protocol/json/json_codec.c
src/protocol/packet/packet_codec.c
tests/contract/test_ipc.c
tests/contract/test_json.c
tests/contract/test_packet.c
```
(plus this file's own subsequent report commit, not included in the count above - see header block)

`contracts/bt_spp.md` and `contracts/tcp_8140.md` remain the 0-byte backfills from Phase 4 (see Blockers item 6) - this repo never received the original bootstrap skeleton `mgr_to_Linux` got. All paths fall within CC-FOUNDATION.md's "Allowed paths" list; nothing outside it was created or touched. Local `build/` directories (regenerated by CMake, never staged) were removed after final verification (Codex F-09).

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

This environment builds with `SAVVY_IPC_REAL_TRANSPORT=OFF` (the `host-mac` preset default) - `test_ipc` (CT-IPC-001~003) is not built here, since Darwin does not support `AF_UNIX SOCK_SEQPACKET`. Per CC-FOUNDATION.md, this environment's scope is packet/JSON/core, and PASS on this item is not required for the IPC layer. Re-run from a clean `build/host-mac` against the Implementation SHA above; CT-JSON-001/002 now additionally exercise this round's fractional-number, UTF-8, and unknown-key-logging fixes.

macOS toolchain used: AppleClang 17.0.0.17000404, CMake 4.4.0.

## Docker Linux build/test commands and results

```bash
docker run --rm -v "$(pwd)":/work -w /work --platform linux/arm64 ubuntu:22.04 bash -c '
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

Environment: Docker Desktop, `ubuntu:22.04` image (`linux/arm64`), GCC 11.4.0, CMake 3.22.1 (apt default). This is the environment that exercises the real `AF_UNIX SOCK_SEQPACKET` transport (`SAVVY_IPC_REAL_TRANSPORT=ON`, the `host-linux` preset default), and - for this round - the first real compile+run of the entire rewritten transport layer (poll-based non-blocking connect, MSG_NOSIGNAL, SOCK_CLOEXEC, fd-invalidate-on-close), none of which could be fully syntax-checked on macOS (`SOCK_CLOEXEC` is a Linux-only identifier).

This round's build/test cycle found and fixed one real, previously-undetected bug: `undefined reference to 'floor'` at link time (see "Codex review round" above) - `json_codec.c`'s new number-validation code (F-04/M-06) calls `floor()`/`isfinite()`, and glibc requires `libm` linked explicitly for these, unlike macOS's libSystem. Fixed in `src/protocol/CMakeLists.txt`.

(Phase-4's original Docker Linux cycle had separately found and fixed one earlier bug specific to this repo - a missing direct `#include <stdint.h>` in `tests/contract/test_ipc.c` - already recorded in this document's prior revision and still in effect; not repeated in full here. The fd double-ownership bug found in `mgr_to_Linux`'s `ipc_server.c` never applied to this repo's `ipc_client.c`, which holds no equivalent per-connection state across calls.)

## Android source evidence / behavior notes

Both `savvy_mgr` (@ `ad83cabebe7643e9eec5c0e75c1c797af30d357a`) and `savvy_sensor` (@ `48e2d1442cd867cc60f8ff3186d53fce1c08f308`) were researched read-only in Phase 4. **This round did no new Android-source research** - see the dedicated integrity section above for the working-tree drift discovered while re-verifying these pinned SHAs this round.

**Architecture correction discovered in Phase 4**: the 26-byte packet codec is not used for MGR-Sensor IPC at all. On the Sensor side specifically, it carries only Sensor↔remote-server traffic (TCP 8141, Stream/Voice). MGR-Sensor IPC is pure Android Messenger/Bundle IPC (string action name + string-typed key/value payload) - no 26-byte header, no CRC. This does not change FND-01/FND-03's deliverables, but corrects the mental model documented in `contracts/json_field_policy.md` §0 and `contracts/ipc_action_catalog.md` §0.

**Confirmed exactly as hypothesized** (Phase 4): Sensor's 8141 response inbound *does* verify CRC on receipt - though a CRC mismatch there is swallowed silently in Android (logged only as "Data CRC Check Error.", no NAK surfaced, `MSG_CMD.REASON_NAK_CRC_ERROR` is declared but never assigned/returned anywhere in this repo, confirmed by exhaustive grep). This is Android-side dead code/quirk, not something this session implements or fixes, since channel-policy enforcement is Wave 1's domain (CC-SENSOR-STREAM), not FND-01's.

**Also discovered** (Phase 4): `TransferBitConverter.getBytesAsLen()` returns a 0-length array (not a padded/truncated 14-byte one) when the input string exceeds 14 characters - a real packet-corruption risk in Android if `deviceSerial` (or a `BeaconName`) ever exceeds 14 chars, since nothing validates that length beforehand. FND-01's `savvy_packet_encode()` explicitly rejects any serial whose length isn't exactly 14 bytes (`SAVVY_ERR_INVALID_ARGUMENT`) rather than reproducing this Android bug, per S-002's safety decision ("Java의 비정상 malformed/null packet 생성 동작은 재현하지 않는다").

**Behavior differences from Android** (all deliberate and documented, not silent):
1. **MGR-Sensor IPC envelope payload shape**: this repo nests `payload` as a JSON *object*; real Android Bundles can only hold Strings, so the real wire value is a *string* containing serialized JSON (double-encoding). Justified by DEC-20260714-02's own text: this IPC's "External compatibility impact: 없음" (none). See `contracts/json_field_policy.md` §0.
2. **`DataResult` missing `"result"` key is a parse error**, not defaulted to `4`. Android's `DataResult` class has no no-arg constructor (only `DataResult(int)`), so Gson (2.8.2, confirmed via `app/build.gradle`) must use unsafe/no-constructor allocation to deserialize it, which bypasses the `= 4` field initializer - meaning a real Android `DataResult` missing that key could plausibly parse to `0` (danger) rather than `4` (normal). This was not independently re-verified by executing real Gson bytecode in this session. CT-JSON-002 already specifies "missing → parse error" as required Foundation behavior; this round additionally added an explicit `{"result":4.9}` regression test for the related fractional-number case (F-04/M-06).

**`keepServerIp` drift - corrected description this round (F-03)**: `savvy_mgr`'s and `savvy_sensor`'s independently-duplicated `JsonConfigDto.keepServerIp` field defaults to different values (`15.165.113.212` vs `13.125.173.114`). **This document's prior revision incorrectly stated that `savvy_config_set_defaults()` uses "the MGR-side value" as a single shared Foundation default in both repos - that was never accurate for this repo's actual code and has been corrected.** This repo's own `savvy_config_set_defaults()` has always returned `13.125.173.114` (Sensor's own compiled default); the counterpart `mgr_to_Linux`'s returns `15.165.113.212` (its own app's compiled default) - each repo matches its own Android source, which is what `contracts/json_field_policy.md` §5 was actually supposed to say (now fixed there too). This does not decide which value (if either) is production-correct - see Blockers below.

Full citation detail (file/class/method for every claim above) is in `contracts/json_field_policy.md` and `contracts/ipc_action_catalog.md`.

## Blockers and incomplete items

1. **Android baseline integrity (URGENT)** - see the dedicated section above. Not resolved by this session; needs a separately-approved recovery/audit action, not a unilateral fix.
2. **`keepServerIp` default mismatch** between `savvy_mgr` and `savvy_sensor` - not resolved by this session (see `contracts/json_field_policy.md` §5). Needs a `SCOPE_CHANGE_REQUEST` owner decision on which value (if either) is production-correct; this round only corrected the documentation/implementation to consistently reflect each app's own real default (F-03), it did not reconcile the two.
3. **`DataResult` Gson-unsafe-allocation risk** - flagged, not independently verified by executing real Android/Gson bytecode. Recommend empirical device/emulator confirmation before treating "missing result key never happens in practice" as settled.
4. **RV1106 cross-build**: `PENDING` - blocked on B-005 (SDK/toolchain, still `OPEN`). Out of this session's scope (macOS host + Docker Linux native build only, per CC-FOUNDATION.md's own build/test scope).
5. **`SO_PEERCRED` / socket peer authentication / production socket path** (`/run/savvy/mgr-sensor.sock`): explicitly `DEFERRED_PRODUCTION_SECURITY` per DEC-20260714-02 - not a Foundation completion condition, not implemented here. Dev/test path (`/tmp/...`) is injected via parameter in this codebase, never hardcoded in production code paths.
6. **`getBytesAsLen()` serial-overflow bug** (see above) - Android's behavior corrupts the outbound packet if triggered; FND-01 deliberately does not reproduce it (rejects instead, per S-002). Flagged for Wave 1 awareness (whoever owns the actual serial-source provider, e.g. CC-MGR-CORE MGC-02 / B-010), not fixed here since FND-01 never implements a concrete serial provider.
7. **This repo never received the original 0-byte bootstrap skeleton** that `mgr_to_Linux` got (only `README.md` was tracked before this session). Backfilled `contracts/bt_spp.md` and `contracts/tcp_8140.md` as empty files (within Allowed paths) so the `contracts/` tree matches `mgr_to_Linux`; did not create `AGENTS.md`/`CLAUDE.md`/`docs/**`/`src/app/main.c` equivalents since those paths are outside Allowed paths in both repos.
8. No `CONTRACT_CHANGE_REQUEST` was filed - see "Contract change" below.

## Contract change

None. The wire contract given in CC-FOUNDATION.md (26-byte packet layout, CRC policy table, serial policy, JSON field policy skeleton, IPC envelope schema) was not modified, this round or the prior one. `contracts/json_field_policy.md` was corrected this round (§5's `keepServerIp` description, see above), which is a documentation fix, not a contract change - the field's actual default *value* per repo did not change.

## Scope assertions

- [x] Allowed paths only (verified: full modified-file list above, cross-checked against CC-FOUNDATION.md "Allowed paths").
- [x] No new product feature (FND-01~04 objectives only; no Wave 1 feature work; `src/features/**` untouched).
- [x] No production mock/dummy (test-only raw-socket counterparts and mock transports live exclusively under `tests/contract/`, never under `src/`).
- [x] No hardware QA claim (no board/hardware interaction; RV1106 cross-build/board runtime explicitly `NOT_PERFORMED`).
- [x] ToF/RKNN engine internals not implemented (not touched; RKNN-related actions are cataloged as IPC actions only, per FND-03's action-catalog objective, with no engine logic).
- [x] Android MGR/Sensor repos not modified *by this session's git-tracked commits* (both HEADs remain exactly the pinned SHAs) - see the dedicated integrity section above for the working-tree caveat.
- [x] `contract-v1` tag not created (this session never creates it).
- [x] No merges performed.

## Next dependency / handoff

- **Android baseline integrity recovery** (see dedicated section above) - recommend prioritizing this ahead of the items below, since it affects the reliability of any future Android-source citation.
- Codex V0 (Foundation) independent re-verification of this fix round (Exit Criteria B).
- User review and Gate approval; `contract-v1` tag creation happens only after that (this session does not create it).
- User/Cowork decision on the `keepServerIp` drift (item 2 above).
- Optional: empirical verification of `DataResult`'s actual Gson deserialization behavior on a real device/emulator (item 3 above).
- B-005 (RV1106 SDK/toolchain) resolution, whenever cross-build work is scheduled - separate from this session.
- Wave 1 sessions (`CC-SENSOR-CORE`, `CC-SENSOR-STREAM`, and `CC-MGR-CORE`/`CC-MGR-SERVER` in the counterpart repo) can now consume the FND-01~04 headers/codecs committed here.
