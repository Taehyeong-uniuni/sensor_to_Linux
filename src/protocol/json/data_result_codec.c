#include "savvy/protocol/data_result_codec.h"
#include "savvy/protocol/json_codec.h"
#include "cJSON.h"
#include <string.h>
#include <stdlib.h>
#include <errno.h>

/* DEC-20260715-DATARESULT-GSON-PARITY: a quoted integer-format string is
 * coerced through the same numeric-token path Gson 2.8.2 uses for an
 * unquoted number - empirically confirmed for "-1", "4", "2147483647"
 * (accept) and "2147483648", "4.9", "abc" (reject). Grammar is the
 * minimal one consistent with every observed case: optional single
 * leading '-', then one or more ASCII digits, nothing else. */
static savvy_status_t savvy_data_result_string_to_int32(const char *s, int32_t *out)
{
    size_t i = (s[0] == '-') ? 1 : 0;
    if (s[i] == '\0') {
        return SAVVY_ERR_PROTOCOL; /* "-" alone, or empty string */
    }
    for (size_t j = i; s[j] != '\0'; j++) {
        if (s[j] < '0' || s[j] > '9') {
            return SAVVY_ERR_PROTOCOL; /* not integer-format, e.g. "4.9", "abc" */
        }
    }

    errno = 0;
    char *end = NULL;
    long long v = strtoll(s, &end, 10);
    if (errno == ERANGE || *end != '\0') {
        return SAVVY_ERR_PROTOCOL;
    }
    if (v < (long long)INT32_MIN || v > (long long)INT32_MAX) {
        return SAVVY_ERR_PROTOCOL; /* int32 overflow, e.g. "2147483648" */
    }

    *out = (int32_t)v;
    return SAVVY_OK;
}

static savvy_status_t savvy_data_result_value_to_int32(const cJSON *item, int32_t *out)
{
    if (cJSON_IsNumber(item)) {
        return savvy_json_number_to_int32(item, out);
    }
    if (cJSON_IsString(item) && item->valuestring != NULL) {
        return savvy_data_result_string_to_int32(item->valuestring, out);
    }
    return SAVVY_ERR_PROTOCOL; /* bool/array/object - not a Gson-coercible int */
}

savvy_status_t savvy_data_result_parse(const char *json, size_t len, savvy_data_result_t *out)
{
    if (json == NULL || out == NULL) {
        return SAVVY_ERR_INVALID_ARGUMENT;
    }

    cJSON *root = NULL;
    savvy_status_t st = savvy_json_parse_allow_duplicate_keys(json, len, &root);
    if (st != SAVVY_OK) {
        return st;
    }

    savvy_status_t result = SAVVY_ERR_PROTOCOL;
    if (cJSON_IsObject(root)) {
        /* DEC-20260715-DATARESULT-GSON-PARITY: last occurrence of a
         * duplicate "result" key wins - cJSON_GetObjectItemCaseSensitive
         * would return the FIRST match, so this walks the child list
         * itself, keeping overwriting the candidate as it goes. */
        cJSON *item = NULL;
        for (cJSON *child = root->child; child != NULL; child = child->next) {
            if (child->string != NULL && strcmp(child->string, "result") == 0) {
                item = child;
            }
        }

        if (item == NULL || cJSON_IsNull(item)) {
            /* Missing key or explicit null: Gson 2.8.2's unsafe/no-
             * constructor allocation bypasses the `= 4` field
             * initializer - empirically confirmed result=0. */
            out->result = 0;
            result = SAVVY_OK;
        } else {
            int32_t v;
            if (savvy_data_result_value_to_int32(item, &v) == SAVVY_OK) {
                out->result = v;
                result = SAVVY_OK;
            }
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
