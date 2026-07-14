#include "savvy/protocol/config_codec.h"
#include "savvy/protocol/json_codec.h"
#include "field_table.h"
#include "cJSON.h"
#include <string.h>
#include <stdlib.h>

/* JSON keys are the exact Android field names (camelCase) - see
 * contracts/json_field_policy.md §2. */
static const savvy_field_desc_t CONFIG_FIELDS[] = {
    { "serverIp", SAVVY_FIELD_STRING, offsetof(savvy_config_t, server_ip), SAVVY_CONFIG_STR_LEN },
    { "ftpIp", SAVVY_FIELD_STRING, offsetof(savvy_config_t, ftp_ip), SAVVY_CONFIG_STR_LEN },
    { "selectWifi", SAVVY_FIELD_STRING, offsetof(savvy_config_t, select_wifi), SAVVY_CONFIG_STR_LEN },
    { "selectBeacon", SAVVY_FIELD_STRING, offsetof(savvy_config_t, select_beacon), SAVVY_CONFIG_STR_LEN },
    { "videoTime", SAVVY_FIELD_INT32, offsetof(savvy_config_t, video_time), 0 },
    { "frame", SAVVY_FIELD_INT32, offsetof(savvy_config_t, frame), 0 },
    { "dangerCount", SAVVY_FIELD_INT32, offsetof(savvy_config_t, danger_count), 0 },
    { "decibel", SAVVY_FIELD_INT32, offsetof(savvy_config_t, decibel), 0 },
    { "inoutTime", SAVVY_FIELD_INT32, offsetof(savvy_config_t, inout_time), 0 },
    { "buzzerTime", SAVVY_FIELD_INT32, offsetof(savvy_config_t, buzzer_time), 0 },
    { "pixelCount", SAVVY_FIELD_INT32, offsetof(savvy_config_t, pixel_count), 0 },
    { "milliMeter", SAVVY_FIELD_INT32, offsetof(savvy_config_t, milli_meter), 0 },
    { "sendingLogTime", SAVVY_FIELD_STRING, offsetof(savvy_config_t, sending_log_time), SAVVY_CONFIG_STR_LEN },
    { "keepAliveTime", SAVVY_FIELD_INT32, offsetof(savvy_config_t, keep_alive_time), 0 },
    { "keepServerIp", SAVVY_FIELD_STRING, offsetof(savvy_config_t, keep_server_ip), SAVVY_CONFIG_STR_LEN },
    { "bootAutoTime", SAVVY_FIELD_STRING, offsetof(savvy_config_t, boot_auto_time), SAVVY_CONFIG_STR_LEN },
    { "compress", SAVVY_FIELD_INT32, offsetof(savvy_config_t, compress), 0 },
    { "fractureFramePixel", SAVVY_FIELD_INT32, offsetof(savvy_config_t, fracture_frame_pixel), 0 },
    { "fractureFrameCnt", SAVVY_FIELD_INT32, offsetof(savvy_config_t, fracture_frame_cnt), 0 },
    { "resetFramePixel", SAVVY_FIELD_INT32, offsetof(savvy_config_t, reset_frame_pixel), 0 },
    { "resetFrameCnt", SAVVY_FIELD_INT32, offsetof(savvy_config_t, reset_frame_cnt), 0 },
    { "sameFramePixel", SAVVY_FIELD_INT32, offsetof(savvy_config_t, same_frame_pixel), 0 },
    { "sameFrameCnt", SAVVY_FIELD_INT32, offsetof(savvy_config_t, same_frame_cnt), 0 },
    { "alertSmoke", SAVVY_FIELD_INT32, offsetof(savvy_config_t, alert_smoke), 0 },
    { "useRknn", SAVVY_FIELD_INT32, offsetof(savvy_config_t, use_rknn), 0 },
    { "volume", SAVVY_FIELD_INT32, offsetof(savvy_config_t, volume), 0 },
    { "buzzerOnStream", SAVVY_FIELD_INT32, offsetof(savvy_config_t, buzzer_on_stream), 0 },
    { "buzzerOnVoice", SAVVY_FIELD_INT32, offsetof(savvy_config_t, buzzer_on_voice), 0 },
    { "buzzerOnBeacon", SAVVY_FIELD_INT32, offsetof(savvy_config_t, buzzer_on_beacon), 0 },
    { "buzzerOnSmoke", SAVVY_FIELD_INT32, offsetof(savvy_config_t, buzzer_on_smoke), 0 },
};
#define N_CONFIG_FIELDS (sizeof(CONFIG_FIELDS) / sizeof(CONFIG_FIELDS[0]))

void savvy_config_set_defaults(savvy_config_t *out)
{
    memset(out, 0, sizeof(*out));
    strcpy(out->server_ip, "");
    strcpy(out->ftp_ip, "");
    strcpy(out->select_wifi, "");
    strcpy(out->select_beacon, "");
    out->video_time = 4;
    out->frame = 3;
    out->danger_count = 4;
    out->decibel = 100000;
    out->inout_time = 5;
    out->buzzer_time = 30;
    out->pixel_count = 2000;
    out->milli_meter = 200;
    strcpy(out->sending_log_time, "03:00");
    out->keep_alive_time = 60000;
    /* MGR-side default; Sensor-side Android default differs
     * (13.125.173.114) - see contracts/json_field_policy.md §5 drift #1. */
    strcpy(out->keep_server_ip, "15.165.113.212");
    strcpy(out->boot_auto_time, "01:00");
    out->compress = 0;
    out->fracture_frame_pixel = 53760;
    out->fracture_frame_cnt = 30;
    out->reset_frame_pixel = 768;
    out->reset_frame_cnt = 6;
    out->same_frame_pixel = 460;
    out->same_frame_cnt = 900;
    out->alert_smoke = 0;
    out->use_rknn = 0;
    out->volume = 80;
    out->buzzer_on_stream = 1;
    out->buzzer_on_voice = 1;
    out->buzzer_on_beacon = 1;
    out->buzzer_on_smoke = 1;
}

savvy_status_t savvy_config_parse(const char *json, savvy_config_t *out)
{
    if (json == NULL || out == NULL) {
        return SAVVY_ERR_INVALID_ARGUMENT;
    }

    cJSON *root = NULL;
    savvy_status_t st = savvy_json_parse(json, strlen(json), &root);
    if (st != SAVVY_OK) {
        return st;
    }

    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return SAVVY_ERR_PROTOCOL;
    }

    st = savvy_apply_field_table(root, CONFIG_FIELDS, N_CONFIG_FIELDS, out);
    cJSON_Delete(root);
    return st;
}

savvy_status_t savvy_config_build(const savvy_config_t *cfg, char **out_json)
{
    if (cfg == NULL || out_json == NULL) {
        return SAVVY_ERR_INVALID_ARGUMENT;
    }

    cJSON *root = savvy_build_field_table(CONFIG_FIELDS, N_CONFIG_FIELDS, cfg);
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
