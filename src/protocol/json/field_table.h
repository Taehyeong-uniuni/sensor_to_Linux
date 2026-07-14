#ifndef SAVVY_JSON_FIELD_TABLE_H
#define SAVVY_JSON_FIELD_TABLE_H

#include <stddef.h>
#include "savvy/core/error.h"
#include "cJSON.h"

/* Private helper shared by config_codec.c/device_codec.c so ~53 fields
 * across two DTOs don't need hand-written per-field parse/build code
 * (and the risk of a transcription error that comes with that). Not a
 * public API - lives only inside src/protocol/json/. */

typedef enum savvy_field_type {
    SAVVY_FIELD_STRING,
    SAVVY_FIELD_INT32
} savvy_field_type_t;

typedef struct savvy_field_desc {
    const char *json_key;
    savvy_field_type_t type;
    size_t offset;   /* offsetof() into the target struct */
    size_t str_cap;  /* buffer size, meaningful only for SAVVY_FIELD_STRING */
} savvy_field_desc_t;

/* Applies each present, non-null, correctly-typed key in `root` onto the
 * corresponding field of `out_struct` (already pre-populated with
 * defaults). Missing keys leave the existing value untouched (common
 * JSON field policy: "missing -> keep default"). Returns
 * SAVVY_ERR_PROTOCOL on: JSON null for a known field, wrong JSON type,
 * or a string longer than its field's str_cap-1 (rejected, not
 * truncated). Ignores unknown keys. Does NOT check for duplicate keys
 * itself - callers parse `root` via savvy_json_parse() first, which
 * already enforces that globally across the whole tree. */
savvy_status_t savvy_apply_field_table(const cJSON *root, const savvy_field_desc_t *fields,
                                        size_t n_fields, void *out_struct);

/* Builds a new cJSON object with one entry per field, reading from
 * `in_struct`. Returns NULL on allocation failure (caller need not
 * cJSON_Delete a NULL result). */
cJSON *savvy_build_field_table(const savvy_field_desc_t *fields, size_t n_fields, const void *in_struct);

#endif
