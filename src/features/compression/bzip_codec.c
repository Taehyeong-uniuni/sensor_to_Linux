#include "sensor_stream/bzip.h"

#include <bzlib.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* libbz2's streaming bz_stream expresses avail_in/avail_out as a 32-bit
 * `unsigned int`, regardless of size_t's actual width - so a single
 * compress/decompress call can only ever hand it at most UINT_MAX bytes
 * of input without a size_t -> unsigned int narrowing cast silently
 * wrapping. Rather than looping to re-feed input in multiple chunks
 * (adding real complexity for a case this system's actual sensor/audio
 * payloads never approach - many orders of magnitude below 4 GiB), this
 * module simply rejects input_len beyond that bound up front with
 * SAVVY_ERR_OVERFLOW. This is an explicit, documented limitation, not a
 * silent truncation. */
#define SENSOR_BZIP_MAX_INPUT ((size_t)UINT_MAX)

#define SENSOR_BZIP_COMPRESS_LEVEL 9
#define SENSOR_BZIP_WORK_FACTOR    0   /* 0 == bzlib default (30) */
#define SENSOR_BZIP_VERBOSITY      0

/* Belt-and-suspenders circuit breaker: bounds the number of
 * BZ2_bzCompress/BZ2_bzDecompress calls in the streaming loops below so
 * a hypothetical bzlib misbehavior (or a bug in this file) can never
 * hang the process forever instead of returning an error. Every
 * realistic call - even multi-megabyte inputs - completes in a handful
 * of iterations (each iteration makes real forward progress via
 * geometric output-buffer growth), so this is never expected to trigger
 * in practice. */
#define SENSOR_BZIP_MAX_LOOP_ITERATIONS 10000000u

static size_t sensor_min_sz(size_t a, size_t b)
{
    return (a < b) ? a : b;
}

/* Grows *buf (currently *cap bytes) so that *cap >= needed, preserving
 * existing contents, via geometric (doubling) growth - overflow-checked
 * so a huge `needed` can never make the doubling arithmetic itself wrap.
 * Returns SAVVY_OK / SAVVY_ERR_OUT_OF_MEMORY; on OOM, *buf and *cap are
 * left exactly as they were (realloc's own no-op-on-failure guarantee),
 * never freed or partially updated. */
static savvy_status_t sensor_bzip_ensure_capacity(uint8_t **buf, size_t *cap, size_t needed)
{
    if (needed <= *cap) {
        return SAVVY_OK;
    }

    size_t new_cap = (*cap == 0) ? (size_t)4096 : *cap;
    while (new_cap < needed) {
        if (new_cap > SIZE_MAX / 2) {
            /* Doubling would overflow size_t - jump directly to `needed`
             * instead (itself already a valid, non-overflowed size_t
             * computed by the caller). */
            new_cap = needed;
            break;
        }
        new_cap *= 2;
    }

    uint8_t *grown = (uint8_t *)realloc(*buf, new_cap);
    if (grown == NULL) {
        return SAVVY_ERR_OUT_OF_MEMORY;
    }
    *buf = grown;
    *cap = new_cap;
    return SAVVY_OK;
}

savvy_status_t sensor_bzip_compress(const uint8_t *input, size_t input_len,
                                     uint8_t **out_data, size_t *out_len)
{
    if (out_data == NULL || out_len == NULL) {
        return SAVVY_ERR_INVALID_ARGUMENT;
    }
    if (input == NULL && input_len > 0) {
        return SAVVY_ERR_INVALID_ARGUMENT;
    }
    if (input_len > SENSOR_BZIP_MAX_INPUT) {
        return SAVVY_ERR_OVERFLOW;
    }

    bz_stream strm;
    memset(&strm, 0, sizeof(strm)); /* bzalloc/bzfree/opaque = NULL -> bzlib's internal malloc/free */

    int bzrc = BZ2_bzCompressInit(&strm, SENSOR_BZIP_COMPRESS_LEVEL, SENSOR_BZIP_VERBOSITY, SENSOR_BZIP_WORK_FACTOR);
    if (bzrc != BZ_OK) {
        /* Unreachable in practice: our params are fixed/valid constants,
         * so this can only be BZ_MEM_ERROR (bzlib's internal state
         * allocation failed) or BZ_PARAM_ERROR (would indicate a bug in
         * this file). Handled defensively either way - never crash. */
        return (bzrc == BZ_MEM_ERROR) ? SAVVY_ERR_OUT_OF_MEMORY : SAVVY_ERR_UNKNOWN;
    }

    /* Initial capacity guess only (correctness never depends on this
     * being right - sensor_bzip_ensure_capacity() grows it further as
     * needed): bzip2's own documented worst-case expansion bound is
     * input + input/100 + 600 (see BZ2_bzBuffToBuffCompress's docs in
     * bzlib.h), which for input_len == 0 still yields a comfortable 600
     * bytes - plenty for the minimal empty-content stream. */
    size_t out_cap = input_len + (input_len / 100) + 600;
    uint8_t *out_buf = (uint8_t *)malloc(out_cap);
    if (out_buf == NULL) {
        BZ2_bzCompressEnd(&strm);
        return SAVVY_ERR_OUT_OF_MEMORY;
    }
    size_t out_written = 0;

    strm.next_in = (char *)input; /* never written through by BZ2_bzCompress; read-only despite the non-const field */
    strm.avail_in = (unsigned int)input_len;

    savvy_status_t status = SAVVY_OK;
    unsigned int iterations = 0;
    for (;;) {
        if (++iterations > SENSOR_BZIP_MAX_LOOP_ITERATIONS) {
            status = SAVVY_ERR_UNKNOWN;
            break;
        }

        if (out_written == out_cap) {
            savvy_status_t grow_status = sensor_bzip_ensure_capacity(&out_buf, &out_cap, out_written + 1);
            if (grow_status != SAVVY_OK) {
                status = grow_status;
                break;
            }
        }

        size_t avail_now = sensor_min_sz(out_cap - out_written, (size_t)UINT_MAX);
        strm.next_out = (char *)(out_buf + out_written);
        strm.avail_out = (unsigned int)avail_now;

        int action = (strm.avail_in > 0) ? BZ_RUN : BZ_FINISH;
        bzrc = BZ2_bzCompress(&strm, action);

        size_t produced = avail_now - (size_t)strm.avail_out;
        out_written += produced;

        if (bzrc == BZ_STREAM_END) {
            break;
        }
        if (bzrc != BZ_RUN_OK && bzrc != BZ_FINISH_OK) {
            /* BZ_SEQUENCE_ERROR/BZ_PARAM_ERROR would indicate a bug in
             * this loop, not a caller-input problem (compress's input is
             * never attacker-controlled/truncated - the whole buffer is
             * always handed over up front) - handled defensively rather
             * than assumed unreachable. */
            status = SAVVY_ERR_UNKNOWN;
            break;
        }
    }

    BZ2_bzCompressEnd(&strm);

    if (status != SAVVY_OK) {
        free(out_buf);
        return status;
    }

    *out_data = out_buf;
    *out_len = out_written;
    return SAVVY_OK;
}

savvy_status_t sensor_bzip_decompress(const uint8_t *input, size_t input_len,
                                       uint8_t **out_data, size_t *out_len)
{
    if (out_data == NULL || out_len == NULL) {
        return SAVVY_ERR_INVALID_ARGUMENT;
    }
    if (input == NULL && input_len > 0) {
        return SAVVY_ERR_INVALID_ARGUMENT;
    }
    if (input_len > SENSOR_BZIP_MAX_INPUT) {
        return SAVVY_ERR_OVERFLOW;
    }
    if (input_len == 0) {
        /* A real bzip2 stream is never 0 bytes (see header doc) - this
         * is a documented decompress-specific rejection, independent of
         * compress()'s empty-input behavior. */
        return SAVVY_ERR_PROTOCOL;
    }

    bz_stream strm;
    memset(&strm, 0, sizeof(strm));

    int bzrc = BZ2_bzDecompressInit(&strm, SENSOR_BZIP_VERBOSITY, 0 /* small */);
    if (bzrc != BZ_OK) {
        return (bzrc == BZ_MEM_ERROR) ? SAVVY_ERR_OUT_OF_MEMORY : SAVVY_ERR_UNKNOWN;
    }

    /* Decompressed size is not stored in a bzip2 stream, so this is only
     * a starting guess - sensor_bzip_ensure_capacity() grows it as
     * needed regardless of how large the true expansion ratio turns out
     * to be. Guard the *4 multiply against overflow explicitly rather
     * than let it silently wrap for a huge input_len. */
    size_t out_cap = (input_len > SIZE_MAX / 4) ? SIZE_MAX : (input_len * 4);
    if (out_cap < 4096) {
        out_cap = 4096;
    }
    uint8_t *out_buf = (uint8_t *)malloc(out_cap);
    if (out_buf == NULL) {
        BZ2_bzDecompressEnd(&strm);
        return SAVVY_ERR_OUT_OF_MEMORY;
    }
    size_t out_written = 0;

    strm.next_in = (char *)input;
    strm.avail_in = (unsigned int)input_len;

    savvy_status_t status = SAVVY_OK;
    int reached_end = 0;
    unsigned int iterations = 0;
    for (;;) {
        if (++iterations > SENSOR_BZIP_MAX_LOOP_ITERATIONS) {
            status = SAVVY_ERR_UNKNOWN;
            break;
        }

        if (out_written == out_cap) {
            savvy_status_t grow_status = sensor_bzip_ensure_capacity(&out_buf, &out_cap, out_written + 1);
            if (grow_status != SAVVY_OK) {
                status = grow_status;
                break;
            }
        }

        size_t avail_out_now = sensor_min_sz(out_cap - out_written, (size_t)UINT_MAX);
        unsigned int avail_in_before = strm.avail_in;
        strm.next_out = (char *)(out_buf + out_written);
        strm.avail_out = (unsigned int)avail_out_now;

        bzrc = BZ2_bzDecompress(&strm);

        size_t produced = avail_out_now - (size_t)strm.avail_out;
        unsigned int consumed = avail_in_before - strm.avail_in;
        out_written += produced;

        if (bzrc == BZ_STREAM_END) {
            reached_end = 1;
            break;
        }
        if (bzrc != BZ_OK) {
            /* BZ_DATA_ERROR: CRC/structure integrity failure.
             * BZ_DATA_ERROR_MAGIC: input doesn't start with a valid
             * bzip2 magic header at all. BZ_MEM_ERROR: internal bzlib
             * allocation failure. BZ_PARAM_ERROR/BZ_SEQUENCE_ERROR:
             * would indicate a bug in this loop. All are handled
             * without ever crashing or returning partial data as if it
             * were success. */
            status = (bzrc == BZ_MEM_ERROR) ? SAVVY_ERR_OUT_OF_MEMORY : SAVVY_ERR_PROTOCOL;
            break;
        }
        if (produced == 0 && consumed == 0) {
            /* No forward progress at all (neither consumed new input nor
             * produced new output) and not yet at BZ_STREAM_END: bzlib
             * is waiting on more input than we have left to give it -
             * i.e. the supplied compressed data is truncated. Reject
             * rather than spin forever. */
            status = SAVVY_ERR_PROTOCOL;
            break;
        }
    }

    BZ2_bzDecompressEnd(&strm);

    if (status != SAVVY_OK) {
        free(out_buf);
        return status;
    }
    if (!reached_end) {
        /* Defensive: the loop only exits without an error status via
         * the BZ_STREAM_END break above, but guard explicitly in case
         * that invariant is ever violated by a future edit. */
        free(out_buf);
        return SAVVY_ERR_PROTOCOL;
    }

    *out_data = out_buf;
    *out_len = out_written;
    return SAVVY_OK;
}
