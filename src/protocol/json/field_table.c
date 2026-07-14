#include "field_table.h"
#include "savvy/protocol/json_codec.h"
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

static bool field_table_has_key(const savvy_field_desc_t *fields, size_t n_fields, const char *key)
{
    for (size_t i = 0; i < n_fields; i++) {
        if (strcmp(fields[i].json_key, key) == 0) {
            return true;
        }
    }
    return false;
}

savvy_status_t savvy_apply_field_table(const cJSON *root, const savvy_field_desc_t *fields,
                                        size_t n_fields, void *out_struct,
                                        const char *object_name, savvy_unknown_key_log_fn log_fn)
{
    if (log_fn != NULL) {
        for (const cJSON *present = root->child; present != NULL; present = present->next) {
            if (present->string != NULL && !field_table_has_key(fields, n_fields, present->string)) {
                log_fn(object_name, present->string);
            }
        }
    }

    for (size_t i = 0; i < n_fields; i++) {
        const cJSON *item = cJSON_GetObjectItemCaseSensitive(root, fields[i].json_key);
        if (item == NULL) {
            continue; /* missing: keep whatever value is already in out_struct */
        }
        if (cJSON_IsNull(item)) {
            return SAVVY_ERR_PROTOCOL;
        }

        unsigned char *field_ptr = (unsigned char *)out_struct + fields[i].offset;

        if (fields[i].type == SAVVY_FIELD_STRING) {
            if (!cJSON_IsString(item) || item->valuestring == NULL) {
                return SAVVY_ERR_PROTOCOL;
            }
            size_t len = strlen(item->valuestring);
            if (len >= fields[i].str_cap) {
                return SAVVY_ERR_PROTOCOL; /* reject rather than silently truncate */
            }
            memcpy(field_ptr, item->valuestring, len + 1);
        } else {
            int32_t v;
            if (savvy_json_number_to_int32(item, &v) != SAVVY_OK) {
                return SAVVY_ERR_PROTOCOL;
            }
            memcpy(field_ptr, &v, sizeof(v));
        }
    }
    return SAVVY_OK;
}

cJSON *savvy_build_field_table(const savvy_field_desc_t *fields, size_t n_fields, const void *in_struct)
{
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return NULL;
    }

    for (size_t i = 0; i < n_fields; i++) {
        const unsigned char *field_ptr = (const unsigned char *)in_struct + fields[i].offset;
        cJSON *value;

        if (fields[i].type == SAVVY_FIELD_STRING) {
            value = cJSON_CreateString((const char *)field_ptr);
        } else {
            int32_t v;
            memcpy(&v, field_ptr, sizeof(v));
            value = cJSON_CreateNumber((double)v);
        }

        if (value == NULL || !cJSON_AddItemToObject(root, fields[i].json_key, value)) {
            cJSON_Delete(value);
            cJSON_Delete(root);
            return NULL;
        }
    }
    return root;
}
