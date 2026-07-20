#include "savvy/protocol/ipc_envelope.h"
#include "savvy/protocol/json_codec.h"
#include "cJSON.h"
#include <stdlib.h>
#include <string.h>

savvy_status_t savvy_ipc_envelope_parse(const char *text, size_t len, savvy_ipc_envelope_t *out_env)
{
    if (text == NULL || out_env == NULL) {
        return SAVVY_ERR_INVALID_ARGUMENT;
    }
    out_env->action = NULL;
    out_env->payload_json = NULL;

    cJSON *root = NULL;
    savvy_status_t st = savvy_json_parse(text, len, &root);
    if (st != SAVVY_OK) {
        return st;
    }

    savvy_status_t result = SAVVY_ERR_PROTOCOL;
    char *action_copy = NULL;
    char *payload_text = NULL;

    if (cJSON_IsObject(root)) {
        int field_count = 0;
        for (cJSON *c = root->child; c != NULL; c = c->next) {
            field_count++;
        }

        cJSON *action_node = cJSON_GetObjectItemCaseSensitive(root, "action");
        cJSON *payload_node = cJSON_GetObjectItemCaseSensitive(root, "payload");

        if (field_count == 2 && action_node != NULL && payload_node != NULL &&
            cJSON_IsString(action_node) && action_node->valuestring != NULL &&
            action_node->valuestring[0] != '\0' && cJSON_IsObject(payload_node)) {

            action_copy = savvy_strdup(action_node->valuestring);
            payload_text = cJSON_PrintUnformatted(payload_node);

            if (action_copy != NULL && payload_text != NULL) {
                out_env->action = action_copy;
                out_env->payload_json = payload_text;
                result = SAVVY_OK;
            } else {
                free(action_copy);
                if (payload_text != NULL) {
                    cJSON_free(payload_text);
                }
                result = SAVVY_ERR_OUT_OF_MEMORY;
            }
        }
    }

    cJSON_Delete(root);
    return result;
}

savvy_status_t savvy_ipc_envelope_build(const char *action, const char *payload_json,
                                        char **out_text, size_t *out_len)
{
    if (action == NULL || action[0] == '\0' || payload_json == NULL ||
        out_text == NULL || out_len == NULL) {
        return SAVVY_ERR_INVALID_ARGUMENT;
    }

    cJSON *payload_node = NULL;
    savvy_status_t st = savvy_json_parse(payload_json, strlen(payload_json), &payload_node);
    if (st != SAVVY_OK) {
        return SAVVY_ERR_INVALID_ARGUMENT;
    }
    if (!cJSON_IsObject(payload_node)) {
        cJSON_Delete(payload_node);
        return SAVVY_ERR_INVALID_ARGUMENT;
    }

    cJSON *root = cJSON_CreateObject();
    cJSON *action_node = (root != NULL) ? cJSON_CreateString(action) : NULL;
    if (root == NULL || action_node == NULL) {
        cJSON_Delete(payload_node);
        cJSON_Delete(root);
        cJSON_Delete(action_node);
        return SAVVY_ERR_OUT_OF_MEMORY;
    }

    cJSON_AddItemToObject(root, "action", action_node);
    cJSON_AddItemToObject(root, "payload", payload_node); /* root now owns both */

    char *text = cJSON_PrintUnformatted(root);
    cJSON_Delete(root); /* also frees action_node and payload_node */

    if (text == NULL) {
        return SAVVY_ERR_OUT_OF_MEMORY;
    }

    size_t len = strlen(text);
    if (len > SAVVY_IPC_MAX_MESSAGE) {
        cJSON_free(text);
        return SAVVY_ERR_OVERFLOW;
    }

    *out_text = text;
    *out_len = len;
    return SAVVY_OK;
}

void savvy_ipc_envelope_free(savvy_ipc_envelope_t *env)
{
    if (env == NULL) {
        return;
    }
    free(env->action);
    if (env->payload_json != NULL) {
        cJSON_free(env->payload_json);
    }
    env->action = NULL;
    env->payload_json = NULL;
}
