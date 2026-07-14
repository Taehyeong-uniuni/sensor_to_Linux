#ifndef SAVVY_PROTOCOL_DEVICE_CODEC_H
#define SAVVY_PROTOCOL_DEVICE_CODEC_H

#include <stddef.h>
#include <stdint.h>
#include "savvy/core/error.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Generous fixed bound for every string field below; parse rejects
 * rather than silently truncates any value that would not fit. */
#define SAVVY_DEVICE_STR_LEN 64

/* jsonDeviceDto (contracts/json_field_policy.md §3) - field order and
 * names mirror the Android DTO (identical between mgr/sensor, no drift
 * found); C field names are the JSON key in snake_case. */
typedef struct savvy_device {
    char device_serial[SAVVY_DEVICE_STR_LEN];
    char device_mac[SAVVY_DEVICE_STR_LEN];
    char device_ip[SAVVY_DEVICE_STR_LEN];
    char bt_name[SAVVY_DEVICE_STR_LEN];
    int32_t blue_tooth;
    int32_t mic;
    int32_t wifi;
    int32_t tof;
    int32_t led;
    int32_t buzzer;
    int32_t move_sensor;
    int32_t reboot;
    char app_mgr[SAVVY_DEVICE_STR_LEN];
    char app_sensor[SAVVY_DEVICE_STR_LEN];
    char os[SAVVY_DEVICE_STR_LEN];
    char t_name[SAVVY_DEVICE_STR_LEN];
    int32_t toilet;
    int32_t stall;
    int32_t beacon;
    char app_rknn[SAVVY_DEVICE_STR_LEN];
    char ver_rknn[SAVVY_DEVICE_STR_LEN];
    int32_t smoke_value;
    int32_t data_collection;
} savvy_device_t;

/* Populates *out with the Android source defaults (JsonDeviceDto Java
 * field initializers: "" for strings, 0 for ints - identical in mgr and
 * sensor source). */
void savvy_device_set_defaults(savvy_device_t *out);

/* Parses `json` (`len` bytes, NUL-terminated at json[len]) into *out,
 * which must already be initialized (typically via
 * savvy_device_set_defaults()). Missing keys leave *out's existing value
 * untouched. Rejects (SAVVY_ERR_PROTOCOL): JSON null for any known field,
 * wrong JSON type for any known field, a fractional/non-finite/out-of-
 * INT32-range number for an integer field, a string value too long for
 * its fixed buffer, invalid UTF-8 anywhere in the tree, and any duplicate
 * key anywhere in the tree. Unknown extra keys are ignored (per
 * contracts/json_field_policy.md "unknown key: ignore + log"); if
 * `unknown_key_log_fn` is non-NULL, it is invoked once per unknown key
 * with ("jsonDeviceDto", key_name) - never the key's value. */
savvy_status_t savvy_device_parse(const char *json, size_t len, savvy_device_t *out,
                                   void (*unknown_key_log_fn)(const char *object_name, const char *key_name));

/* Builds *dev into a newly malloc'd, NUL-terminated JSON string
 * (*out_json; caller frees with free()). */
savvy_status_t savvy_device_build(const savvy_device_t *dev, char **out_json);

#ifdef __cplusplus
}
#endif

#endif
