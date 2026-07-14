#include "savvy/protocol/data_result_codec.h"
#include "savvy/protocol/json_codec.h"
#include "cJSON.h"
#include <string.h>
#include <stdlib.h>

savvy_status_t savvy_data_result_parse(const char *json, savvy_data_result_t *out)
{
    if (json == NULL || out == NULL) {
        return SAVVY_ERR_INVALID_ARGUMENT;
    }

    cJSON *root = NULL;
    savvy_status_t st = savvy_json_parse(json, strlen(json), &root);
    if (st != SAVVY_OK) {
        return st;
    }

    savvy_status_t result = SAVVY_ERR_PROTOCOL;
    if (cJSON_IsObject(root)) {
        cJSON *item = cJSON_GetObjectItemCaseSensitive(root, "result");
        /* Missing "result" is deliberately a parse error here - see header. */
        if (item != NULL && cJSON_IsNumber(item)) {
            out->result = (int32_t)item->valuedouble;
            result = SAVVY_OK;
        }
    }

    cJSON_Delete(root);
    return result;
}

savvy_status_t savvy_data_result_build(const savvy_data_result_t *dr, char **out_json)
{
    if (dr == NULL || out_json == NULL) {
        return SAVVY_ERR_INVALID_ARGUMENT;
    }

    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return SAVVY_ERR_OUT_OF_MEMORY;
    }
    cJSON *value = cJSON_CreateNumber((double)dr->result);
    if (value == NULL || !cJSON_AddItemToObject(root, "result", value)) {
        cJSON_Delete(value);
        cJSON_Delete(root);
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

bool savvy_data_result_is_normal(const savvy_data_result_t *dr)
{
    return dr->result == 4;
}
