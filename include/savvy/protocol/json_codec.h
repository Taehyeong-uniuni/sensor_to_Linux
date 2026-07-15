#ifndef SAVVY_PROTOCOL_JSON_CODEC_H
#define SAVVY_PROTOCOL_JSON_CODEC_H

#include <stddef.h>
#include <stdint.h>
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
 * duplicate keys, so this is applied unconditionally. Every string key and
 * value anywhere in the tree is also validated as well-formed UTF-8
 * (rejects truncated/overlong/surrogate-invalid sequences) - also
 * unconditional.
 * Fails with SAVVY_ERR_INVALID_ARGUMENT if text[len] != '\0'.
 * Fails with SAVVY_ERR_PROTOCOL if `text` contains an embedded NUL before
 * `len` (would otherwise let cJSON silently parse a truncated prefix).
 * On success, caller owns *out_root and must cJSON_Delete() it. */
savvy_status_t savvy_json_parse(const char *text, size_t len, cJSON **out_root);

/* DataResult-Gson-parity exception ONLY (DEC-20260715-DATARESULT-GSON-
 * PARITY) - identical to savvy_json_parse() (NUL-terminator / embedded-NUL
 * checks, cJSON_ParseWithOpts, UTF-8 validation) EXCEPT it does NOT reject
 * duplicate keys. Every other schema-managed object (envelope root,
 * payload, jsonConfigDto, jsonDeviceDto) MUST keep using savvy_json_parse()
 * and its unconditional duplicate-key rejection - this function exists
 * solely so src/protocol/json/data_result_codec.c can apply DataResult's
 * own last-value-wins duplicate-key policy, matching observed Gson 2.8.2
 * behavior. Do not call this from any other codec. */
savvy_status_t savvy_json_parse_allow_duplicate_keys(const char *text, size_t len, cJSON **out_root);

/* Returns SAVVY_OK if no object in `node`'s subtree (node included) has a
 * duplicate key; SAVVY_ERR_PROTOCOL otherwise. Recurses into nested
 * objects and arrays. NULL is treated as SAVVY_OK (nothing to check). */
savvy_status_t savvy_json_check_no_duplicate_keys(const void *node);

/* Returns SAVVY_OK if every string key and string value in `node`'s
 * subtree (node included) is well-formed UTF-8; SAVVY_ERR_PROTOCOL
 * otherwise. NULL is SAVVY_OK. */
savvy_status_t savvy_json_check_utf8(const void *node);

/* Validates that a NUL-terminated string is well-formed UTF-8 per RFC
 * 3629: rejects invalid leading/continuation bytes, sequences truncated
 * by the end of the string, overlong encodings, surrogate-range
 * codepoints (U+D800-U+DFFF, which UTF-8 must never encode), and
 * codepoints beyond U+10FFFF. */
savvy_status_t savvy_utf8_validate(const char *s);

/* Validates that `item` is a JSON number representing a finite,
 * mathematical integer within [INT32_MIN, INT32_MAX], and if so writes
 * it to *out. Returns SAVVY_ERR_PROTOCOL for non-numbers, NaN/Infinity,
 * fractional values (e.g. a field declared as {"decibel":4.9}), or
 * out-of-int32-range values - never silently truncates. */
savvy_status_t savvy_json_number_to_int32(const void *item, int32_t *out);

/* malloc()-based string duplication (not strdup(), to avoid depending on
 * POSIX feature-test macros across macOS/glibc/uClibc). Caller frees the
 * result with free(). Returns NULL on allocation failure. */
char *savvy_strdup(const char *s);

#ifdef __cplusplus
}
#endif

#endif
