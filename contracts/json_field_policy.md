# JSON field policy (FND-02)

This file is a **contract**, not free-form documentation (`contracts/**`
per CC-FOUNDATION.md "Allowed paths"). It is committed identically to
`mgr_to_Linux` and `sensor_to_Linux`.

## 0. Architecture correction (from Android source research, 2026-07-14)

The 26-byte packet codec (FND-01) is **not** used for MGR↔Sensor IPC.
Confirmed independently from both `savvy_mgr` and `savvy_sensor` source:

- `PacketIfCommData`/`PacketBTData` (26-byte header + CRC32) carry traffic
  only on: MGR↔remote-server TCP 8140, MGR↔BT-Client RFCOMM, and
  Sensor↔remote-server TCP 8141 (Stream/Voice).
- MGR↔Sensor is pure Android Messenger/Binder IPC: a Bundle with a string
  `action` key plus string-typed key/value payload pairs - no 26-byte
  header, no CRC, at all.

Config/Device (and, indirectly, DataResult) are JSON structures that
happen to be carried over **multiple** transports in Android (BT via
`PacketBTData`'s Data field, KeepAlive via `PacketIfCommData`'s Data
field as `DeviceInfo`/`DeviceConfig`, and MGR↔Sensor via a Bundle string
value containing the same JSON). This file fixes the Config/Device/
DataResult JSON **schema** itself (field-level); FND-01's packet codec
and FND-03's IPC envelope are separate, transport-level contracts that
both carry this JSON as their Data/payload.

**Envelope payload shape deviation (documented, not silent):** Android's
Bundle can only hold primitives/Strings, so `jsonConfigDto`/`jsonDeviceDto`
are transmitted as a **string** containing serialized JSON (double
encoding) in the real MGR↔Sensor Messenger traffic. `contracts/
mgr_sensor_ipc.schema.json` and `contracts/ipc_action_catalog.md`
instead nest these as proper JSON **objects** under `payload`. This is a
deliberate Linux-native design choice, not an oversight: 08_BLOCKERS.md
DEC-20260714-02 states this IPC's "External compatibility impact: 없음
(MGR/Sensor 내부 IPC이며 외부 프로토콜과 무관)" - the double-string-encoding
is an artifact of the Android Bundle API's limitations, not a wire
contract external parties depend on, so there is nothing to preserve
byte-for-byte here (00_SCOPE_LOCK.md 5.2: internal implementation may
change). The field-level JSON schema fixed below is unaffected either
way - only whether it arrives as a JSON object or as a JSON-string-of-that-object.

## 1. Common field policy

| 상황 | 정책 |
|---|---|
| unknown key (schema에 없는 필드) | 무시(reject하지 않음, forward-compat) + 로그 |
| missing required key | Android source에 정의된 default 값이 있으면 그 default 적용. default가 없으면 reject + 오류 기록(성공 처리 금지) |
| null value | 값이 필요한 필드에 `null`이 오면 reject(암묵적으로 0/빈 문자열로 치환하지 않는다) |
| 0 (숫자 필드의 실제 값) | missing과 구분되는 유효한 값으로 취급한다(0을 "필드 없음"으로 오인해 default로 덮어쓰지 않는다) |
| duplicate key | schema가 관리하는 모든 JSON object에서 reject한다. 적용 대상은 IPC envelope root, `payload`, `jsonConfigDto`, `jsonDeviceDto`를 포함한다. 마지막 값으로 덮어쓰기 금지. **예외: `DataResult`는 2026-07-15 `DEC-20260715-DATARESULT-GSON-PARITY`에 의해 이 규칙에서 제외된다 - 마지막 값이 승리한다. §4 참조** |

Enforcement note: duplicate-key rejection cannot be expressed as a JSON
Schema constraint over an already-parsed object (a conforming parser has
already collapsed duplicates by the time you have a tree) - it is
enforced at parse time in `src/protocol/json/json_codec.c`
(`savvy_json_check_no_duplicate_keys`, called from `savvy_json_parse`),
applied unconditionally to every parse **except** `DataResult`'s, which
uses the sibling function `savvy_json_parse_allow_duplicate_keys` (same
file) instead - see §4. `savvy_json_parse` itself is completely
unmodified by this exception; a new, separate, narrowly-scoped function
was added alongside it rather than adding a parameter/flag to the shared
parser, so no other codec's behavior can be affected.

## 2. JsonConfigDto (a.k.a. `jsonConfigDto`) - 30 fields

No field is "required" in Android source - Gson enforces nothing on any
of them (plain `new Gson()`, zero `GsonBuilder` customization, confirmed
in both `savvy_mgr` and `savvy_sensor`). Every field below therefore
follows: missing key → apply default; explicit `null` → reject; wrong
JSON type → reject; unknown extra keys → ignored.

| Field | JSON type | Default | Android source (mgr) | Android source (sensor) |
|---|---|---:|---|---|
| serverIp | string | `""` | `BT/DataObjects/JsonConfigDto.java` | `DataObjects/JsonConfigDto.java` |
| ftpIp | string | `""` | same | same |
| selectWifi | string | `""` | same | same |
| selectBeacon | string | `""` | same | same |
| videoTime | int | `4` | same | same |
| frame | int | `3` | same (⚠ Javadoc on this field says "Default = 5fps" in both apps - stale comment; code value `3` is authoritative) | same |
| dangerCount | int | `4` | same | same |
| decibel | int | `100000` | same | same (sensor's `actionConfig` treats exactly `100000` as "mic disabled" sentinel) |
| inoutTime | int | `5` | same | same |
| buzzerTime | int | `30` | same | same |
| pixelCount | int | `2000` | same | same |
| milliMeter | int | `200` | same | same |
| sendingLogTime | string | `"03:00"` | same | same |
| keepAliveTime | int | `60000` | same | same |
| **keepServerIp** | string | **`"15.165.113.212"`** | `JsonConfigDto.java` | **`"13.125.173.114"`** (⚠ see §5 drift below) |
| bootAutoTime | string | `"01:00"` | same | same |
| compress | int | `0` | same | same |
| fractureFramePixel | int | `53760` (`DEF.FRACTRUE_FRAME_PIXEL` = (int)(76800*0.7)) | same | same |
| fractureFrameCnt | int | `30` (`DEF.FRACTRUE_FRAME_CNT`) | same | same |
| resetFramePixel | int | `768` (`DEF.RESET_FRAME_PIXEL`) | same | same |
| resetFrameCnt | int | `6` (`DEF.RESET_FRAME_CNT`) | same | same |
| sameFramePixel | int | `460` (`DEF.SAME_FRAME_PIXEL`, approx.) | same | same |
| sameFrameCnt | int | `900` (`DEF.SAME_FRAME_CNT`) | same | same |
| alertSmoke | int | `0` | same | same |
| useRknn | int | `0` | same | same |
| volume | int | `80` | same | same |
| buzzerOnStream | int | `1` | same | same |
| buzzerOnVoice | int | `1` | same | same |
| buzzerOnBeacon | int | `1` | same | same |
| buzzerOnSmoke | int | `1` | same | same |

## 3. JsonDeviceDto (a.k.a. `jsonDeviceDto`) - 23 fields

Field names, types, and defaults confirmed identical between `savvy_mgr`
(`BT/DataObjects/JsonDeviceDto.java`) and `savvy_sensor`
(`DataObjects/JsonDeviceDto.java`) - no drift found for this DTO.

| Field | JSON type | Default |
|---|---|---:|
| deviceSerial | string | `""` |
| deviceMAC | string | `""` |
| deviceIp | string | `""` |
| btName | string | `""` |
| blueTooth | int | `0` |
| mic | int | `0` |
| wifi | int | `0` |
| tof | int | `0` |
| led | int | `0` |
| buzzer | int | `0` |
| moveSensor | int | `0` |
| reboot | int | `0` |
| appMgr | string | `""` |
| appSensor | string | `""` |
| os | string | `""` |
| t_name | string | `""` |
| toilet | int | `0` |
| stall | int | `0` |
| beacon | int | `0` |
| appRknn | string | `""` |
| verRknn | string | `""` |
| smokeValue | int | `0` |
| dataCollection | int | `0` |

Sensor-side note (`MainActivity.java` startup, `setJsonDeviceDto(true)`):
on Sensor's own process startup only (not on every parse), a subset of
these fields (blueTooth/mic/wifi/tof/led/buzzer/moveSensor/beacon/reboot)
is explicitly reset to `0` regardless of the cached/parsed value, since
"Device 상태 정보는 MGR에서 내려받는 정보"; this is Wave 1 (CC-SENSOR-CORE
SNC-01) lifecycle behavior, not a codec-level field-default rule, and is
recorded here only for cross-reference.

## 4. DataResult - 1 field (Sensor-side only)

**Not present in `savvy_mgr` at all** - confirmed by exhaustive grep (no
class or JSON key named `DataResult`, no `==4`/`!=4` comparison on any
result-like field anywhere in that repo). This DTO exists only on the
`savvy_sensor` side, decoded from the Data field of packets received on
the Stream/Voice TCP 8141 channel (FND-01 packet codec, not the
MGR-Sensor IPC envelope). This codec is still built and tested
identically in both `mgr_to_Linux` and `sensor_to_Linux` (shared/common
code layer), even though only Sensor's business logic exercises it.

### 4.1 `DEC-20260715-DATARESULT-GSON-PARITY` (supersedes the pre-2026-07-15 policy below)

2026-07-15: the user obtained the real `gson-2.8.2.jar`
(SHA-256 `b7134929f7cc7c04021ec1cc27ef63ab907e410cf0588e397b8851181eb91092`)
and authorized executing it directly, outside both production repos,
against a class structurally identical to Android's `DataResult`
(`int result = 4;` field initializer, only constructor `DataResult(int)`,
no no-arg constructor, plain `new Gson()`, zero customization - matching
`IfComm/DataObjects/DataResult.java` exactly). This closes the
uncertainty §4.2/§5(old) previously flagged as "not independently
verified by execution" - the behavior below is **observed**, not
theorized.

**Original 7-case matrix** (artifact:
`SAVVY_migration_control_v1.0/artifacts/gson/2.8.2/dataresult-matrix/`,
`DataResultGsonMatrix.java` / `DataResult_Gson_2.8.2_matrix.log`):

| JSON | Result |
|---|---|
| `{"result":4}` | SUCCESS, result=4 |
| `{"result":7}` | SUCCESS, result=7 |
| `{}` | SUCCESS, result=**0** (field initializer bypassed - unsafe allocation confirmed) |
| `{"result":null}` | SUCCESS, result=**0** |
| `{"result":"4"}` | SUCCESS, result=**4** (numeric string coerced like an unquoted number) |
| `{"result":4.9}` | FAIL - `JsonSyntaxException`/`NumberFormatException`: "Expected an int but was 4.9" |
| `{"result":4,"result":7}` | SUCCESS, result=**7** (duplicate key, last value wins) |

**G001 additional 5-case matrix** (this round, same JAR, same class
shape, run outside both production repos; artifact:
`DataResultGsonMatrixAdditional.java` /
`DataResult_Gson_2.8.2_matrix_additional.log`), fixing the int32-range /
fractional-string / nonnumeric-string sub-policy the original matrix left
open:

| JSON | Result |
|---|---|
| `{"result":"-1"}` | SUCCESS, result=-1 |
| `{"result":"2147483647"}` (INT32_MAX) | SUCCESS, result=2147483647 |
| `{"result":"2147483648"}` (INT32_MAX+1) | FAIL - "Expected an int but was 2147483648" |
| `{"result":"4.9"}` | FAIL - "Expected an int but was 4.9" |
| `{"result":"abc"}` | FAIL - `NumberFormatException`: "For input string: \"abc\"" |

**Supplementary confirmatory case** (1 case, same JAR/class, added this
round to remove an inference gap rather than assume symmetry; artifact:
`DataResultGsonMatrixSupplementary.java` /
`DataResult_Gson_2.8.2_matrix_supplementary.log`):

| JSON | Result |
|---|---|
| `{"result":2147483648}` (unquoted, INT32_MAX+1) | FAIL - "Expected an int but was 2147483648" (identical error format to the quoted-string overflow case above - confirms Gson routes a quoted numeric string through the exact same numeric-token parser/range-check as an unquoted number, not a separate code path) |

**Fixed policy for `DataResult.result`** (derived only from the 13
observations above - per explicit instruction, not extrapolated/guessed
beyond them):

| Input | Outcome |
|---|---|
| JSON integer, `[INT32_MIN, INT32_MAX]` | success, that value |
| JSON integer, fractional / non-finite / out of int32 range | parse error (never truncated/clamped/wrapped) |
| missing key | success, result=**0** |
| JSON `null` | success, result=**0** |
| JSON string, integer format (`-?[0-9]+`), value fits int32 | success, parsed as that int |
| JSON string, integer format, value out of int32 range | parse error |
| JSON string, fractional format (e.g. `"4.9"`) | parse error |
| JSON string, non-numeric (e.g. `"abc"`) | parse error |
| JSON string, any other lexical form not listed above (leading `+`, whitespace, exponent notation, ...) | parse error - **not tested against the real JAR; this codec does not extend the accepted grammar by guessing**, so anything outside the minimal grammar above is rejected rather than assumed-accepted |
| JSON bool / array / object | parse error (Gson throws for a type-incompatible target field; not independently re-verified since no coercion path exists for these types) |
| duplicate `result` key | success, **last** occurrence wins (only schema-managed object in this codebase with this exception - §1) |
| invalid UTF-8 anywhere in the JSON text | parse error (unchanged - orthogonal to this exception) |
| non-object root | parse error (unchanged) |

`result == 4` is normal (`isChecked()` returns `false`); every other
successfully-parsed integer value (including 0, negative values, and any
value ≠ 4) is non-normal/danger (`isChecked()` returns `true`) - K-001,
unconditional on the integer value.

**Scope**: this is a **DataResult-ONLY** exception. `jsonConfigDto`,
`jsonDeviceDto`, the IPC envelope root, `payload`, and every other
schema-managed JSON object keep their existing strict policy (duplicate-
key reject, required-`null` reject, existing type policy) completely
unchanged - see §1's table and its enforcement note. The shared/common
parser `savvy_json_parse` (`src/protocol/json/json_codec.c`) is itself
untouched; the exception is implemented via a separate sibling function
`savvy_json_parse_allow_duplicate_keys`, called only from
`src/protocol/json/data_result_codec.c`.

### 4.2 Pre-2026-07-15 policy (superseded, kept for history only)

Before `DEC-20260715-DATARESULT-GSON-PARITY`, missing/null/duplicate/
string-typed `result` were all treated as parse errors (the same strict
policy as every other schema object), because the Gson unsafe-allocation
behavior above was "known library behavior, not independently
re-verified by execution" and the ambiguity was resolved in favor of the
safer (stricter) of the two candidates rather than guessing. §4.1 above
is the current, empirically-verified policy and supersedes this
paragraph entirely; it is kept only so the history of why the policy
changed is not lost.

## 5. Discovered drift requiring attention (not resolved here)

1. **`keepServerIp` default value differs between apps** for the
   identical field name/JSON key: MGR's `JsonConfigDto` defaults to
   `15.165.113.212`; Sensor's `JsonConfigDto` defaults to
   `13.125.173.114`. Both are independently-duplicated POJOs (no shared
   library dependency in either `build.gradle`), so this is plausibly an
   unintentional drift between the two codebases rather than a deliberate
   difference. `savvy_config_set_defaults()` matches **each repo's own
   app**: `mgr_to_Linux`'s `savvy_config_set_defaults()` returns
   `15.165.113.212` (MGR's own compiled default) and
   `sensor_to_Linux`'s returns `13.125.173.114` (Sensor's own compiled
   default) - a deliberate, documented per-repo divergence, not a shared
   Foundation-wide default and not a bug. This does not resolve which
   value is "correct" for production if the two apps are ever meant to
   agree - that decision belongs to whoever owns `08_BLOCKERS.md`/
   `SCOPE_CHANGE_REQUEST`, not to this Foundation session.
2. **`DataResult` Gson-unsafe-allocation risk** (§4) - **RESOLVED
   2026-07-15** by `DEC-20260715-DATARESULT-GSON-PARITY` (§4.1): executed
   the real `gson-2.8.2.jar` directly (not a device/emulator, but the
   actual production Gson version/bytecode) and confirmed missing-key
   unsafe-allocation does happen and does yield `result=0`. No longer an
   open risk - the codec now matches this behavior exactly rather than
   rejecting it.
3. **CRC-mismatch on Sensor's 8141 inbound is swallowed silently** (no
   NAK surfaced; `MSG_CMD.REASON_NAK_CRC_ERROR` is declared but never
   assigned/returned anywhere in `savvy_sensor`) - relevant to FND-01's
   channel CRC policy table, not this file's JSON policy, but recorded
   here since it was discovered during the same research pass. Foundation
   does not attempt to fix or reproduce this by itself (channel/business
   logic is Wave 1's domain); flagged for SESSION_RESULT.md.
