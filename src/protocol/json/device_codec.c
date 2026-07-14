#include "savvy/protocol/device_codec.h"
#include "savvy/protocol/json_codec.h"
#include "field_table.h"
#include "cJSON.h"
#include <string.h>
#include <stdlib.h>

/* JSON keys are the exact Android field names - see
 * contracts/json_field_policy.md §3 (identical between mgr/sensor). */
static const savvy_field_desc_t DEVICE_FIELDS[] = {
    { "deviceSerial", SAVVY_FIELD_STRING, offsetof(savvy_device_t, device_serial), SAVVY_DEVICE_STR_LEN },
    { "deviceMAC", SAVVY_FIELD_STRING, offsetof(savvy_device_t, device_mac), SAVVY_DEVICE_STR_LEN },
    { "deviceIp", SAVVY_FIELD_STRING, offsetof(savvy_device_t, device_ip), SAVVY_DEVICE_STR_LEN },
    { "btName", SAVVY_FIELD_STRING, offsetof(savvy_device_t, bt_name), SAVVY_DEVICE_STR_LEN },
    { "blueTooth", SAVVY_FIELD_INT32, offsetof(savvy_device_t, blue_tooth), 0 },
    { "mic", SAVVY_FIELD_INT32, offsetof(savvy_device_t, mic), 0 },
    { "wifi", SAVVY_FIELD_INT32, offsetof(savvy_device_t, wifi), 0 },
    { "tof", SAVVY_FIELD_INT32, offsetof(savvy_device_t, tof), 0 },
    { "led", SAVVY_FIELD_INT32, offsetof(savvy_device_t, led), 0 },
    { "buzzer", SAVVY_FIELD_INT32, offsetof(savvy_device_t, buzzer), 0 },
    { "moveSensor", SAVVY_FIELD_INT32, offsetof(savvy_device_t, move_sensor), 0 },
    { "reboot", SAVVY_FIELD_INT32, offsetof(savvy_device_t, reboot), 0 },
    { "appMgr", SAVVY_FIELD_STRING, offsetof(savvy_device_t, app_mgr), SAVVY_DEVICE_STR_LEN },
    { "appSensor", SAVVY_FIELD_STRING, offsetof(savvy_device_t, app_sensor), SAVVY_DEVICE_STR_LEN },
    { "os", SAVVY_FIELD_STRING, offsetof(savvy_device_t, os), SAVVY_DEVICE_STR_LEN },
    { "t_name", SAVVY_FIELD_STRING, offsetof(savvy_device_t, t_name), SAVVY_DEVICE_STR_LEN },
    { "toilet", SAVVY_FIELD_INT32, offsetof(savvy_device_t, toilet), 0 },
    { "stall", SAVVY_FIELD_INT32, offsetof(savvy_device_t, stall), 0 },
    { "beacon", SAVVY_FIELD_INT32, offsetof(savvy_device_t, beacon), 0 },
    { "appRknn", SAVVY_FIELD_STRING, offsetof(savvy_device_t, app_rknn), SAVVY_DEVICE_STR_LEN },
    { "verRknn", SAVVY_FIELD_STRING, offsetof(savvy_device_t, ver_rknn), SAVVY_DEVICE_STR_LEN },
    { "smokeValue", SAVVY_FIELD_INT32, offsetof(savvy_device_t, smoke_value), 0 },
    { "dataCollection", SAVVY_FIELD_INT32, offsetof(savvy_device_t, data_collection), 0 },
};
#define N_DEVICE_FIELDS (sizeof(DEVICE_FIELDS) / sizeof(DEVICE_FIELDS[0]))

void savvy_device_set_defaults(savvy_device_t *out)
{
    memset(out, 0, sizeof(*out));
    /* Every string field defaults to "" and every int field to 0 in
     * Android source (JsonDeviceDto, identical in mgr/sensor); memset(0)
     * already yields exactly that (empty NUL-terminated strings, 0 ints). */
}

savvy_status_t savvy_device_parse(const char *json, size_t len, savvy_device_t *out,
                                   void (*unknown_key_log_fn)(const char *object_name, const char *key_name))
{
    if (json == NULL || out == NULL) {
        return SAVVY_ERR_INVALID_ARGUMENT;
    }

    cJSON *root = NULL;
    savvy_status_t st = savvy_json_parse(json, len, &root);
    if (st != SAVVY_OK) {
        return st;
    }

    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return SAVVY_ERR_PROTOCOL;
    }

    st = savvy_apply_field_table(root, DEVICE_FIELDS, N_DEVICE_FIELDS, out, "jsonDeviceDto", unknown_key_log_fn);
    cJSON_Delete(root);
    return st;
}

savvy_status_t savvy_device_build(const savvy_device_t *dev, char **out_json)
{
    if (dev == NULL || out_json == NULL) {
        return SAVVY_ERR_INVALID_ARGUMENT;
    }

    cJSON *root = savvy_build_field_table(DEVICE_FIELDS, N_DEVICE_FIELDS, dev);
    if (root == NULL) {
        return SAVVY_ERR_OUT_OF_MEMORY;
    }

    char *text = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (text == NULL) {
        return SAVVY_ERR_OUT_OF_MEMORY;
    }

    *out_json = text;
    return SAVVY_OK;
}
