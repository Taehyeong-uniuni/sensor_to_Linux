#ifndef SAVVY_PROTOCOL_JSON_CODEC_H
#define SAVVY_PROTOCOL_JSON_CODEC_H

#include <stddef.h>
#include "savvy/core/error.h"
#include "cJSON.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Common parse/build utilities shared by config_codec/device_codec/
 * data_result_codec (DEC-20260714-01). cJSON* is used only within
 * src/protocol/{json,ipc}/ - code outside this layer never holds a cJSON*,
 * only the typed structs / plain strings these codecs produce. */

/* Parses `text` (exactly `len` bytes, NUL-terminated at text[len]) with
 * cJSON_ParseWithOpts(require_null_terminated=1), then rejects the parse
 * (SAVVY_ERR_PROTOCOL, tree freed) if any object anywhere in the resulting
 * tree contains a duplicate key - every schema-controlled object (envelope
 * root, payload, jsonConfigDto, jsonDeviceDto, DataResult, ...) rejects
 * duplicate keys, so this is applied unconditionally.
 * Fails with SAVVY_ERR_INVALID_ARGUMENT if text[len] != '\0'.
 * Fails with SAVVY_ERR_PROTOCOL if `text` contains an embedded NUL before
 * `len` (would otherwise let cJSON silently parse a truncated prefix).
 * On success, caller owns *out_root and must cJSON_Delete() it. */
savvy_status_t savvy_json_parse(const char *text, size_t len, cJSON **out_root);

/* Returns SAVVY_OK if no object in `node`'s subtree (node included) has a
 * duplicate key; SAVVY_ERR_PROTOCOL otherwise. Recurses into nested
 * objects and arrays. NULL is treated as SAVVY_OK (nothing to check). */
savvy_status_t savvy_json_check_no_duplicate_keys(const void *node);

/* malloc()-based string duplication (not strdup(), to avoid depending on
 * POSIX feature-test macros across macOS/glibc/uClibc). Caller frees the
 * result with free(). Returns NULL on allocation failure. */
char *savvy_strdup(const char *s);

#ifdef __cplusplus
}
#endif

#endif
