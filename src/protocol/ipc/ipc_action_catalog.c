#include "savvy/protocol/ipc_action_catalog.h"
#include "savvy/protocol/json_codec.h"
#include "cJSON.h"
#include <string.h>
#include <stddef.h>

typedef enum catalog_value_type {
    VT_OBJECT,
    VT_STRING,
    VT_NUMBER
} catalog_value_type_t;

typedef struct catalog_key {
    const char *name; /* NULL terminates the array */
    catalog_value_type_t type;
    bool required;
    /* If a present key's value is JSON null: accepted without a type
     * check when nullable, rejected otherwise. Independent of `required`
     * - required+nullable means "key must be present, value may be
     * null-or-typed"; !required+nullable (the TOF case below) means "key
     * may be absent; if present, may be null-or-typed." */
    bool nullable;
} catalog_key_t;

typedef struct catalog_entry {
    const char *action;
    savvy_ipc_direction_t direction;
    const catalog_key_t *payload_keys; /* terminated by {NULL, ...} */
} catalog_entry_t;

#define KEY_END { NULL, VT_OBJECT, false, false }

static const catalog_key_t NO_KEYS[] = { KEY_END };
static const catalog_key_t CONFIG_KEYS[] = { { "jsonConfigDto", VT_OBJECT, true, false }, KEY_END };
static const catalog_key_t DEVICE_KEYS[] = { { "jsonDeviceDto", VT_OBJECT, true, false }, KEY_END };
static const catalog_key_t TEST_KEYS[] = { { "TEST", VT_STRING, true, false }, KEY_END };
static const catalog_key_t RESET_KEYS[] = { { "RESET", VT_STRING, true, false }, KEY_END };
/* PwrLedState/AlertLedState/AlertTime/AlertSec/STATE/IFCOMM_START/
 * DELAY_SEC: Android Bundles only carry Strings across this Messenger
 * IPC (see contracts/json_field_policy.md §0 and contracts/
 * ipc_action_catalog.md's own per-action notes - "string, numeric
 * ordinal" / "strings, numeric" / "int as string" / "stringified byte" /
 * hardcoded "5"). V0R-H-01: these were wrongly declared VT_NUMBER, which
 * made the validator (and CT-IPC-001's own vectors) accept the opposite
 * of the real wire type and reject the real one. Fixed to VT_STRING. */
static const catalog_key_t LEDPWR_KEYS[] = { { "PwrLedState", VT_STRING, true, false }, KEY_END };
static const catalog_key_t ALERT_KEYS[] = {
    { "AlertLedState", VT_STRING, true, false },
    { "AlertTime", VT_STRING, true, false },
    { "AlertSec", VT_STRING, true, false },
    KEY_END
};
static const catalog_key_t RKNN_RESULT_KEYS[] = { { "rknnAnalResult", VT_STRING, true, false }, KEY_END };
static const catalog_key_t GETSTATE_KEYS[] = {
    { "SENSOR", VT_STRING, true, false },
    { "STATE", VT_STRING, true, false },
    KEY_END
};
static const catalog_key_t IFCOMM_START_KEYS[] = { { "IFCOMM_START", VT_STRING, true, false }, KEY_END };
static const catalog_key_t UPLOAD_KEYS[] = {
    { "targetFilePath", VT_STRING, true, false },
    { "targetFileNm", VT_STRING, true, false },
    KEY_END
};
static const catalog_key_t DELAY_KEYS[] = { { "DELAY_SEC", VT_STRING, true, false }, KEY_END };
static const catalog_key_t TOF_PROP_KEYS[] = {
    /* Optional AND nullable per contracts/ipc_action_catalog.md - a
     * missing key or a present JSON null are both accepted; a present
     * key must still match the declared type if it isn't null. */
    { "TofTemperature", VT_NUMBER, false, true },
    { "TofTemperDrv", VT_NUMBER, false, true },
    { "SmokeValue", VT_NUMBER, false, true },
    { "MicValue", VT_NUMBER, false, true },
    KEY_END
};
static const catalog_key_t RSLT_KEYS[] = { { "rslt", VT_STRING, true, false }, KEY_END };

/* contracts/ipc_action_catalog.md §1-2. TEST_BROADCAST_1/2 and the
 * commented-out ACTION_BROADCAST_SEVERIP variant are deliberately
 * excluded (confirmed dead/unreachable in Android source). */
static const catalog_entry_t CATALOG[] = {
    { "com.uniuni.savvysensor.config", SAVVY_IPC_MGR_TO_SENSOR, CONFIG_KEYS },
    { "com.uniuni.savvysensor.device", SAVVY_IPC_MGR_TO_SENSOR, DEVICE_KEYS },
    { "com.uniuni.savvysensor.serverip", SAVVY_IPC_MGR_TO_SENSOR, NO_KEYS },
    { "com.uniuni.savvysensor.voicestart", SAVVY_IPC_MGR_TO_SENSOR, NO_KEYS },
    { "com.uniuni.savvysensor.streamstart", SAVVY_IPC_MGR_TO_SENSOR, NO_KEYS },
    { "com.uniuni.savvysensor.test", SAVVY_IPC_MGR_TO_SENSOR, TEST_KEYS },
    { "com.uniuni.savvysensor.sensor.reset", SAVVY_IPC_MGR_TO_SENSOR, RESET_KEYS },
    { "com.uniuni.savvysensor.sensor.beaconnotify", SAVVY_IPC_MGR_TO_SENSOR, NO_KEYS },
    { "com.uniuni.savvysensor.sensor.apkupdate", SAVVY_IPC_MGR_TO_SENSOR, NO_KEYS },
    { "com.uniuni.savvysensor.sensor.status.ledpwr", SAVVY_IPC_MGR_TO_SENSOR, LEDPWR_KEYS },
    { "com.uniuni.savvysensor.sensor.status.alert", SAVVY_IPC_MGR_TO_SENSOR, ALERT_KEYS },
    { "com.uniuni.savvysensor.sensor.rknn.alert", SAVVY_IPC_MGR_TO_SENSOR, NO_KEYS },
    { "com.uniuni.savvysensor.sensor.update.threash.hold", SAVVY_IPC_MGR_TO_SENSOR, NO_KEYS },
    { "com.uniuni.savvysensor.sensor.rknn.anal.result", SAVVY_IPC_MGR_TO_SENSOR, RKNN_RESULT_KEYS },
    { "com.uniuni.savvysensor.sensor.max.cpu.temp", SAVVY_IPC_MGR_TO_SENSOR, NO_KEYS },

    { "com.uniuni.savvymgr.ipc.connect", SAVVY_IPC_SENSOR_TO_MGR, NO_KEYS },
    { "com.uniuni.savvymgr.getstate.sensor", SAVVY_IPC_SENSOR_TO_MGR, GETSTATE_KEYS },
    { "com.uniuni.savvymgr.alert.sensor", SAVVY_IPC_SENSOR_TO_MGR, IFCOMM_START_KEYS },
    { "com.uniuni.savvymgr.upload.sensor", SAVVY_IPC_SENSOR_TO_MGR, UPLOAD_KEYS },
    { "com.uniuni.savvymgr.restart.sensor", SAVVY_IPC_SENSOR_TO_MGR, DELAY_KEYS },
    { "com.uniuni.savvymgr.fracture.sensor", SAVVY_IPC_SENSOR_TO_MGR, NO_KEYS },
    { "com.uniuni.savvymgr.tof.property", SAVVY_IPC_SENSOR_TO_MGR, TOF_PROP_KEYS },
    { "com.uniuni.savvymgr.update.threash.rslt", SAVVY_IPC_SENSOR_TO_MGR, RSLT_KEYS },
};
#define N_CATALOG (sizeof(CATALOG) / sizeof(CATALOG[0]))

static const catalog_entry_t *find_entry(const char *action)
{
    if (action == NULL) {
        return NULL;
    }
    for (size_t i = 0; i < N_CATALOG; i++) {
        if (strcmp(CATALOG[i].action, action) == 0) {
            return &CATALOG[i];
        }
    }
    return NULL;
}

bool savvy_ipc_action_known(const char *action)
{
    return find_entry(action) != NULL;
}

savvy_ipc_direction_t savvy_ipc_action_direction(const char *action)
{
    const catalog_entry_t *e = find_entry(action);
    return (e != NULL) ? e->direction : SAVVY_IPC_MGR_TO_SENSOR;
}

static bool value_matches_type(const cJSON *item, catalog_value_type_t type)
{
    switch (type) {
    case VT_OBJECT: return cJSON_IsObject(item);
    case VT_STRING: return cJSON_IsString(item) && item->valuestring != NULL;
    case VT_NUMBER: {
        int32_t unused;
        return savvy_json_number_to_int32(item, &unused) == SAVVY_OK;
    }
    default: return false;
    }
}

savvy_status_t savvy_ipc_action_validate_payload(const char *action, const char *payload_json)
{
    const catalog_entry_t *e = find_entry(action);
    if (e == NULL || payload_json == NULL) {
        return SAVVY_ERR_PROTOCOL;
    }

    cJSON *root = NULL;
    savvy_status_t st = savvy_json_parse(payload_json, strlen(payload_json), &root);
    if (st != SAVVY_OK) {
        return st;
    }
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return SAVVY_ERR_PROTOCOL;
    }

    savvy_status_t result = SAVVY_OK;
    for (const catalog_key_t *key = e->payload_keys; key->name != NULL; key++) {
        cJSON *item = cJSON_GetObjectItemCaseSensitive(root, key->name);
        if (item == NULL) {
            if (key->required) {
                result = SAVVY_ERR_PROTOCOL;
                break;
            }
            continue;
        }
        if (cJSON_IsNull(item)) {
            if (key->nullable) {
                continue;
            }
            result = SAVVY_ERR_PROTOCOL;
            break;
        }
        if (!value_matches_type(item, key->type)) {
            result = SAVVY_ERR_PROTOCOL;
            break;
        }
    }

    cJSON_Delete(root);
    return result;
}
