#include "savvy/protocol/stream_parser.h"
#include <string.h>

void savvy_stream_parser_init(savvy_stream_parser_t *p, uint8_t *buf, size_t buf_cap)
{
    p->buf = buf;
    p->buf_cap = buf_cap;
    p->head = 0;
    p->tail = 0;
}

savvy_status_t savvy_stream_parser_push(savvy_stream_parser_t *p, const uint8_t *bytes, size_t len)
{
    if (len == 0) {
        return SAVVY_OK;
    }
    if (bytes == NULL) {
        return SAVVY_ERR_INVALID_ARGUMENT;
    }

    size_t used = p->tail - p->head;
    size_t free_at_tail = p->buf_cap - p->tail;
    if (free_at_tail < len) {
        /* Compact consumed space. Safe: any pointer from a previous
         * try_extract() must not be used across this call (see header). */
        if (p->head > 0) {
            memmove(p->buf, p->buf + p->head, used);
            p->head = 0;
            p->tail = used;
            free_at_tail = p->buf_cap - p->tail;
        }
        if (free_at_tail < len) {
            return SAVVY_ERR_OVERFLOW;
        }
    }

    memcpy(p->buf + p->tail, bytes, len);
    p->tail += len;
    return SAVVY_OK;
}

savvy_stream_result_t savvy_stream_parser_try_extract(savvy_stream_parser_t *p,
                                                        savvy_packet_header_t *out_header,
                                                        const uint8_t **out_data,
                                                        size_t *out_data_len)
{
    size_t available = p->tail - p->head;
    if (available < SAVVY_PACKET_HEADER_LEN) {
        return SAVVY_STREAM_NEED_MORE_DATA;
    }

    const uint8_t *base = p->buf + p->head;
    uint32_t length = ((uint32_t)base[4] << 24) | ((uint32_t)base[5] << 16) |
                       ((uint32_t)base[6] << 8) | (uint32_t)base[7];
    size_t total_needed = (size_t)SAVVY_PACKET_HEADER_LEN + length;

    if (total_needed > p->buf_cap) {
        return SAVVY_STREAM_ERROR; /* can never fit in this parser's buffer */
    }
    if (available < total_needed) {
        return SAVVY_STREAM_NEED_MORE_DATA;
    }

    const uint8_t *data_ptr = NULL;
    size_t data_len = 0;
    savvy_status_t st = savvy_packet_decode(base, total_needed, out_header, &data_ptr, &data_len);
    if (st != SAVVY_OK) {
        return SAVVY_STREAM_ERROR;
    }

    *out_data = data_ptr;
    *out_data_len = data_len;

    /* Only advance head - never shift/overwrite bytes here, so the
     * pointer just returned in *out_data stays valid as documented. */
    p->head += total_needed;

    return SAVVY_STREAM_PACKET_READY;
}
