#include "savvy/protocol/ipc_action_catalog.h"
#include "savvy/protocol/json_codec.h"
#include "cJSON.h"
#include <string.h>
#include <stddef.h>

typedef struct catalog_entry {
    const char *action;
    savvy_ipc_direction_t direction;
    const char *const *payload_keys; /* NULL-terminated */
} catalog_entry_t;

static const char *const NO_KEYS[] = { NULL };
static const char *const CONFIG_KEYS[] = { "jsonConfigDto", NULL };
static const char *const DEVICE_KEYS[] = { "jsonDeviceDto", NULL };
static const char *const TEST_KEYS[] = { "TEST", NULL };
static const char *const RESET_KEYS[] = { "RESET", NULL };
static const char *const LEDPWR_KEYS[] = { "PwrLedState", NULL };
static const char *const ALERT_KEYS[] = { "AlertLedState", "AlertTime", "AlertSec", NULL };
static const char *const RKNN_RESULT_KEYS[] = { "rknnAnalResult", NULL };
static const char *const GETSTATE_KEYS[] = { "SENSOR", "STATE", NULL };
static const char *const IFCOMM_START_KEYS[] = { "IFCOMM_START", NULL };
static const char *const UPLOAD_KEYS[] = { "targetFilePath", "targetFileNm", NULL };
static const char *const DELAY_KEYS[] = { "DELAY_SEC", NULL };
static const char *const TOF_PROP_KEYS[] = { "TofTemperature", "TofTemperDrv", "SmokeValue", "MicValue", NULL };
static const char *const RSLT_KEYS[] = { "rslt", NULL };

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
    for (const char *const *key = e->payload_keys; *key != NULL; key++) {
        if (cJSON_GetObjectItemCaseSensitive(root, *key) == NULL) {
            result = SAVVY_ERR_PROTOCOL;
            break;
        }
    }

    cJSON_Delete(root);
    return result;
}
