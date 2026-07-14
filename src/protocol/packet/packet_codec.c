#include "savvy/protocol/packet_codec.h"
#include <string.h>
#include <pthread.h>

static uint32_t g_crc32_table[256];
static pthread_once_t g_crc32_once = PTHREAD_ONCE_INIT;

static void crc32_build_table(void)
{
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int k = 0; k < 8; k++) {
            c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        }
        g_crc32_table[i] = c;
    }
}

uint32_t savvy_crc32(const uint8_t *data, size_t len)
{
    pthread_once(&g_crc32_once, crc32_build_table);
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++) {
        crc = g_crc32_table[(crc ^ data[i]) & 0xFFu] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFu;
}

savvy_status_t savvy_packet_encode(uint8_t start, uint8_t command, uint8_t device, uint8_t config,
                                    const uint8_t *serial, size_t serial_len,
                                    const uint8_t *data, size_t data_len,
                                    uint8_t *out, size_t out_cap, size_t *out_written)
{
    if (serial == NULL || serial_len != SAVVY_PACKET_SERIAL_LEN) {
        return SAVVY_ERR_INVALID_ARGUMENT;
    }
    if (data == NULL && data_len != 0) {
        return SAVVY_ERR_INVALID_ARGUMENT;
    }
    if (out == NULL || out_written == NULL) {
        return SAVVY_ERR_INVALID_ARGUMENT;
    }
    /* The wire Length field is exactly 32 bits - reject before the
     * narrowing cast below could silently wrap. */
    if (data_len > UINT32_MAX) {
        return SAVVY_ERR_OVERFLOW;
    }
    /* Guard the header+data_len addition itself against size_t overflow
     * before doing any arithmetic with it (a 32-bit build could otherwise
     * wrap this sum and pass the capacity check below with a buffer far
     * too small for the actual data_len). */
    if (data_len > SIZE_MAX - (size_t)SAVVY_PACKET_HEADER_LEN) {
        return SAVVY_ERR_OVERFLOW;
    }
    size_t total = (size_t)SAVVY_PACKET_HEADER_LEN + data_len; /* now safe: overflow ruled out above */
    if (out_cap < total) {
        return SAVVY_ERR_OVERFLOW;
    }

    out[0] = start;
    out[1] = command;
    out[2] = device;
    out[3] = config;

    uint32_t length = (uint32_t)data_len; /* safe: data_len <= UINT32_MAX checked above */
    out[4] = (uint8_t)((length >> 24) & 0xFFu);
    out[5] = (uint8_t)((length >> 16) & 0xFFu);
    out[6] = (uint8_t)((length >> 8) & 0xFFu);
    out[7] = (uint8_t)(length & 0xFFu);

    memcpy(out + 8, serial, SAVVY_PACKET_SERIAL_LEN);

    uint32_t crc = savvy_crc32(data, data_len);
    out[22] = (uint8_t)((crc >> 24) & 0xFFu);
    out[23] = (uint8_t)((crc >> 16) & 0xFFu);
    out[24] = (uint8_t)((crc >> 8) & 0xFFu);
    out[25] = (uint8_t)(crc & 0xFFu);

    if (data_len > 0) {
        memcpy(out + SAVVY_PACKET_HEADER_LEN, data, data_len);
    }

    *out_written = total;
    return SAVVY_OK;
}

savvy_status_t savvy_packet_decode(const uint8_t *in, size_t in_len,
                                    savvy_packet_header_t *out_header,
                                    const uint8_t **out_data, size_t *out_data_len)
{
    if (in == NULL || out_header == NULL || out_data == NULL || out_data_len == NULL) {
        return SAVVY_ERR_INVALID_ARGUMENT;
    }
    if (in_len < SAVVY_PACKET_HEADER_LEN) {
        return SAVVY_ERR_PROTOCOL;
    }

    out_header->start = in[0];
    out_header->command = in[1];
    out_header->device = in[2];
    out_header->config = in[3];

    uint32_t length = ((uint32_t)in[4] << 24) | ((uint32_t)in[5] << 16) |
                       ((uint32_t)in[6] << 8) | (uint32_t)in[7];
    out_header->length = length;

    memcpy(out_header->serial, in + 8, SAVVY_PACKET_SERIAL_LEN);

    uint32_t crc = ((uint32_t)in[22] << 24) | ((uint32_t)in[23] << 16) |
                   ((uint32_t)in[24] << 8) | (uint32_t)in[25];
    out_header->crc32 = crc;

    /* Length overflow guard: never read Data past what `in` actually holds.
     * in_len >= SAVVY_PACKET_HEADER_LEN already checked above, so this
     * subtraction cannot underflow. */
    if (length > in_len - SAVVY_PACKET_HEADER_LEN) {
        return SAVVY_ERR_PROTOCOL;
    }

    *out_data = in + SAVVY_PACKET_HEADER_LEN;
    *out_data_len = length;
    return SAVVY_OK;
}
