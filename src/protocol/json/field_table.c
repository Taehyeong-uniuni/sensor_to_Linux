#include "field_table.h"
#include <string.h>
#include <stdint.h>

savvy_status_t savvy_apply_field_table(const cJSON *root, const savvy_field_desc_t *fields,
                                        size_t n_fields, void *out_struct)
{
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
            if (!cJSON_IsNumber(item)) {
                return SAVVY_ERR_PROTOCOL;
            }
            int32_t v = (int32_t)item->valuedouble;
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
