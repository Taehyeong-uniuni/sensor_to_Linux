# MGR-Sensor IPC action catalog (FND-03)

Contract file (`contracts/**`), committed identically to `mgr_to_Linux`
and `sensor_to_Linux`. Fixes the action/key catalog objective from
CC-FOUNDATION.md ("MGR–Sensor action catalog, direction, key, type,
replay/drop matrix를 고정한다").

## 0. Transport correction

Real Android MGR↔Sensor IPC is Messenger/Binder: MGR hosts
`MessengerService` (exported), Sensor binds to it. Every message is
`Message.what ∈ {IPC_MSG_MGR_TO_SENSOR=1, IPC_MSG_SENSOR_TO_MGR=2}`
carrying a `Bundle` with a string `action` key (`BUNDLE_ARG_NAME`) plus
string-typed payload key/value pairs - there is no 26-byte packet header
on this channel (see `contracts/json_field_policy.md` §0). The Linux port
(FND-03) replaces this with `AF_UNIX SOCK_SEQPACKET` + the JSON envelope
in `contracts/mgr_sensor_ipc.schema.json`; action names and payload key
names below are preserved from Android, payload values are carried as
proper nested JSON (not double-string-encoded) per the documented
deviation in `json_field_policy.md` §0.

An older Intent-broadcast mechanism (`sendToMgrBroadcast`/
`sendToSensorBroadcast`, `ACTION_BROADCAST_*` naming holdover) exists in
both source trees but has **zero live call sites** in either app
(every call site is commented out) - confirmed dead code, not part of
this catalog.

## 1. MGR → Sensor

| Action constant | Wire string | Payload keys | Notes |
|---|---|---|---|
| ACTION_BROADCAST_CONFIG | `com.uniuni.savvysensor.config` | `jsonConfigDto` (object) | Sent on Config change and on Sensor reconnect (see §3). Sensor persists + diffs old vs. new. |
| ACTION_BROADCAST_DEVICE | `com.uniuni.savvysensor.device` | `jsonDeviceDto` (object) | Sent on Device change and on Sensor reconnect (see §3). |
| ACTION_BROADCAST_SEVERIP | `com.uniuni.savvysensor.serverip` | none | Sensor's handler (`actionServerip`) is a no-op stub (log line only) - included because it is a real wire action, not because it currently does anything. |
| ACTION_BROADCAST_VOICE_START | `com.uniuni.savvysensor.voicestart` | none | Starts Sensor's Voice `ClientChannel`. |
| ACTION_BROADCAST_STREAM_START | `com.uniuni.savvysensor.streamstart` | none | Starts Sensor's Stream `ClientChannel`. |
| ACTION_BROADCAST_TEST | `com.uniuni.savvysensor.test` | `TEST` (string) | Display-only on Sensor side. |
| ACTION_BROADCAST_SENSOR_RESET | `com.uniuni.savvysensor.sensor.reset` | `RESET` (string; one of `DEF.SENSOR_TYPE`: LED/BUZZER/TOF/MIC) | LED/BUZZER/TOF branches are empty stubs; MIC branch's call is commented out - currently all no-op on Sensor side. Included as a real wire action regardless. |
| ACTION_BROADCAST_BEACON_NOTIFY | `com.uniuni.savvysensor.sensor.beaconnotify` | none | Sensor updates a display field only; payload (if any) not read. |
| ACTION_BROADCAST_APK_UPDATE | `com.uniuni.savvysensor.sensor.apkupdate` | none | Sets Sensor's `mIsApkUpdate = true` (update guard - see SNC-04). |
| ACTION_BROADCAST_STATUS_LED_PWR | `com.uniuni.savvysensor.sensor.status.ledpwr` | `PwrLedState` (string, numeric ordinal) | Parse failure/-1 → `PwrLedState.PWR_NONE` on Sensor side. Replayed on reconnect (§3). |
| ACTION_BROADCAST_STATUS_ALERT | `com.uniuni.savvysensor.sensor.status.alert` | `AlertLedState`, `AlertTime`, `AlertSec` (strings, numeric) | Parse failures default to `ALERT_NONE`/`0`/`0` on Sensor side. Replayed on reconnect (§3). |
| ACTION_BROADCAST_RKNN_ALERT | `com.uniuni.savvysensor.sensor.rknn.alert` | none | Conditional on Sensor's `useRknn`/`dataCollection` state - relays a saved ToF frame to the Stream server. |
| ACTION_BROADCAST_UPDATE_THREASH_HOLD_DATA | `com.uniuni.savvysensor.sensor.update.threash.hold` | none | Triggers Sensor→RKNN UDS request; Sensor replies with `UPDATE_THREASH_HOLD_BROADCAST_RSLT` (§2). |
| ACTION_BROADCAST_RKNN_ANAL_RESULT | `com.uniuni.savvysensor.sensor.rknn.anal.result` | `rknnAnalResult` (string) | Conditional on `useRknn`; wraps the string into a 26-byte IfComm packet (Start='T', Command='S') sent over the Stream TCP channel - crosses into FND-01 territory, not this IPC envelope, once relayed. |
| ACTION_BROADCAST_MAX_CPU_TEMP | `com.uniuni.savvysensor.sensor.max.cpu.temp` | none | Conditional on ToF being in use. |

**Excluded from this catalog (confirmed dead/unreachable, not ported):**
`ACTION_BROADCAST_SEVERIP`'s commented-out declaration variant and
`TEST_BROADCAST_1`/`TEST_BROADCAST_2` on the MGR side - the latter two are
only triggered from `MainActivity.onClick`, but `MainActivity`'s view
binding is never inflated (`setContentView` calls are commented out and
`onCreate` calls `finish()` unconditionally) and no `BroadcastReceiver`
routes those two action strings by any other path - unreachable in the
current build.

## 2. Sensor → MGR

| Action constant | Wire string | Payload keys | Trigger |
|---|---|---|---|
| CONNECT_BROADCAST_IPC | `com.uniuni.savvymgr.ipc.connect` | none | Every successful bind/re-bind - the *only* thing Sensor sends on (re)connect. Triggers MGR's replay (§3). |
| GETSTATE_BROADCAST_SENSOR | `com.uniuni.savvymgr.getstate.sensor` | `SENSOR` (SENSOR_TYPE string), `STATE` (int as string) | Only when a tracked sensor state actually changes. |
| ALERT_BROADCAST_SENSOR | `com.uniuni.savvymgr.alert.sensor` | `IFCOMM_START` (stringified byte) | Stream danger-count-threshold trip, or any Voice danger result (see FND-01 CRC-policy work; Stream/Voice result semantics belong to CC-SENSOR-STREAM SNS-03). |
| UPLOAD_BROADCAST_SENSOR | `com.uniuni.savvymgr.upload.sensor` | `targetFilePath`, `targetFileNm` (=deviceSerial) | Guarded by Sensor having a cached Device. |
| RESTART_BROADCAST_SENSOR | `com.uniuni.savvymgr.restart.sensor` | `DELAY_SEC` (hardcoded `"5"`) | |
| FRACTURE_BROADCAST_SENSOR | `com.uniuni.savvymgr.fracture.sensor` | none | |
| PROPERTY_BROADCAST_TOF | `com.uniuni.savvymgr.tof.property` | `TofTemperature`, `TofTemperDrv`, `SmokeValue`, `MicValue` (optional/nullable) | |
| UPDATE_THREASH_HOLD_BROADCAST_RSLT | `com.uniuni.savvymgr.update.threash.rslt` | `rslt` (`"True"`/`"False"`) | Reply to MGR's `ACTION_BROADCAST_UPDATE_THREASH_HOLD_DATA`. |

## 3. Reconnect / replay matrix

Confirmed identically from both sides of the channel (MGR's send logic
and Sensor's receive logic independently agree):

- **Pre-connect send: dropped, never queued.** MGR's
  `MessengerService.sendIpcMessage()` only sends `if (gReplyTo != null)` -
  no buffering exists. This matches FND-03's transport primitive design:
  there is structurally no `savvy_ipc_transport_t` handle to send through
  until `accept()`/`connect()` succeeds (see `savvy_ipc_server_accept`/
  `savvy_ipc_client_connect`).
- **On Sensor's `CONNECT_BROADCAST_IPC`, MGR replays exactly four
  actions** (`SavvyService.actionConnectIpc()`): `ACTION_BROADCAST_CONFIG`,
  `ACTION_BROADCAST_DEVICE`, `ACTION_BROADCAST_STATUS_ALERT`,
  `ACTION_BROADCAST_STATUS_LED_PWR` - more than the "Config/Device"
  wording used elsewhere in the control docs; this is the precise,
  source-confirmed list. Everything else that happens to fire while
  Sensor is disconnected is simply lost, with no recovery.
- **Sensor does not request or expect an explicit resync** - it has no
  outbound action for "please resend config." It also does not reload
  its own cached Config/Device from SharedPreferences on IPC (re)connect;
  it keeps using whatever is already in memory and simply announces
  `CONNECT_BROADCAST_IPC`.
- **No automatic IPC re-bind/retry is authored in Sensor** - reconnection
  (if any) depends entirely on Android's OS-level `bindService`/
  `ServiceConnection` semantics, which has no direct Linux-daemon
  equivalent. This is a Wave 1 (CC-SENSOR-CORE) policy decision, not a
  Foundation transport concern - FND-03 only guarantees that `connect()`/
  `accept()` can be called again after a disconnect (verified by
  CT-IPC-002's reconnect scenario).

## 4. Verified by

CT-IPC-001 (this catalog's conformance - normal messages for real
actions parse with the documented payload keys; payload type mismatches,
missing `action`, and duplicate keys are rejected) and CT-IPC-002
(connect/disconnect/reconnect/resync-capability, action-agnostic).
