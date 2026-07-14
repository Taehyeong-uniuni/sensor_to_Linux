#ifndef SAVVY_PROTOCOL_CONFIG_CODEC_H
#define SAVVY_PROTOCOL_CONFIG_CODEC_H

#include <stdint.h>
#include "savvy/core/error.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Generous fixed bound for every string field below (longest observed
 * real value is a 14-char IP address); parse rejects rather than
 * silently truncates any value that would not fit. */
#define SAVVY_CONFIG_STR_LEN 64

/* jsonConfigDto (contracts/json_field_policy.md §2) - field order and
 * names mirror the Android DTO; C field names are the JSON key in
 * snake_case. */
typedef struct savvy_config {
    char server_ip[SAVVY_CONFIG_STR_LEN];
    char ftp_ip[SAVVY_CONFIG_STR_LEN];
    char select_wifi[SAVVY_CONFIG_STR_LEN];
    char select_beacon[SAVVY_CONFIG_STR_LEN];
    int32_t video_time;
    int32_t frame;
    int32_t danger_count;
    int32_t decibel;
    int32_t inout_time;
    int32_t buzzer_time;
    int32_t pixel_count;
    int32_t milli_meter;
    char sending_log_time[SAVVY_CONFIG_STR_LEN];
    int32_t keep_alive_time;
    char keep_server_ip[SAVVY_CONFIG_STR_LEN];
    char boot_auto_time[SAVVY_CONFIG_STR_LEN];
    int32_t compress;
    int32_t fracture_frame_pixel;
    int32_t fracture_frame_cnt;
    int32_t reset_frame_pixel;
    int32_t reset_frame_cnt;
    int32_t same_frame_pixel;
    int32_t same_frame_cnt;
    int32_t alert_smoke;
    int32_t use_rknn;
    int32_t volume;
    int32_t buzzer_on_stream;
    int32_t buzzer_on_voice;
    int32_t buzzer_on_beacon;
    int32_t buzzer_on_smoke;
} savvy_config_t;

/* Populates *out with the Android source defaults (JsonConfigDto Java
 * field initializers). keep_server_ip uses the MGR-side default
 * (15.165.113.212) - MGR's and Sensor's compiled-in defaults for this one
 * field genuinely differ in Android source (see contracts/
 * json_field_policy.md §5 drift #1); callers needing the Sensor-side
 * default must override keep_server_ip explicitly after calling this. */
void savvy_config_set_defaults(savvy_config_t *out);

/* Parses `json` (NUL-terminated) into *out, which must already be
 * initialized (typically via savvy_config_set_defaults()). Missing keys
 * leave *out's existing value untouched. Rejects (SAVVY_ERR_PROTOCOL):
 * JSON null for any known field, wrong JSON type for any known field, a
 * string value too long for its fixed buffer, and any duplicate key
 * anywhere in the tree. Unknown extra keys are ignored. */
savvy_status_t savvy_config_parse(const char *json, savvy_config_t *out);

/* Builds *cfg into a newly malloc'd, NUL-terminated JSON string
 * (*out_json; caller frees with free()). */
savvy_status_t savvy_config_build(const savvy_config_t *cfg, char **out_json);

#ifdef __cplusplus
}
#endif

#endif
