#ifndef SAVVY_PROTOCOL_PACKET_CODEC_H
#define SAVVY_PROTOCOL_PACKET_CODEC_H

#include <stdint.h>
#include <stddef.h>
#include "savvy/core/error.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SAVVY_PACKET_HEADER_LEN 26u
#define SAVVY_PACKET_SERIAL_LEN 14u

/* Fixed 26-byte header (01_BASELINE.md 4.1): offsets 0/1/2/3 Start/Command/
 * Device/Config, 4 Length(4B BE), 8 Serial(14B), 22 CRC32(4B), 26+ Data.
 * Fields below are host-byte-order after decode / before encode. */
typedef struct savvy_packet_header {
    uint8_t  start;
    uint8_t  command;
    uint8_t  device;
    uint8_t  config;
    uint32_t length;                          /* Data length */
    uint8_t  serial[SAVVY_PACKET_SERIAL_LEN];  /* opaque 14-byte field, not a C string */
    uint32_t crc32;                            /* CRC32 of Data as read off the wire */
} savvy_packet_header_t;

/* CRC-32/ISO-HDLC (poly 0xEDB88320 reflected, init 0xFFFFFFFF, final XOR
 * 0xFFFFFFFF - the java.util.zip.CRC32-compatible zlib/gzip/PKZIP variant)
 * over `len` bytes starting at `data`. */
uint32_t savvy_crc32(const uint8_t *data, size_t len);

/* Encodes Start/Command/Device/Config + serial + Data into a 26-byte
 * header followed by Data, computing CRC32 over Data.
 *   - `serial` must be exactly SAVVY_PACKET_SERIAL_LEN bytes, already
 *     normalized by the caller: this function performs no padding or
 *     truncation and rejects any other serial_len with
 *     SAVVY_ERR_INVALID_ARGUMENT (see contracts/ serial policy).
 *   - Fails with SAVVY_ERR_OVERFLOW if out_cap < SAVVY_PACKET_HEADER_LEN + data_len.
 * On success, *out_written == SAVVY_PACKET_HEADER_LEN + data_len. */
savvy_status_t savvy_packet_encode(uint8_t start, uint8_t command, uint8_t device, uint8_t config,
                                    const uint8_t *serial, size_t serial_len,
                                    const uint8_t *data, size_t data_len,
                                    uint8_t *out, size_t out_cap, size_t *out_written);

/* Decodes one header + Data from `in` (in_len bytes available).
 *   - Fails with SAVVY_ERR_PROTOCOL if in_len < SAVVY_PACKET_HEADER_LEN.
 *   - Fails with SAVVY_ERR_PROTOCOL if the decoded Length would require
 *     reading past in_len (length overflow); never reads past `in`.
 *   - On success, *out_data points *inside* `in` at offset
 *     SAVVY_PACKET_HEADER_LEN (no copy) and *out_data_len == header->length.
 *   - Does NOT verify CRC32: callers apply the endpoint-specific CRC
 *     policy (see contracts/) themselves via savvy_crc32(). */
savvy_status_t savvy_packet_decode(const uint8_t *in, size_t in_len,
                                    savvy_packet_header_t *out_header,
                                    const uint8_t **out_data, size_t *out_data_len);

#ifdef __cplusplus
}
#endif

#endif
