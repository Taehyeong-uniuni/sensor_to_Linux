#ifndef SAVVY_PROTOCOL_STREAM_PARSER_H
#define SAVVY_PROTOCOL_STREAM_PARSER_H

#include <stdint.h>
#include <stddef.h>
#include "savvy/core/error.h"
#include "savvy/protocol/packet_codec.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Incremental packet parser for byte-stream transports with no inherent
 * message boundary (TCP 8140/8141, BT SPP/RFCOMM). NOT used for the
 * MGR-Sensor IPC layer: AF_UNIX SOCK_SEQPACKET already preserves record
 * boundaries (poc/ipc_seqpacket/result.md scenario 4), so FND-03 does not
 * reuse or apply this parser. */
typedef struct savvy_stream_parser {
    uint8_t *buf;
    size_t   buf_cap;
    size_t   head;  /* consumed offset; valid unread bytes are buf[head, tail) */
    size_t   tail;  /* write offset */
} savvy_stream_parser_t;

typedef enum savvy_stream_result {
    SAVVY_STREAM_NEED_MORE_DATA = 0,
    SAVVY_STREAM_PACKET_READY,
    SAVVY_STREAM_ERROR
} savvy_stream_result_t;

/* `buf`/`buf_cap` is caller-owned storage, sized to hold at least one
 * maximum-size packet (SAVVY_PACKET_HEADER_LEN + largest expected Data). */
void savvy_stream_parser_init(savvy_stream_parser_t *p, uint8_t *buf, size_t buf_cap);

/* Appends newly-read bytes. May compact already-consumed space internally
 * to make room; fails with SAVVY_ERR_OVERFLOW only if there is still not
 * enough room after compacting. Any pointer returned by a previous
 * try_extract() call must not be used after calling push() again. */
savvy_status_t savvy_stream_parser_push(savvy_stream_parser_t *p, const uint8_t *bytes, size_t len);

/* Attempts to extract one complete packet from whatever is currently
 * buffered. Call this repeatedly after each push() - a single push() may
 * deliver more than one complete, coalesced packet - until it returns
 * SAVVY_STREAM_NEED_MORE_DATA. On SAVVY_STREAM_PACKET_READY, *out_data
 * points into the parser's internal buffer and stays valid until the next
 * push()/try_extract() call. SAVVY_STREAM_ERROR means the declared Length
 * can never fit in buf_cap - the stream is unrecoverable and the caller
 * should close the connection. */
savvy_stream_result_t savvy_stream_parser_try_extract(savvy_stream_parser_t *p,
                                                        savvy_packet_header_t *out_header,
                                                        const uint8_t **out_data,
                                                        size_t *out_data_len);

#ifdef __cplusplus
}
#endif

#endif
