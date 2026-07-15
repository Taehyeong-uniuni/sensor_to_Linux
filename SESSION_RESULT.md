# SESSION_RESULT

- SESSION_ID: `CC-FOUNDATION` (this document covers the initial Foundation implementation and five subsequent Codex fix rounds, all on the same branch)
- Repository: `sensor_to_Linux`
- Branch: `foundation/contract-v1`
- Base SHA: `f476aea1ce2674e1a1a791154b2e830cbb87940b` (pinned, gate-verified before branch creation)
- **Pre-this-round HEAD**: `4c1f144e8cb8979fc4813ae5e1845d3c4c230935` (final HEAD of the prior TC-H-01/TC-M-01/TC-L-01/TC-L-02 round)
- **Implementation SHA (this round - DataResult Gson Parity + FG-M-01 - last code/test commit, what was actually built and tested)**: `aca143a7f4b76dc8cb6fff324ca21ea9f557622a`
- **Report SHA**: not embedded here - this document's own commit necessarily lands *after* the Implementation SHA above, so a SHA claiming to be "this file's own commit" cannot appear inside that same commit's content (Codex V0A F-12; see "F-12 resolution" below for the full explanation). Run `git rev-parse HEAD` on `foundation/contract-v1` for the true current HEAD; it will be exactly one commit past the Implementation SHA above, and that one commit touches only this file (this round's report-only changes).
- Counterpart repo (`mgr_to_Linux`) base SHA: `9ae8b92d327487a3e0bdf0588449744d66b78c4e`
- Counterpart repo pre-this-round HEAD: `7bdf4f5703491b400d2248b27b8d97e1cba317da`
- Counterpart repo Implementation SHA (this round): `5de2ca8182c76158e198ca12c9ea36ddb8f2070e`
- Contract version: `1.0.0`
- Status: `IMPLEMENTATION_FINISHED` / `AWAITING_CODEX_FINAL_REVIEW` (this session does not claim the Foundation work is finalized, independently verified, or ready for downstream consumption - Codex final re-verification and user testing are still outstanding)

## DataResult Gson Parity + FG-M-01 fix round (current)

Codex's Gate verdict on the prior round was `PASS_WITH_NON_BLOCKING_FINDINGS`
(Critical 0, High 0) - two items remained before the `contract-v1` tag: the
user-approved `DataResult` Gson 2.8.2 empirical parity contract (below), and
Medium finding `FG-M-01` (the prior round's own TC-H-01 test seam had leaked
into production exposure).

- **DataResult Gson 2.8.2 empirical parity (`DEC-20260715-DATARESULT-GSON-
  PARITY`, user-approved)**: the prior round's "Pre-tag compatibility
  evidence" item (`DataResult` Gson-unsafe-allocation risk, `NOT_VERIFIABLE`)
  is now closed. The user obtained the real `gson-2.8.2.jar` (SHA-256
  `b7134929f7cc7c04021ec1cc27ef63ab907e410cf0588e397b8851181eb91092`) and
  authorized executing it directly, outside both production repos, against a
  class structurally identical to Android's `DataResult`. The existing
  7-case matrix (already run, reused as-is) plus a **new** 5-case matrix run
  this round (`{"result":"-1"}`, `{"result":"2147483647"}`,
  `{"result":"2147483648"}`, `{"result":"4.9"}`, `{"result":"abc"}`) plus one
  supplementary confirmatory case (`{"result":2147483648}` unquoted, added to
  turn an inference into a direct observation) fully pin down the int32-
  range/fractional-string/nonnumeric-string sub-policy the prior matrix left
  open. See "DataResult Gson 2.8.2 matrix - full results" below for the
  complete 13-observation table and the derived policy. **Fix**: `result`'s
  codec (`src/protocol/json/data_result_codec.c`) now implements this
  DataResult-ONLY policy - missing/null → `SAVVY_OK`, `result=0`; integer-
  format numeric string (in int32 range) → `SAVVY_OK`, parsed as that int;
  duplicate `"result"` key → `SAVVY_OK`, last occurrence wins (via a new
  sibling function `savvy_json_parse_allow_duplicate_keys`, `src/protocol/
  json/json_codec.c` - `savvy_json_parse()` itself, used by every other
  codec, is completely unmodified). Every other schema-managed object
  (`jsonConfigDto`, `jsonDeviceDto`, the IPC envelope root/payload) keeps its
  existing strict policy unchanged - verified by re-running `CT-JSON-001`
  (unchanged pass) alongside the expanded `CT-JSON-002`.
- **FG-M-01 (test-hook production exposure, Medium)**: the prior round's own
  TC-H-01 fault-injection seam (`savvy_test_poll_override`,
  `ipc_test_hooks.h`) sat in the *public* `include/savvy/platform/` path and
  was compiled into `libsavvy_platform_ipc.a` **unconditionally** - the
  library's own `add_subdirectory` is gated only by `SAVVY_IPC_REAL_
  TRANSPORT`, not `SAVVY_BUILD_TESTS`, so the symbol and the mutable-
  function-pointer indirection existed even in a `SAVVY_BUILD_TESTS=OFF`
  production build. **Fix** (test-only build-definition-gated private seam +
  test-target-only private header - two of the four recommended minimal
  approaches, used together): `ipc_test_hooks.h` moved to `src/platform/
  linux/ipc/` (no longer under any `PUBLIC` include path); `ipc_transport_
  common.c`'s `savvy_test_poll_override`/`savvy_do_poll()` indirection now
  lives inside `#ifdef SAVVY_IPC_TEST_HOOKS`, with a plain direct-`poll()`
  `#else` branch; `src/platform/linux/ipc/CMakeLists.txt` only defines
  `SAVVY_IPC_TEST_HOOKS` (`PRIVATE`, so it cannot leak into any consumer's
  own compile flags) `if(SAVVY_BUILD_TESTS)`; `tests/contract/CMakeLists.txt`
  adds the header's new location as a `PRIVATE` include path for the
  `test_ipc` target only. No timeout/cancel logic was touched - only the
  test-hook's compilation/visibility. See "FG-M-01 verification" below for
  the full `nm -g` evidence and the unweakened-J1-J6 re-run.

**This round's build/test verification**: macOS 5/5 (test_json's CT-JSON-002
now carries 21 checks, up from 8, all passing on both repos); Docker Linux
9/9 on both repos via the official `run-foundation-tests.sh`, plus a
separate production-only build (`SAVVY_BUILD_TESTS=OFF SAVVY_IPC_REAL_
TRANSPORT=ON`) confirming zero test-hook symbol exposure, plus a standalone
`test_ipc 004` run confirming all `004J1`-`004J6` deterministic checks still
pass with unweakened semantics.

## TC-H-01 / TC-M-01 / TC-L-01 / TC-L-02 fix round (prior round, historical)

A further Codex re-verification found a real regression introduced by the
previous round's own FINAL-M-01 fix (below), a genuine fd-reuse race between
`cancel()` and `destroy()`, and a documentation inaccuracy - plus asked for the
governing status of several long-standing items to be corrected (TC-L-02).

- **TC-H-01 (EINTR-after-deadline-expiry busy-retry regression)**: FINAL-M-01's
  fix (below) removed `savvy_ipc_poll_with_deadline_cancelable()`'s upfront
  `savvy_deadline_expired()` pre-check ENTIRELY to fix `timeout_ms == 0` - but
  that pre-check was also the only thing that stopped a sustained `EINTR`
  storm arriving *after* the absolute deadline had already elapsed, for
  `timeout_ms > 0`: with it gone, every `EINTR` unconditionally retried
  (`continue`), regardless of whether the deadline was already past, which
  could keep the loop retrying indefinitely under sustained signal pressure.
  **Fix**: a `first_poll_done` flag now exempts ONLY the very first `poll()`
  call from the expiry check (preserving FINAL-M-01's `timeout_ms == 0` fix -
  the very first poll always happens); every retry after that first call
  (i.e. every `EINTR`) checks `savvy_deadline_expired()` first and returns
  `SAVVY_ERR_TIMEOUT` immediately, without polling again, if the deadline has
  already elapsed. **Verified deterministically**: real external signal
  delivery cannot reliably land inside a single `poll(..., 0)` call's
  sub-microsecond window, so a new test-only fault-injection seam
  (`savvy_test_poll_override`, `include/savvy/platform/ipc_test_hooks.h`, a
  single function-pointer indirection around this one call site, `NULL` in
  every non-test build) lets tests script poll()'s exact return sequence:
  timeout=0 ready/not-ready both resolve in exactly 1 scripted poll() call;
  an `EINTR` before the deadline retries and then succeeds (2 calls); an
  `EINTR` that (via a scripted sleep) occurs strictly after a short deadline
  has elapsed resolves to `SAVVY_ERR_TIMEOUT` in exactly 1 call (proving no
  wasted second attempt); and a sustained-EINTR scenario with 30 scripted
  retries available against a 50ms deadline stops well before exhausting them
  (bounded call count) and does not meaningfully exceed the requested
  timeout. All against the SAME shared `ipc_transport_common.c` used by both
  repos' `recv`/`send`/`accept`/`connect` paths.
- **TC-M-01 (cancel/destroy fd-reuse race)**: concurrent
  `savvy_ipc_cancel_source_cancel()` and `savvy_ipc_cancel_source_destroy()`
  on the same source could race - `destroy()` closes both pipe fds, the OS
  is free to reuse those exact fd numbers for an unrelated object
  immediately afterward, and an in-flight `cancel()` could then write its
  cancel byte into that unrelated fd. Per this round's explicit instruction,
  no refcounting/generic lifetime framework was added. Instead,
  `include/savvy/platform/ipc_cancel.h` now states an explicit, documented
  concurrency contract: cancel+cancel and cancel+waiter concurrency remain
  supported; cancel+destroy racing is an explicit, undefined, unsupported
  precondition violation, not a race this API defends against; the required
  ordering before calling `destroy()` is (1) no new `cancel()` calls will be
  issued, (2) every thread that might still be calling `cancel()` has been
  joined, (3) every accept/connect waiter using the source has been joined -
  only then may `destroy()` run. This ordering was checked against every
  existing test's own structure (all already comply, by construction - none
  needed behavior changes). New tests confirm: `destroy()` then `cancel()`
  (strictly sequential) returns `SAVVY_ERR_INVALID_ARGUMENT`, not a
  crash; a second, sequential `destroy()` call remains a safe no-op; and the
  existing ordered-lifecycle tests are annotated as concrete examples of the
  one supported ordering. No production behavior changed - this finding was
  entirely a documentation/contract-clarity gap, not a code defect requiring
  a fix, per the explicit instruction not to build new lifetime machinery.
- **TC-L-01 (repeated-cancel documentation inaccuracy)**: `ipc_cancel.h`
  previously claimed repeated `cancel()` calls are "no-ops that skip the
  write" once a byte is already pending - inaccurate: every call attempts a
  REAL `write()`, which usually succeeds (a pipe's kernel buffer is normally
  tens of KB) rather than immediately hitting `EAGAIN`. Corrected to state
  the actual, safe behavior: every call writes; `EAGAIN`/`EWOULDBLOCK` once
  the buffer is genuinely full is treated as "already pending" success; and -
  since nothing in this API ever drains the pipe - a cancelled source must
  not be reused for a later, unrelated accept/connect attempt (a leftover
  pending byte would falsely cancel it immediately). Documentation-only, per
  the explicit instruction not to redesign the behavior.
- **TC-L-02 (SESSION_RESULT governing-status corrections)**: see "Docker
  Linux build/test" below (the locally-built image ID is now the closed,
  approved baseline, not a flagged mismatch), "Blockers and incomplete
  items" below (the placeholder contract files are reclassified as Wave 1
  adapter-contract pending scope, no longer listed as a Foundation blocker;
  RV1106 is explicitly separated from the host/Docker Foundation code Gate
  as target-validation-pending), and "Pre-tag compatibility evidence" below
  (the `DataResult` Gson item is now stated explicitly as NOT optional -
  required before the `contract-v1` tag, currently `NOT_VERIFIABLE`, and
  separate from this session's own Foundation Linux C code findings).

**This round's build/test verification**: macOS 5/5 (unchanged - this round's
fixes don't touch anything macOS builds); Docker Linux 9/9 on both repos via
the official `run-foundation-tests.sh` against the approved image, with the
new TC-H-01 deterministic fault-injection checks and the TC-M-01
destroy/cancel-ordering checks folded into the same supplementary
`CT-IPC-CANCEL` test; the pre-existing timeout-zero matrix (FINAL-M-01's own
`004H1`-`004H6`) re-verified with no regression.

## FINAL-M-01 / FINAL-M-02 / FINAL-L-01 fix round (prior round, historical)

A final Codex re-verification found two remaining production bugs and asked for
one report-only cleanup, on top of the V0R-H-01/F-07/V0B-H-02/F-08 round
documented in the (now historical, but still accurate) 23-item checklist
section below.

- **FINAL-M-01 (timeout_ms == 0 semantics)**: the public IPC timeout contract
  says `timeout_ms == 0` means "don't block, but check readiness exactly once."
  `savvy_ipc_poll_with_deadline_cancelable()` had a redundant pre-check
  (`if (savvy_deadline_expired(&deadline)) return 0;`) at the top of its loop,
  before poll() was ever called. `savvy_deadline_expired()` treats a 0ms
  deadline as expired from the very first instant (elapsed `>= 0` is always
  true), so this made `timeout_ms == 0` always report `SAVVY_ERR_TIMEOUT`
  regardless of actual readiness - an already-ready descriptor never got a
  chance to be observed. **Fix**: removed the pre-check entirely. This has zero
  effect on `timeout_ms > 0` behavior (an already-elapsed deadline there still
  resolves correctly via `poll(..., 0)` a few lines later, exactly as before -
  the check was purely redundant, and only broke the 0ms edge case). No busy
  loop is introduced; each call is still exactly one bounded `poll()`.
  **Verified**: a full ready/not-ready matrix across recv (H1/H2), send
  (H3/H4), and this repo's own connect role (H5/H6, including an honest
  documented note about the same backlog-exhaustion-doesn't-reliably-block
  environment quirk already found in the prior round's 004C1) - all 6 passing
  on Docker Linux, confirmed by running `test_ipc 004` standalone (not just via
  `ctest`, which only shows output for failing tests): 36/36 checks passed.
- **FINAL-M-02 (cancel self-pipe EINTR handling)**: `savvy_ipc_cancel_source_
  cancel()` called `write()` exactly once and discarded the result - an
  `EINTR` (byte never actually written) was silently treated as success,
  which could leave a blocked waiter with no way to know cancellation was ever
  requested. **Fix**: `savvy_ipc_cancel_source_cancel()` now returns
  `savvy_status_t` (was `void`): retries on `EINTR` (bounded to 16 attempts -
  never an unbounded/busy loop) until the byte is genuinely written; treats
  `EAGAIN`/`EWOULDBLOCK` (a byte from an earlier call is already pending) as
  success, not failure; propagates any other `errno` as `SAVVY_ERR_IO`. A
  caller that receives `SAVVY_OK` is now guaranteed the cancel byte is
  pending, so any waiter will wake promptly. This is a signature change to one
  function with no prior external callers besides this session's own tests
  (updated to check the new return value) - not a large-scale redesign of
  anything already resolved. **Verified**: a dedicated thread hammers `SIGUSR2`
  (handler installed *without* `SA_RESTART`, so delivered signals are
  guaranteed to actually interrupt a syscall) at the exact thread calling
  `cancel()`, for the full duration of that call. This cannot *guarantee*
  `EINTR` actually lands inside a single 1-byte pipe `write()`'s real syscall
  window on every run (such writes are normally sub-microsecond, faster than
  external signal delivery can reliably race against) - documented as an
  honest limitation, not overclaimed - but it does confirm the end-to-end
  outcome (`cancel()` returns `SAVVY_OK`, the waiter's connect attempt
  concludes definitively) holds under sustained concurrent signal pressure
  regardless of whether this specific run happened to hit the `EINTR` branch.
- **FINAL-L-01 (SESSION_RESULT report-only cleanup)**: see "Android baseline
  integrity" (reframed as NOT a Foundation code blocker), "Approved:
  keepServerIp divergence" (reframed as an approved, closed design decision,
  not an open `SCOPE_CHANGE_REQUEST`), and "Pre-tag compatibility evidence"
  (the `DataResult` Gson item, kept open and explicitly NOT recorded as
  resolved, tracked separately from "Blockers") below - all report-only, no
  code or test behavior changed by this item.

**This round's build/test verification**: macOS 5/5 (unchanged in substance -
`test_ipc` isn't built there); Docker Linux 9/9 on both repos via the official
`run-foundation-tests.sh` against the official image, **plus** direct
standalone execution of `test_ipc 004` in a throwaway container (bypassing
`ctest`'s output-on-failure-only capture) to visually confirm every individual
new `CHECK` - 30/30 (mgr) and 36/36 (sensor) - see "macOS host build/test" and
"Docker Linux build/test" below for full detail.

## Codex re-verification round - 23-item checklist (prior round, historical)

This section is the authoritative index for the **prior** (V0R-H-01/F-07/
V0B-H-02/F-08) round's required items - kept here unmodified for that round's
own record; later sections give full detail and are cross-referenced by number.

1. **Pre-this-round HEAD**: `92142bd65ed68e8c4b4f42bd5733351565f7356e` (see header block above).
2. **Final implementation commit SHA**: `2a068ef205108edd4875f495253de261e0b76e1c` (see header block above; the SESSION_RESULT report commit lands one commit after this, per the F-12 resolution).
3. **Finding IDs fixed this round**: `V0R-H-01`, `F-07`, `V0B-H-02`, `F-08`. See "Findings addressed" below for what each required and how it was verified.
4. **Modified files this round**: 17 files, 1112 insertions(+), 58 deletions(-), 1 commit. Full list in "Modified files" below.
5. **Android baseline verification method**: no new Android-source research was needed for V0R-H-01's fix itself beyond what Phase 4 already established, but the fix's correctness was independently re-confirmed this round via `git show <pinned-sha>:<path>` against both `savvy_mgr`@`ad83cabe...` and `savvy_sensor`@`48e2d144...` (never the working tree, which remains under the integrity issue documented below) - see "V0R-H-01" under "Findings addressed" for exact citations.
6. **macOS test command + result**: see "macOS host build/test" below - 5/5 passing (CT-PKT-001~003, CT-JSON-001~002; `test_ipc`/CT-IPC-*/CT-IPC-CANCEL are not built here, `SAVVY_IPC_REAL_TRANSPORT=OFF`).
7. **Docker test command + result**: see "Docker Linux build/test" below - 9/9 passing (all 8 required + the supplementary CT-IPC-CANCEL).
8. **Docker image name + Image ID**: `savvy-foundation-test:ubuntu22.04-arm64-v1`, built locally from the user-provided Dockerfile at `SAVVY_migration_control_v1.0/docker/foundation/Dockerfile` as `sha256:73c8a9709607d1910231efb4648510e4d72052072629901fa28fd5c9f39753e7` - **this did not match the Image ID stated in the original task prompt** (`sha256:ec199756c978f0ed5ad9e73a9df5b54d7caaaafe2556cb3367dbde96694956f9`); that tag was not present on this machine's Docker daemon (`docker image inspect` returned "No such image" before this round's build), so it was built fresh from the provided Dockerfile rather than reused. **Resolved per TC-L-02 (a later round)**: `sha256:73c8a970...` is now the approved, closed local Docker baseline for this project - see "Docker Linux build/test" below, not an open discrepancy anymore.
9. **CT-PKT result**: 3/3 passing on both platforms, unchanged in substance this round (test_packet.c gained an F-08 fix, see below, but no new packet-contract behavior).
10. **CT-JSON result**: 2/2 passing on both platforms, unchanged in substance this round (test_json.c gained committed UTF-8 matrix coverage, see F-08 below, but no new JSON-contract behavior).
11. **CT-IPC result**: 3/3 (CT-IPC-001~003) passing on Docker Linux, plus the new supplementary CT-IPC-CANCEL (9th test) also passing - see "V0B-H-02" below for what CT-IPC-CANCEL covers.
12. **Reconnect replay test result**: passing - see "F-07" under "Findings addressed" below (verified inside CT-IPC-002: no callback on first connect, exactly-once firing with correct Config-before-Device order on reconnect, a genuine callback-internal failure that doesn't crash the process, and a second independent reconnect firing again).
13. **Timeout/cancel test result**: passing - see "V0B-H-02" below (CT-IPC-CANCEL: blocked reader/writer woken by another thread's close(), connect() cancellation proven deterministically, plain timeout still works, repeated EINTR doesn't reset the deadline, idempotent double-close, one transport's failure doesn't affect another).
14. **65,536B/65,537B test result**: passing on both the pre-existing sender-side check (CT-IPC-003) and the new F-08 receiver-side global-cap check with an oversized-record-sized-or-larger receive buffer (CT-IPC-003, extended this round).
15. **Contract manifest hash**: unchanged this round (`contracts/**` was not touched) - see "Contract manifest comparison" below for the full listing.
16. **Dependency manifest hash**: unchanged this round (cJSON was not touched) - see "Dependency manifest comparison" below.
17. **Two-repository comparison result**: contract manifest identical (`diff`, exit 0); dependency manifest identical (`diff`, exit 0); this round's finding-by-finding fixes applied symmetrically to both repos' role-specific code (MGR=server/accept, Sensor=client/connect) sharing the same `ipc_transport_common.c`/`ipc_cancel.h`/`ipc_reconnect.c`/`ipc_reconnect.h`, confirmed byte-identical between repos.
18. **Allowed paths violation check**: none - full modified-file list (below) cross-checked against the Allowed paths list; every path falls under `CMakeLists.txt`/`src/core/**`/`src/protocol/**`/`src/platform/interfaces/**`/`src/platform/linux/ipc/**`/`include/**/core/**`/`include/**/protocol/**`/`include/**/platform/**`/`tests/contract/**`.
19. **Android repository not modified check**: both `savvy_mgr` and `savvy_sensor` HEADs remain exactly the pinned SHAs; this session issued no write commands against either repo this round (only `git show`/`git status`/`stat` read-only checks, per the read-only research method in item 5). The pre-existing, still-unresolved working-tree drift reported in the prior round is unchanged - see the dedicated integrity section below.
20. **No new feature added check**: confirmed - all four findings are Foundation-layer fixes (catalog type correctness, a minimal hook interface, transport cancellation, test-vector correctness), not Wave 1 feature work; `src/features/**`, `src/app/**` remain untouched; no Wi-Fi/BT/BLE/TCP-8140/TCP-8141/ToF/RKNN/GPIO/watchdog/thermal/OTA code was added.
21. **Hardware QA not performed check**: confirmed - no board/hardware interaction this round; verification is macOS host + Docker Linux (`linux/arm64` container) only, exactly as scoped.
22. **Remaining blockers**: see "Blockers and incomplete items" below (RV1106 cross-build, `SO_PEERCRED`/production socket path, placeholder contract files, no `CONTRACT_CHANGE_REQUEST` filed, the Docker Image ID mismatch) - per FINAL-L-01, Android baseline integrity and `keepServerIp` are explicitly NOT blockers (see their own dedicated sections), and `DataResult` Gson-risk is open but tracked separately as pre-tag compatibility evidence, not a blocker.
23. **Tag not created check**: confirmed - `contract-v1` was not created this round (or any prior round); no new branch, merge, rebase, or push was performed either.

## ⚠ Android baseline integrity - separate audit item, NOT a Foundation code blocker

**FINAL-L-01 clarification**: this is a real, unresolved integrity concern about
the two read-only Android reference repos' *working trees* (see below), but it is
**not a blocker on this Foundation code** and does not appear in "Blockers and
incomplete items" below. Every Android-source citation this session has ever
made - this round's V0R-H-01 re-verification included - was read via `git show
<pinned-sha>:<path>` against the pinned commit *objects*, never the working
tree; Foundation's implementation provenance has never depended on, and is
unaffected by, whatever is currently sitting in either working tree. The Android
repositories themselves have not been modified by this session (see item 19 in
the checklist above) and will not be, regardless of how this integrity item is
eventually resolved - that decision belongs to whoever owns Android baseline
management, as a separate, explicitly out-of-scope-for-Foundation action.

Both read-only Android reference repos currently have **uncommitted working-tree
changes**, independently re-confirmed via `git status`/`git diff`/`stat` immediately
before this commit (originally flagged by Codex V0A F-01/F-11 against an earlier
snapshot; unchanged since the prior round's report, still true now; **not
re-investigated further this round** beyond confirming the situation is unchanged -
this round's own Android-source citations used `git show` against the pinned SHAs
only, never the working tree, per this task's explicit instruction):

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

**Nothing is unrecoverable**: both repos' HEAD commits are exactly the pinned
SHAs; the corruption is uncommitted working-tree drift only. This session has not
run any restorative command on either repo and will not without explicit
direction - these repos are strictly read-only per this task's instructions, and
restoring them (even to "undo" apparent damage) is a decision for whoever owns
baseline integrity, not something to do unilaterally after the fact. Full timing
evidence and analysis of possible causes is in the prior round's version of this
document (`git show 92142bd:SESSION_RESULT.md` on this branch) - not repeated
verbatim here to keep this revision focused on the current round, but the
finding itself remains exactly as serious and exactly as unresolved.

## F-12 resolution (SESSION_RESULT SHA/stat mismatch)

Codex V0A F-12 originally flagged that a prior revision of this document
self-embedded a SHA for its own commit, which is inherently impossible to keep
consistent (the previous revision's own report commit lands after the SHA it
tried to claim as final). Resolution, unchanged from the prior round and applied
again here: this document names an explicit **Implementation SHA** (the last
commit that changes code/contracts/tests) and does not attempt to self-embed a
"Report SHA" for its own commit. All stats in this document are `git rev-parse`/
`git diff --stat`/`git log --oneline` output captured against the Implementation
SHA, not hand-typed.

## Independent final verification pass

Before reporting this round complete, an independent reviewer (model: Opus, no
prior context from this implementation work) was asked to adversarially re-check
the diff against each finding's claim, allowed-paths compliance, Android-repo
non-modification, tag/branch/merge absence, the actual Docker log evidence, and
the forbidden-status-phrase constraint - not just re-read this document's prose.
Result: every functional claim (A-F in its report) verified independently and
passed. One real issue was found (G): this file's own status-disclaimer sentence
quoted the forbidden phrases verbatim inside a negation ("does not declare
X"), which a naive automated substring check could mistake for a violation
despite the clearly-compliant intent; reworded (see the Status line above) so
none of the literal forbidden strings appear anywhere in this document. A
non-blocking style nit (H) was also fixed: two CT-IPC-002 resync payloads used
bare JSON numbers for fields V0R-H-01 now defines as strings (harmless there,
since that code path only tests delivery, not `savvy_ipc_action_validate_
payload`, but confusing to read next to that fix). Both fixes were re-verified
against the full 9/9 Docker suite before this document's own report commit.

## Findings addressed this round

### V0R-H-01 - IPC action payload type regression

**Finding**: `PwrLedState`, `AlertLedState`/`AlertTime`/`AlertSec`, `STATE`,
`IFCOMM_START`, `DELAY_SEC` were declared `VT_NUMBER` in
`src/protocol/ipc/ipc_action_catalog.c`, and `CT-IPC-001`'s own test vectors were
written to match that (JSON number = valid, JSON string = type-mismatch) - the
exact inverse of the real contract. `contracts/ipc_action_catalog.md`'s own
per-action notes already documented these as strings ("string, numeric ordinal" /
"strings, numeric" / "int as string" / "stringified byte" / hardcoded `"5"`), so
this was a code+test regression against the project's own already-correct
contract doc, not a contract ambiguity.

**Independent re-verification** (`git show <pinned-sha>:<path>`, both Android
repos, never the working tree): both `savvy_mgr` and `savvy_sensor` route every
one of these Bundle values through a shared `sendIpcMessage(action, argNames[],
argValues[])` helper whose loop body is `b.putString(argNames[i], argValues[i])`
- confirmed at `savvy_sensor/app/src/main/java/com/uniuni/savvysensor/MainActivity.java:2433-2452`
and the equivalent in `savvy_mgr/app/src/main/java/com/uniuni/savvymgr/MessengerService.java:127-137`.
Every caller stringifies its int/byte/ordinal value via `String.valueOf(...)`
before calling this helper (e.g. this repo's own `callBroadcastGetstateSensor()`
at `MainActivity.java:1179-1181` for `STATE`; `callBroadcastAlert()` at
`MainActivity.java:1194-1197` for `IFCOMM_START`; `callBroadcastReStart()` at
`MainActivity.java:1819-1821` for `DELAY_SEC`, confirming the literal `5` is
sent as the string `"5"`; MGR's `SavvyService.callBroadcastStatusLedPwr()` at
`SavvyService.java:2199-2200` for `PwrLedState`). No `putInt`/`getInt` call site
exists for any of these 7 keys in either repo at the pinned SHAs; old
`Intent.putExtra(key, <int/byte>)` lines visible near some call sites are dead,
commented-out code from a pre-Messenger design. One incidental finding recorded
for completeness, not a type mismatch: MGR's `actionRestartSensor(Bundle)`
(`SavvyService.java:2833-2836`) never actually reads the `DELAY_SEC` key this
repo sends - a full-repo grep for the literal `"DELAY_SEC"` at the pinned MGR SHA
returns zero matches outside the catalog constant itself. This is an
Android-side quirk on the MGR receiving end, not a defect in this repo, and is
not fixed here.

**Fix**: all 7 keys changed to `VT_STRING` in the catalog; `CT-IPC-001`'s valid/
type-mismatch vectors inverted to match (valid = JSON string, mismatch = JSON
number). Also added a `nullable` flag to `catalog_key_t`, independent of
`required`: `PROPERTY_BROADCAST_TOF`'s four keys are now `required=false,
nullable=true` (missing OR present-as-null are both accepted; present with a
non-null wrong type is still rejected), and the validator now checks
`cJSON_IsNull` explicitly before the type check rather than falling through to a
type check that a null value would fail anyway for the wrong reason. New test
coverage: a 4-case matrix for the TOF nullable fields (missing/null/valid/
invalid-non-null), a non-nullable-required-key-as-null rejection case, and a
payload-root-not-an-object case (bare string, bare array).

### F-07 - reconnect Config/Device replay hook

**Finding**: only the transport-level reconnect capability was proven; nothing
verified an actual replay-ordering guarantee or hook interface a Wave 1 consumer
could use.

**Fix**: `savvy_ipc_reconnect_tracker_t` (`include/savvy/platform/
ipc_reconnect.h` + `src/platform/interfaces/ipc_reconnect.c` - a new, portable,
Linux-syscall-free module, buildable on `host-mac` too) tracks exactly one bit of
state ("has a connect been observed before") and exposes
`savvy_ipc_reconnect_tracker_on_connected(tracker, hooks)`: a no-op on the first
call after `init()`, and on every call after that, invokes `hooks->request_config`
then `hooks->request_device`, in that order, exactly once. Foundation does not
implement a real Config/Device business store, disk queue, or any durable retry
beyond what Android itself has - this is only the hook interface; the actual
resend logic is Wave 1's (CC-SENSOR-CORE), consuming this hook around its own
`accept()`/`connect()` call sites. Verified inside `CT-IPC-002` (which already
tests the real connect→disconnect→reconnect flow): first connect fires nothing;
reconnect fires the hook exactly once with Config strictly before Device
(checked via a shared monotonic call-order index); the `request_config` callback
in the test deliberately attempts a `send()` through an already-closed transport
to simulate a genuine internal failure, and this neither crashes the test process
nor prevents the rest of the reconnect flow (including the pre-existing
CONFIG+DEVICE+STATUS_ALERT+STATUS_LED_PWR resync-capability check) from
completing normally; a second, independent reconnect fires the hook again
(twice total, once each), proving it isn't a one-time latch.

### V0B-H-02 - strict cancellable I/O

**Finding**: the transport layer only bounded blocking I/O by a timeout; nothing
let a caller *cancel* a blocked `send()`/`recv()`/`connect()` from another thread
before its deadline, and repeated `EINTR` could in principle extend a wait well
past its stated timeout if naively retried.

**Fix**:
- `fd_transport_close()` now calls `shutdown(fd, SHUT_RDWR)` immediately before
  `close(fd)`. This is a POSIX-documented, race-free way to interrupt another
  thread currently blocked in `poll()`/`send()`/`recvmsg()` on the *same* fd - a
  bare `close()` gives no such guarantee (its effect on a different thread's
  concurrently-blocked call is unspecified, and the fd number can even be reused
  by an unrelated new socket before the blocked thread notices). No public API
  changed - `transport.close()` already existed with this exact signature.
- A new `savvy_ipc_cancel_source_t` (`include/savvy/platform/ipc_cancel.h`, a
  self-pipe) lets a caller request cancellation of a pending
  `savvy_ipc_client_connect_cancelable()` call - a new function, additive only;
  this repo's `savvy_ipc_client_connect()` keeps its exact existing signature,
  now implemented as a thin wrapper passing `cancel=NULL`. The cancelable
  function checks the cancel source once, upfront, before doing any other work,
  in addition to checking it inside the poll wait itself - a healthy `connect()`
  to a listener with backlog room can complete synchronously (never returning
  `EINPROGRESS`), which would otherwise let an already-cancelled attempt
  "succeed" anyway.
- `savvy_ipc_poll_with_deadline()` (and the new `..._cancelable` variant, shared
  with the counterpart repo's `ipc_transport_common.c`) now arms one absolute
  `CLOCK_MONOTONIC` deadline via `savvy/core/clock.h` at entry and recomputes the
  *remaining* time on every `EINTR`, instead of restarting a full
  `timeout_ms`-length wait each time.

**Verification** (`CT-IPC-CANCEL`, a supplementary test - see naming note
below): a thread blocked in `recv()` wakes within 2s of another thread's
`close()` (not the full 30s timeout it was given); a thread blocked in `send()`
(send buffer deliberately shrunk to 1KiB and filled) wakes the same way,
reporting a failure status, not a stale success; a plain bounded `recv()` with
nothing sent and nothing cancelling still returns `SAVVY_ERR_TIMEOUT` at
approximately its requested deadline; a `recv()` given a 500ms timeout while a
forked child process delivers `SIGUSR1` (handler installed without
`SA_RESTART`, so every signal genuinely interrupts `poll()`) roughly every 50ms
for ~2s still returns at ~500ms, not ~2s+, proving the deadline isn't reset by
repeated `EINTR`; `close()` called twice in a row is idempotent and a
post-close `recv()` reports `SAVVY_ERR_CLOSED`; and closing one transport
instance is confirmed to leave a second, independent transport instance (and
the ability to create a brand new third one afterward) completely unaffected.

For `connect_cancelable` specifically (this repo's own role), two sub-cases were
needed: an initial attempt exhausted a `listen(fd, 1)` backlog with one
un-accepted connection and expected the *second* `connect()` to genuinely block
long enough to race a cancellation delivered ~200ms later - **empirically, on
this Docker image's kernel, it does not**: the second AF_UNIX connect()
resolved before the cancellation could apply, so that scenario's assertion was
loosened to "never hangs, and if it doesn't succeed outright it must specifically
be `SAVVY_ERR_CANCELLED`" (documented as an environment observation, not a code
defect - AF_UNIX loopback connects complete near-instantaneously in general, and
backlog enforcement is evidently more lenient than a hard cap of exactly 1 on
this kernel). The deterministic, strictly-asserted proof is a second sub-case:
cancelling the source *before* calling `connect_cancelable()` at all, against a
listener that would otherwise accept normally - this must, and does, return
`SAVVY_ERR_CANCELLED` promptly every time, which is exactly what the upfront
pre-check (added specifically because of this finding) guarantees regardless of
whether the underlying `connect()` would have gone through `EINPROGRESS`+`poll()`
or resolved synchronously.

### F-08 - missing/incorrect test vectors

- **Packet max body**: the existing `SIZE_MAX` overflow-rejection case passed
  `data == NULL`, which trips `savvy_packet_encode`'s *earlier*
  `data == NULL && data_len != 0` argument-validation guard before either
  overflow check is ever reached - the test "passed" via
  `SAVVY_ERR_INVALID_ARGUMENT` without ever exercising the overflow guards it
  claimed to cover. Fixed to use a real, small, non-NULL buffer (safe regardless
  of the bogus claimed length, since the function must reject on `data_len`
  alone before ever reading from it) and to assert `SAVVY_ERR_OVERFLOW`
  specifically, for both the `SIZE_MAX` case and a separate `UINT32_MAX + 1`
  case. (On this project's actual 64-bit `size_t` build targets, both values are
  caught by the same `data_len > UINT32_MAX` guard - `SIZE_MAX -
  SAVVY_PACKET_HEADER_LEN` is still far larger than `UINT32_MAX`, so the second,
  header+data_len-addition-overflow guard is defense-in-depth for a
  hypothetical 32-bit build this test suite doesn't target, not independently
  exercisable here; documented in the test's own comment rather than silently
  glossed over.)
- **IPC global message cap**: added a new case sending a real 65537-byte raw
  record (bypassing the client-side `send()` guard, same technique as the
  pre-existing `MSG_TRUNC` case) into a receive buffer *larger* than the record
  itself, so `MSG_TRUNC` cannot be what catches it - only the independent
  `n > SAVVY_IPC_MAX_MESSAGE` check (the F-06/M-05 fix from the prior round) can,
  and does. A normal record sent immediately after is confirmed uncorrupted,
  exactly like the existing `MSG_TRUNC` recovery case.
- **UTF-8 matrix**: `savvy_utf8_validate`'s own doc comment already claimed to
  reject truncated continuations, overlong encodings, surrogate-range
  codepoints, and out-of-Unicode-range codepoints, but only the truncated case
  was committed to the real test suite (the others were previously verified only
  via an ad-hoc scratch script outside the repo). Added committed cases for: a
  valid multibyte string (`"café"`, must be accepted and preserved
  byte-for-byte - guards against a false-positive-prone fix); the classic
  overlong-slash vector (`0xC0 0xAF`, the canonical 1-byte encoding of `/`
  re-encoded as an unnecessary 2 bytes); the first UTF-16 high surrogate
  (`U+D800` as `0xED 0xA0 0x80`, forbidden in UTF-8 by RFC 3629 even though the
  3-byte sequence is structurally well-formed); and one codepoint past the
  Unicode maximum (`U+110000` as `0xF4 0x90 0x80 0x80`).

Supplementary coverage for the V0B-H-02/F-08 additions above (the cross-thread
cancellation scenarios and the receiver-side-cap-with-large-buffer case) is
registered as **`CT-IPC-CANCEL`**, deliberately not `CT-IPC-004` - this is
additional coverage beyond, not a renumbering of, the required `CT-PKT-001~003`/
`CT-JSON-001~002`/`CT-IPC-001~003`.

## Modified files (this round)

**Current round** (prior HEAD `4c1f144`..Implementation SHA `aca143a`): 12
files changed, 409 insertions(+), 72 deletions(-), 1 commit:

```text
contracts/contract-manifest.sha256
contracts/json_field_policy.md
include/savvy/protocol/data_result_codec.h
include/savvy/protocol/json_codec.h
src/platform/linux/ipc/CMakeLists.txt
src/platform/linux/ipc/ipc_test_hooks.h            (moved from include/savvy/platform/)
src/platform/linux/ipc/ipc_transport_common.c
src/protocol/json/data_result_codec.c
src/protocol/json/json_codec.c
tests/contract/CMakeLists.txt
tests/contract/test_ipc.c
tests/contract/test_json.c
```

All twelve fall within the Allowed paths list for this round
(`contracts/**`, `include/**/protocol/**`, `src/platform/linux/ipc/**`,
`src/protocol/**`, `tests/contract/**`).

Full base..HEAD session total (all rounds): 19 commits, 63 files changed
(`git log --oneline`/`git diff --stat`, run directly; the file-count and
insertion total below are pure insertions relative to base since every
touched file was created after base - deletions only show up within a
single round's own narrower diff, as in "Current round" above):

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
92142bd docs: update SESSION_RESULT.md for Codex-review fix round
2a068ef fix: address Codex re-verification findings (V0R-H-01, F-07, V0B-H-02, F-08)
0e0350a docs: update SESSION_RESULT.md for Codex re-verification round
c9cd633 fix: remove literal forbidden-status substrings and align resync test data with V0R-H-01
fc0aa62 docs: record the independent verification pass and final touch-up SHA
941545f fix: FINAL-M-01 timeout_ms==0 semantics and FINAL-M-02 cancel EINTR handling
c1fc299 docs: FINAL-M-01/M-02 findings + FINAL-L-01 report-only cleanup
bc89652 fix: TC-H-01 EINTR-after-deadline regression, TC-M-01/TC-L-01 cancel/destroy contract docs
4c1f144 docs: TC-H-01/M-01/L-01 findings + TC-L-02 governing-status corrections
aca143a fix: DataResult Gson 2.8.2 empirical parity contract, FG-M-01 test-hook production exposure
```

The prior rounds' own file lists (17 files/1112(+)/58(-), then 3 files/271(+)/
17(-), then 5 files/416(+)/33(-)) are unchanged from what's recorded further
above/below in their own historical sections. All paths across every round
fall within the Allowed paths list. Local `build/` directories and stray
`.omc/` framework artifacts (regenerated/recreated by tooling, never staged)
were removed before this round's commit.

## macOS host build/test

```bash
cmake --preset host-mac
cmake --build --preset host-mac
ctest --preset host-mac --output-on-failure
```

```text
100% tests passed out of 5
    CT-PKT-001 .......... Passed
    CT-PKT-002 .......... Passed
    CT-PKT-003 .......... Passed
    CT-JSON-001 ......... Passed
    CT-JSON-002 ......... Passed
```

**This round**: `test_json 002` run standalone to see every individual
`CHECK` (not just the aggregate `ctest` line) - **21/21 passed**, including
all new DataResult Gson-parity cases (missing/null→0, numeric strings
"4"/"7"/"-1"/"2147483647", fractional-format string, non-numeric string,
out-of-range string and unquoted number, both duplicate-key orders,
bool/array/object rejection, invalid UTF-8). Full list in "DataResult Gson
2.8.2 matrix - full results" below.

`test_ipc` (CT-IPC-001~003, CT-IPC-CANCEL) is not built here
(`SAVVY_IPC_REAL_TRANSPORT=OFF` default for `host-mac`; Darwin has no `AF_UNIX
SOCK_SEQPACKET`) - this environment does not claim to have verified the real
Linux IPC transport. Toolchain: AppleClang 17.0.0.17000404, CMake 4.4.0.

## Docker Linux build/test

Run via the user-provided official script, exactly as provided, against the
user-provided `Dockerfile`:

```bash
/Users/juganghyeon/Desktop/uniuni/SAVVY_migration_control_v1.0/docker/foundation/run-foundation-tests.sh
```

which internally runs (per repo, `--platform linux/arm64 --network none`,
source read-only mounted, build directory in the container's own `/tmp`):

```bash
cmake -S <container_path> -B <build_path> -G Ninja -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON -DSAVVY_IPC_REAL_TRANSPORT=ON
cmake --build <build_path> --parallel
ctest --test-dir <build_path> -N
ctest --test-dir <build_path> --output-on-failure
```

```text
100% tests passed, 0 tests failed out of 9
    CT-PKT-001 .......... Passed
    CT-PKT-002 .......... Passed
    CT-PKT-003 .......... Passed
    CT-JSON-001 ......... Passed
    CT-JSON-002 ......... Passed  (now 21 checks, DataResult Gson-parity - see below)
    CT-IPC-001 .......... Passed
    CT-IPC-002 .......... Passed
    CT-IPC-003 .......... Passed
    CT-IPC-CANCEL ........ Passed  (~3.3s - unchanged this round; FG-M-01's
                                    refactor did not alter test-build
                                    semantics, only production exposure)
```

`ctest --output-on-failure` only prints a test's stdout when it fails, so a
passing `CT-IPC-CANCEL` line alone doesn't show each individual `CHECK`
inside it. Since FG-M-01's fix specifically needed to prove the `004J1`-
`004J6` deterministic checks are unweakened, `test_ipc 004` was run
**standalone**, directly, in a throwaway container (same image), to see
every line: **46/46 checks passed**, including every `004J1`-`004J6` name
explicitly with the exact same semantics as before the refactor ("exactly 1
poll() call", "retried (2 poll() calls)", "bounded well below the 30
scripted", etc.) - see "FG-M-01 verification" below for the full output and
the separate production-only build this round added.

Environment: `savvy-foundation-test:ubuntu22.04-arm64-v1`, GCC 11.4.0, CMake
3.22.1, Ninja 1.10.1, `linux/arm64`. Full raw logs written to
`SAVVY_migration_control_v1.0/reviews/codex/foundation/v0/evidence/
docker-sensor-foundation.log` (this repo) and `docker-mgr-foundation.log`
(counterpart) by the script itself.

**Docker image ID - closed baseline (unchanged from prior rounds).**
`savvy-foundation-test:ubuntu22.04-arm64-v1` @
`sha256:73c8a9709607d1910231efb4648510e4d72052072629901fa28fd5c9f39753e7`
remains the approved, closed local Docker baseline for this project. Both
repos' full 9/9 pass under this exact image.

## DataResult Gson 2.8.2 matrix - full results

All cases below were run with the exact `gson-2.8.2.jar` (SHA-256
`b7134929f7cc7c04021ec1cc27ef63ab907e410cf0588e397b8851181eb91092`,
independently confirmed via `shasum -a 256` this round), outside both
production repos, against a `DataResult`-shaped class matching
`IfComm/DataObjects/DataResult.java` exactly (`int result = 4;` field
initializer, only constructor `DataResult(int)`, no no-arg constructor,
plain `new Gson()`).

**Original 7-case matrix** (already run before this round; reused as-is,
not re-run):

| JSON | Result |
|---|---|
| `{"result":4}` | SUCCESS, result=4 |
| `{"result":7}` | SUCCESS, result=7 |
| `{}` | SUCCESS, result=**0** |
| `{"result":null}` | SUCCESS, result=**0** |
| `{"result":"4"}` | SUCCESS, result=**4** |
| `{"result":4.9}` | FAIL - "Expected an int but was 4.9" |
| `{"result":4,"result":7}` | SUCCESS, result=**7** |

**G001 additional 5-case matrix** (this round):

| JSON | Result |
|---|---|
| `{"result":"-1"}` | SUCCESS, result=-1 |
| `{"result":"2147483647"}` | SUCCESS, result=2147483647 |
| `{"result":"2147483648"}` | FAIL - "Expected an int but was 2147483648" |
| `{"result":"4.9"}` | FAIL - "Expected an int but was 4.9" |
| `{"result":"abc"}` | FAIL - "For input string: \"abc\"" |

**Supplementary confirmatory case** (this round, not part of the original
5-case ask - added to eliminate an inference gap rather than assume
symmetry):

| JSON | Result |
|---|---|
| `{"result":2147483648}` (unquoted) | FAIL - "Expected an int but was 2147483648" (identical error format to the quoted-string case, confirming Gson routes both through the same numeric-token parser) |

**Derived policy** (from the 13 observations above only - not extrapolated):
JSON integer in int32 range → success; fractional/non-finite/out-of-range
JSON integer → error; missing/null → success, result=0; integer-format
numeric string in int32 range → success, parsed as int; integer-format
numeric string out of range → error; fractional-format or non-numeric
string → error; any other string lexical form (leading `+`, whitespace,
exponent) → error (untested, not assumed-accepted); bool/array/object →
error; duplicate key → last value wins. Full contract in `contracts/
json_field_policy.md` §4.1 and `include/savvy/protocol/data_result_codec.h`.

Artifacts (outside both production repos):
`SAVVY_migration_control_v1.0/artifacts/gson/2.8.2/dataresult-matrix/`
(`DataResultGsonMatrix.java`/`.log` - original 7-case;
`DataResultGsonMatrixAdditional.java`/`_additional.log` - G001 5-case;
`DataResultGsonMatrixSupplementary.java`/`_supplementary.log` - the 1
confirmatory case).

## FG-M-01 verification

Production-only build, both repos, same Docker image:

```bash
cmake -S <repo> -B <build> -G Ninja -DCMAKE_BUILD_TYPE=Debug -DSAVVY_BUILD_TESTS=OFF -DSAVVY_IPC_REAL_TRANSPORT=ON
cmake --build <build> --parallel
nm -g <build>/src/platform/linux/ipc/libsavvy_platform_ipc.a
```

Result: `tests/` is not built at all (`SAVVY_BUILD_TESTS=OFF` skips
`add_subdirectory(tests/contract)` entirely); `nm -g` on the resulting
`libsavvy_platform_ipc.a` shows every expected production symbol
(`savvy_ipc_cancel_source_*`, `savvy_ipc_poll_with_deadline*`,
`savvy_ipc_send_capped`, `savvy_ipc_recv_capped`, `savvy_ipc_fd_transport_
init`, `savvy_ipc_client_*`) and **zero** occurrences of
`savvy_test_poll_override` or any other test-hook symbol, in either repo -
confirmed both by full symbol-table listing and a targeted
`grep -i 'test_poll\|test_hook'` (no matches). This holds for both
`sensor_to_Linux` (`ipc_client.c`) and `mgr_to_Linux` (`ipc_server.c`).

Test build (unchanged `SAVVY_BUILD_TESTS` default = ON,
`SAVVY_IPC_REAL_TRANSPORT=ON`): `test_ipc 004` run standalone, both repos -
**sensor: 46/46 passed, mgr: 40/40 passed** (this repo has more sub-cases
per its existing connect-vs-accept role asymmetry, unrelated to this
round), including every `004J1`-`004J6` TC-H-01 deterministic check with
the exact same semantics as before this round's refactor.

## Contract manifest comparison

**Updated this round** (`contracts/json_field_policy.md` §4 rewritten for
`DEC-20260715-DATARESULT-GSON-PARITY`) - regenerated via `cmake/generate_
manifest.cmake` (the same deterministic generator used every prior round),
independently cross-checked with `shasum -a 256`. Remains byte-for-byte
identical between `sensor_to_Linux` and `mgr_to_Linux` (`diff`, exit 0), no
self-entry, no absolute paths, no timestamps, no hostname, no username. Only
`json_field_policy.md`'s hash changed; the other four entries are unchanged:

```text
e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855  contracts/bt_spp.md
60bd04569266011a2a1c37fcee89154c7a5b76a2df87fd806e22be92e32c9874  contracts/ipc_action_catalog.md
c8ab42374fd606ed4a4c0a6c78037c032442622fea004051b2bf103679a10101  contracts/json_field_policy.md
913e373a93d71e243a4efdf95e6edd28b4f36cbcea75ca22d36cf43d85bebcb0  contracts/mgr_sensor_ipc.schema.json
e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855  contracts/tcp_8140.md
```

## Dependency manifest comparison

Unchanged this round (cJSON was not touched). `third_party/
DEPENDENCY_MANIFEST.sha256` (3 entries, cJSON v1.7.19 @
`c859b25da02955fef659d658b8f324b5cde87be3`) remains byte-for-byte identical
between both repos:

```text
298581a04a36c0165da4b0aade235c23088cb2faa58651d720ea2f3706ed0b0d  third_party/cJSON/cJSON.c
25b0145150d500498e4d209cec69c18c42cf818bffcc54690be3b895a2a16dee  third_party/cJSON/cJSON.h
a36dda207c36db5818729c54e7ad4e8b0c6fba847491ba64f372c1a2037b6d5c  third_party/cJSON/LICENSE
```

## keepServerIp - corrected history

**This document's own prior revisions understated the real history.** The
actual sequence, confirmed directly from `git log -p` on
`src/protocol/json/config_codec.c` in this repo (not asserted from memory):

1. **Initial Linux implementation** (this repo's commit `c1eed05`,
   "foundation: FND-02 typed JSON codecs..."): this repo's own
   `savvy_config_set_defaults()` was written with `keep_server_ip =
   "15.165.113.212"` - the **MGR-side** Android default - even though the code
   comment at the time already noted "Sensor-side Android default differs
   (13.125.173.114)". This was a real, if documented-as-known, drift between
   the Foundation code and this app's own actual Android baseline.
2. **Android Sensor baseline value**: `13.125.173.114`, confirmed from
   `savvy_sensor`'s own `JsonConfigDto` Java source at its pinned SHA.
3. **Fix commit** (this repo's commit `cde953b`, this session's prior
   Codex-review round, F-03): `keep_server_ip` corrected to `13.125.173.114`,
   matching this app's own actual Android baseline, with the code comment
   updated from "MGR-side default; Sensor-side differs" to "Sensor's own
   compiled Android default."

**Current final implementation values are unchanged and correct**:
`sensor_to_Linux` returns `13.125.173.114` (its own app's compiled default, as
of the F-03 fix above); the counterpart `mgr_to_Linux` returns `15.165.113.212`
(its own app's compiled default, never altered). The two applications' own
Android baselines are not unified by this or any prior round - see Blockers
below.

## Android source evidence / behavior notes

No new Android-source research was performed this round beyond the V0R-H-01
re-verification cited above (via `git show` on the pinned SHAs only). All
Phase-4 and prior-round findings (architecture correction, CRC-policy
confirmations, envelope payload-shape deviation, `DataResult` Gson-risk,
`keepServerIp` drift, the `getBytesAsLen()` serial-overflow note) remain as
previously recorded; see `git show 92142bd:SESSION_RESULT.md` on this branch
for the full prior text, not repeated verbatim here.

## Blockers and incomplete items

Per FINAL-L-01, two items previously listed here are **not Foundation code
blockers** and have been moved to their own sections instead: Android baseline
integrity (see the dedicated section near the top of this document) and the
`keepServerIp` per-application divergence (now recorded as an approved design
decision, see "Approved: keepServerIp divergence" below, not an open question).

Per TC-L-02 (this round), two further items are removed/reclassified: the
Docker Image ID discrepancy is now a closed, approved baseline (see "Docker
Linux build/test" above), not a blocker; and `contracts/bt_spp.md`/`tcp_8140.md`
are reclassified below as Wave 1 adapter-contract pending scope, not a
Foundation Gate blocker.

1. **RV1106 cross-build**: `TARGET_VALIDATION_PENDING`, blocked on B-005 - explicitly separated from, and not part of, this session's host/Docker Foundation code Gate (TC-L-02).
2. **`SO_PEERCRED`/production socket path**: explicitly `DEFERRED_PRODUCTION_SECURITY` per DEC-20260714-02.
3. **`getBytesAsLen()` serial-overflow bug**: unchanged from prior rounds - Android-side risk, FND-01 deliberately does not reproduce it, not fixed here since FND-01 never implements a concrete serial provider.
4. No `CONTRACT_CHANGE_REQUEST` filed - none of this round's fixes altered the wire contract.

**`contracts/bt_spp.md`/`tcp_8140.md`** (TC-L-02, no longer listed as a
blocker): these remain 0-byte placeholders, but are reclassified as **Wave 1
adapter-contract pending scope** - filling in the actual TCP-8140/BT-SPP wire
contracts is CC-MGR-CONNECTIVITY/CC-MGR-SERVER's objective, not an
independent Foundation Gate blocker; FND-01~04 never had a deliverable that
depended on their content.

The `DataResult` Gson item, previously tracked separately from "Blockers" as
an open pre-tag item, is now **CLOSED** this round - see "Pre-tag
compatibility evidence" below.

## Approved: keepServerIp divergence (not a blocker)

Per FINAL-L-01: `sensor_to_Linux` (`13.125.173.114`) and `mgr_to_Linux`
(`15.165.113.212`) each match their own app's real, independently-confirmed
Android compiled default (see "keepServerIp - corrected history" above for the
full history and citations). This is the **approved** per-application Android
baseline - the two values are not a unification target, this is not an open
`SCOPE_CHANGE_REQUEST` decision, and this item does not block anything.

## Pre-tag compatibility evidence - CLOSED this round

**`DataResult` Gson 2.8.2 compatibility evidence: CLOSED.** Previously
`NOT_VERIFIABLE` (prior rounds could not obtain the real `gson-2.8.2.jar`).
This round, the user obtained the exact JAR and authorized executing it
directly, outside both production repos - see "DataResult Gson 2.8.2 matrix
- full results" above for the complete 13-observation table. The
missing-key risk previously flagged (deserializing to `0`/danger instead of
the `4`/normal field initializer) is now **confirmed to actually happen**,
not merely theorized, and the codec has been updated to match this
observed behavior exactly (`DEC-20260715-DATARESULT-GSON-PARITY`) rather
than rejecting it as a parse error. This item no longer blocks the
`contract-v1` tag decision.

## Contract change

**This round**: yes, one - `contracts/json_field_policy.md` §4 was rewritten
for `DEC-20260715-DATARESULT-GSON-PARITY`, a user-approved, DataResult-ONLY
exception (documented in full above), scoped by explicit user decision
rather than requiring a separate `CONTRACT_CHANGE_REQUEST` filing (the
decision itself, made this round with the empirical evidence in hand, *is*
the equivalent authorization). No other contract file changed; `jsonConfigDto`/
`jsonDeviceDto`/the IPC envelope's existing strict policies are untouched.
FG-M-01 is a build/test-hygiene fix, not a contract change.

Prior rounds: none. No wire contract, envelope schema, or field policy was modified in those rounds - V0R-H-01's fix corrects the *implementation* to match what `contracts/ipc_action_catalog.md` already specified; F-07/V0B-H-02 are new/hardened platform-layer code, not contract changes; F-08 is test-only.

## Scope assertions

- [x] Allowed paths only (item 18 above).
- [x] No new product feature (item 20 above).
- [x] No production mock/dummy (test-only harness code lives exclusively under `tests/contract/`; the reconnect hook is a real, minimal, Wave-1-consumable interface, not a mock).
- [x] No hardware QA claim (item 21 above).
- [x] ToF/RKNN engine internals not implemented (unchanged from prior rounds).
- [x] Android MGR/Sensor repos not modified *by this session's git-tracked commits* (item 19 above) - see the dedicated integrity section for the working-tree caveat.
- [x] `contract-v1` tag not created (item 23 above).
- [x] No merges, new branches, rebases, or pushes performed.

## Next dependency / handoff

- Codex final re-verification of this round's two items (DataResult Gson
  parity, FG-M-01) and all prior rounds' fixes (TC-H-01/TC-M-01/TC-L-01/
  TC-L-02, V0R-H-01/F-07/V0B-H-02/F-08, FINAL-M-01/FINAL-M-02/FINAL-L-01).
- User review and Gate approval; `contract-v1` tag creation happens only
  after that. With this round's two items closed, no other pre-tag item is
  currently open in this document.
- Android baseline integrity recovery (see the dedicated section above) - a
  real, separate, unresolved audit item, tracked independently of this
  Foundation session's own completion (per FINAL-L-01, not a blocker on it).
- B-005 (RV1106 SDK/toolchain) resolution, whenever cross-build work is
  scheduled - target-validation-pending, separate from this session's own
  Gate.
- Wave 1 sessions (`CC-SENSOR-CORE`, `CC-SENSOR-STREAM`, and `CC-MGR-CORE`/
  `CC-MGR-SERVER` in the counterpart repo) can consume the FND-01~04
  headers/codecs, including the new `savvy_ipc_reconnect_tracker_t` hook,
  `savvy_ipc_cancel_source_t` cancellation primitive, and this round's
  DataResult Gson-parity codec. Wave 1's TCP-8140/BT-SPP adapter contract
  work (CC-MGR-CONNECTIVITY/CC-MGR-SERVER) is what will fill in `contracts/
  bt_spp.md`/`tcp_8140.md`.
