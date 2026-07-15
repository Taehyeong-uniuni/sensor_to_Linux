#include "sensor_stream/wav.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* Explicit byte-at-a-time little-endian stores - deliberately NOT a cast +
 * memcpy of a native uint16_t/uint32_t, because that would silently depend
 * on the host's byte order. WAV headers are always little-endian regardless
 * of host/CPU endianness (and regardless of this project's OWN wire packet
 * header, which is big-endian - see savvy/protocol/packet_codec.h; the two
 * are unrelated formats that happen to disagree on endianness). */
static void put_u16_le(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
}

static void put_u32_le(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
    p[2] = (uint8_t)((v >> 16) & 0xFFu);
    p[3] = (uint8_t)((v >> 24) & 0xFFu);
}

savvy_status_t sensor_wav_wrap(const uint8_t *pcm, size_t pcm_len,
                                uint8_t **out_wav, size_t *out_wav_len)
{
    if (out_wav == NULL || out_wav_len == NULL) {
        return SAVVY_ERR_INVALID_ARGUMENT;
    }
    if (pcm == NULL && pcm_len > 0) {
        return SAVVY_ERR_INVALID_ARGUMENT;
    }

    /* The WAV `data` chunk's size field, and the RIFF chunk's "36 + data
     * size" field, are both 32-bit unsigned wire fields. pcm_len must fit
     * in the former; 36 + pcm_len must fit in the latter. Both are
     * checked explicitly rather than letting a (uint32_t) cast silently
     * wrap. */
    if (pcm_len > UINT32_MAX) {
        return SAVVY_ERR_OVERFLOW;
    }
    if (pcm_len > (size_t)(UINT32_MAX - 36u)) {
        return SAVVY_ERR_OVERFLOW;
    }

    /* 44 + pcm_len must not overflow size_t either. On the 64-bit size_t
     * builds this project actually targets, this can never independently
     * fire (pcm_len is already bounded to <= UINT32_MAX - 36 above, which
     * is far below SIZE_MAX - 44) - kept as an explicit, self-documenting
     * guard for defense-in-depth / any hypothetical 32-bit size_t build,
     * mirroring the equivalent guard in tests/contract/test_packet.c's
     * savvy_packet_encode overflow coverage. */
    if (pcm_len > SIZE_MAX - (size_t)SENSOR_WAV_HEADER_LEN) {
        return SAVVY_ERR_OVERFLOW;
    }

    size_t total_len = (size_t)SENSOR_WAV_HEADER_LEN + pcm_len;
    uint8_t *buf = (uint8_t *)malloc(total_len);
    if (buf == NULL) {
        return SAVVY_ERR_OUT_OF_MEMORY;
    }

    size_t off = 0;
    memcpy(buf + off, "RIFF", 4); off += 4;
    put_u32_le(buf + off, 36u + (uint32_t)pcm_len); off += 4;
    memcpy(buf + off, "WAVE", 4); off += 4;
    memcpy(buf + off, "fmt ", 4); off += 4;
    put_u32_le(buf + off, 16u); off += 4;                                  /* fmt chunk size */
    put_u16_le(buf + off, (uint16_t)SENSOR_WAV_AUDIO_FORMAT_PCM); off += 2; /* audio format = PCM */
    put_u16_le(buf + off, (uint16_t)SENSOR_WAV_NUM_CHANNELS); off += 2;
    put_u32_le(buf + off, (uint32_t)SENSOR_WAV_SAMPLE_RATE); off += 4;
    put_u32_le(buf + off, (uint32_t)SENSOR_WAV_BYTE_RATE); off += 4;
    put_u16_le(buf + off, (uint16_t)SENSOR_WAV_BLOCK_ALIGN); off += 2;
    put_u16_le(buf + off, (uint16_t)SENSOR_WAV_BITS_PER_SAMPLE); off += 2;
    memcpy(buf + off, "data", 4); off += 4;
    put_u32_le(buf + off, (uint32_t)pcm_len); off += 4;

    if (pcm_len > 0) {
        memcpy(buf + off, pcm, pcm_len);
    }
    off += pcm_len;

    /* off must equal total_len exactly: 4+4+4+4+4+2+2+4+4+2+2+4+4 == 44,
     * i.e. SENSOR_WAV_HEADER_LEN, plus pcm_len - asserted implicitly by
     * construction, no separate runtime check needed since every field
     * width above is a compile-time constant. */

    *out_wav = buf;
    *out_wav_len = total_len;
    return SAVVY_OK;
}
