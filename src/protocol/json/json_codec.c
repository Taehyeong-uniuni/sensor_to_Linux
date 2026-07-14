#include "savvy/protocol/json_codec.h"
#include <string.h>
#include <stdlib.h>

char *savvy_strdup(const char *s)
{
    size_t n = strlen(s) + 1;
    char *out = (char *)malloc(n);
    if (out != NULL) {
        memcpy(out, s, n);
    }
    return out;
}

savvy_status_t savvy_json_check_no_duplicate_keys(const void *node_v)
{
    const cJSON *node = (const cJSON *)node_v;
    if (node == NULL) {
        return SAVVY_OK;
    }

    if (cJSON_IsObject(node)) {
        for (const cJSON *a = node->child; a != NULL; a = a->next) {
            for (const cJSON *b = a->next; b != NULL; b = b->next) {
                if (a->string != NULL && b->string != NULL && strcmp(a->string, b->string) == 0) {
                    return SAVVY_ERR_PROTOCOL;
                }
            }
        }
    }

    if (cJSON_IsObject(node) || cJSON_IsArray(node)) {
        for (const cJSON *child = node->child; child != NULL; child = child->next) {
            savvy_status_t st = savvy_json_check_no_duplicate_keys(child);
            if (st != SAVVY_OK) {
                return st;
            }
        }
    }

    return SAVVY_OK;
}

savvy_status_t savvy_json_parse(const char *text, size_t len, cJSON **out_root)
{
    if (text == NULL || out_root == NULL) {
        return SAVVY_ERR_INVALID_ARGUMENT;
    }
    if (text[len] != '\0') {
        return SAVVY_ERR_INVALID_ARGUMENT;
    }
    if (memchr(text, '\0', len) != NULL) {
        /* A raw NUL inside the bytes is never valid JSON text and would
         * otherwise let cJSON_ParseWithOpts silently stop at the first one. */
        return SAVVY_ERR_PROTOCOL;
    }

    const char *parse_end = NULL;
    cJSON *root = cJSON_ParseWithOpts(text, &parse_end, 1);
    if (root == NULL) {
        return SAVVY_ERR_PROTOCOL;
    }

    if (savvy_json_check_no_duplicate_keys(root) != SAVVY_OK) {
        cJSON_Delete(root);
        return SAVVY_ERR_PROTOCOL;
    }

    *out_root = root;
    return SAVVY_OK;
}
