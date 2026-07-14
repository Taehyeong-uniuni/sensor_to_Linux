#ifndef SAVVY_PROTOCOL_IPC_ENVELOPE_H
#define SAVVY_PROTOCOL_IPC_ENVELOPE_H

#include <stddef.h>
#include "savvy/core/error.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 64 KiB application-level cap (08_BLOCKERS.md DEC-20260714-02); the
 * kernel does not enforce this for AF_UNIX SOCK_SEQPACKET, so it must be
 * checked here before every send() and after every recv(). */
#define SAVVY_IPC_MAX_MESSAGE 65536u

/* A parsed, validated {action, payload} envelope
 * (contracts/mgr_sensor_ipc.schema.json). Both fields are malloc'd,
 * NUL-terminated strings owned by this struct; `payload_json` is the
 * re-serialized JSON text of the "payload" object (unformatted), meant to
 * be handed to the matching DTO codec (config_codec_parse() etc. in
 * src/protocol/json/). This struct never carries a cJSON* so that no code
 * outside src/protocol/{json,ipc}/ ever touches cJSON directly
 * (08_BLOCKERS.md DEC-20260714-01). */
typedef struct savvy_ipc_envelope {
    char *action;
    char *payload_json;
} savvy_ipc_envelope_t;

/* Parses and validates one envelope from `text` (len bytes, NUL-terminated
 * at text[len]). Enforces: root is a JSON object with exactly the two
 * keys "action" and "payload" (no additional top-level fields - e.g. no
 * version/sequence/request_id); "action" is a non-empty string; "payload"
 * is an object (payload: {} for actions that carry no data). Duplicate
 * keys anywhere in the tree are rejected (delegated to savvy_json_parse).
 * Does NOT enforce SAVVY_IPC_MAX_MESSAGE - callers check the encoded size
 * themselves before send() / after recv(), per contracts/. */
savvy_status_t savvy_ipc_envelope_parse(const char *text, size_t len, savvy_ipc_envelope_t *out_env);

/* Builds `{"action":"<action>","payload":<payload_json>}` into a newly
 * malloc'd, NUL-terminated string (*out_text; caller frees with free()).
 * `payload_json` must already be valid, serialized JSON object text (e.g.
 * "{}", or a DTO codec's build() output). Fails with SAVVY_ERR_OVERFLOW if
 * the encoded size would exceed SAVVY_IPC_MAX_MESSAGE - callers must not
 * call send() in that case. */
savvy_status_t savvy_ipc_envelope_build(const char *action, const char *payload_json,
                                        char **out_text, size_t *out_len);

void savvy_ipc_envelope_free(savvy_ipc_envelope_t *env);

#ifdef __cplusplus
}
#endif

#endif
