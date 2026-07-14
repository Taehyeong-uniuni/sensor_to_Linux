#include "savvy/protocol/json_codec.h"
#include <string.h>
#include <stdlib.h>
#include <math.h>

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

savvy_status_t savvy_utf8_validate(const char *s)
{
    if (s == NULL) {
        return SAVVY_ERR_INVALID_ARGUMENT;
    }

    const unsigned char *p = (const unsigned char *)s;
    while (*p != '\0') {
        unsigned char b0 = p[0];
        size_t extra;
        uint32_t cp;
        uint32_t min_cp;

        if (b0 < 0x80u) {
            p += 1;
            continue;
        } else if ((b0 & 0xE0u) == 0xC0u) {
            extra = 1; cp = b0 & 0x1Fu; min_cp = 0x80u;
        } else if ((b0 & 0xF0u) == 0xE0u) {
            extra = 2; cp = b0 & 0x0Fu; min_cp = 0x800u;
        } else if ((b0 & 0xF8u) == 0xF0u) {
            extra = 3; cp = b0 & 0x07u; min_cp = 0x10000u;
        } else {
            return SAVVY_ERR_PROTOCOL; /* stray continuation byte or invalid 0xF8-0xFF leading byte */
        }

        /* Reading p[1+i] here can never run past the buffer: if the
         * sequence is truncated by the string's own NUL terminator, that
         * NUL byte itself fails the (b & 0xC0)==0x80 continuation check
         * below and we return before advancing p past it. */
        for (size_t i = 0; i < extra; i++) {
            unsigned char b = p[1 + i];
            if ((b & 0xC0u) != 0x80u) {
                return SAVVY_ERR_PROTOCOL; /* truncated or malformed continuation byte */
            }
            cp = (cp << 6) | (b & 0x3Fu);
        }

        if (cp < min_cp) {
            return SAVVY_ERR_PROTOCOL; /* overlong encoding */
        }
        if (cp >= 0xD800u && cp <= 0xDFFFu) {
            return SAVVY_ERR_PROTOCOL; /* surrogate range - invalid in UTF-8 */
        }
        if (cp > 0x10FFFFu) {
            return SAVVY_ERR_PROTOCOL; /* beyond Unicode range */
        }

        p += 1 + extra;
    }
    return SAVVY_OK;
}

savvy_status_t savvy_json_check_utf8(const void *node_v)
{
    const cJSON *node = (const cJSON *)node_v;
    if (node == NULL) {
        return SAVVY_OK;
    }

    if (node->string != NULL && savvy_utf8_validate(node->string) != SAVVY_OK) {
        return SAVVY_ERR_PROTOCOL;
    }
    if (cJSON_IsString(node) && node->valuestring != NULL) {
        if (savvy_utf8_validate(node->valuestring) != SAVVY_OK) {
            return SAVVY_ERR_PROTOCOL;
        }
    }

    if (cJSON_IsObject(node) || cJSON_IsArray(node)) {
        for (const cJSON *child = node->child; child != NULL; child = child->next) {
            savvy_status_t st = savvy_json_check_utf8(child);
            if (st != SAVVY_OK) {
                return st;
            }
        }
    }

    return SAVVY_OK;
}

savvy_status_t savvy_json_number_to_int32(const void *item_v, int32_t *out)
{
    const cJSON *item = (const cJSON *)item_v;
    if (item == NULL || out == NULL || !cJSON_IsNumber(item)) {
        return SAVVY_ERR_PROTOCOL;
    }
    double v = item->valuedouble;
    if (!isfinite(v)) {
        return SAVVY_ERR_PROTOCOL;
    }
    if (v != floor(v)) {
        return SAVVY_ERR_PROTOCOL; /* fractional, e.g. 4.9 */
    }
    /* INT32_MIN/INT32_MAX are both exactly representable as double
     * (well within the 2^53 exact-integer range), so this comparison is
     * precise, not an approximation. */
    if (v < (double)INT32_MIN || v > (double)INT32_MAX) {
        return SAVVY_ERR_PROTOCOL;
    }
    *out = (int32_t)v;
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
    if (savvy_json_check_utf8(root) != SAVVY_OK) {
        cJSON_Delete(root);
        return SAVVY_ERR_PROTOCOL;
    }

    *out_root = root;
    return SAVVY_OK;
}
