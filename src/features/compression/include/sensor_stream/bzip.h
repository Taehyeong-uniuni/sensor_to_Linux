#ifndef SENSOR_STREAM_BZIP_H
#define SENSOR_STREAM_BZIP_H

/*
 * SNS-04 (BZip): compress/decompress wrapper around the system libbz2,
 * producing/consuming a standard bzip2 stream.
 *
 * Port of the pinned Android app's BZip2 usage pattern (`savvy_sensor`
 * repo, commit 48e2d1442cd867cc60f8ff3186d53fce1c08f308,
 * app/src/main/cpp/bzip-lib.cpp, Java_..._BZip_compressStreamTofJNI) -
 * used here only as the reference for the streaming BZ2 API shape
 * (BZ2_bzCompressInit/BZ2_bzCompress/BZ2_bzCompressEnd at level 9).
 *
 * Unlike that reference, this implementation does NOT assume the
 * compressed output fits in a buffer the same size as the input: bzip2
 * output can legitimately be LARGER than the input for high-entropy/
 * incompressible data, and this module grows its output buffer as
 * needed to always return the true, correct, correctly-sized compressed
 * (or decompressed) bytes - never truncated, never clamped, and never a
 * silent fallback to the raw/uncompressed input.
 *
 * The real production server (streaming_server_v2) decompresses with
 * Apache Commons Compress's standard BZip2CompressorInputStream, i.e. a
 * plain bzip2 stream - so wire compatibility with any standard libbz2
 * implementation is exactly the goal here; nothing exotic or
 * non-standard is produced.
 */

#include <stddef.h>
#include <stdint.h>

#include "savvy/core/error.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Compresses `input` (input_len bytes) into a standard bzip2 stream, at
 * compression level 9 (matching the pinned Android reference).
 *
 * On SAVVY_OK, *out_data is a newly malloc'd buffer of *out_len bytes
 * that the caller must release with free(). *out_data is ALWAYS a valid
 * non-NULL pointer whenever SAVVY_OK is returned, even when *out_len is
 * small; the caller never needs to NULL-check before free().
 *
 * Design decisions (documented, not left implicit):
 *   - input == NULL is only accepted when input_len == 0; otherwise
 *     SAVVY_ERR_INVALID_ARGUMENT.
 *   - input_len == 0 (empty input) is NOT an error: it succeeds and
 *     produces a real, minimal, valid bzip2 stream for zero bytes of
 *     content (i.e. compress() never special-cases "empty" into a
 *     placeholder/sentinel result - its output is always a genuine
 *     bzip2 stream that sensor_bzip_decompress(), or any standard
 *     bzip2 reader, can parse back into 0 bytes).
 *   - This call processes at most UINT_MAX (~4 GiB) bytes of input in
 *     one shot, because libbz2's streaming API expresses avail_in as a
 *     32-bit `unsigned int`; input_len beyond that returns
 *     SAVVY_ERR_OVERFLOW rather than silently truncating a size_t into
 *     a narrower field. (Sensor/audio payloads in this system are many
 *     orders of magnitude smaller than this limit.)
 *   - The compressed output is NEVER assumed to fit in a same-size (or
 *     any fixed-size) buffer: the output buffer grows as needed, so
 *     output larger than input_len is handled correctly, not
 *     truncated/clamped/silently replaced with the raw input.
 *   - out_data == NULL || out_len == NULL -> SAVVY_ERR_INVALID_ARGUMENT.
 *   - malloc/realloc failure, or an internal libbz2 allocation failure
 *     -> SAVVY_ERR_OUT_OF_MEMORY.
 *   - On any non-SAVVY_OK return, *out_data and *out_len are left
 *     untouched and no memory is leaked (BZ2_bzCompressEnd is always
 *     called exactly once for every successful BZ2_bzCompressInit).
 */
savvy_status_t sensor_bzip_compress(const uint8_t *input, size_t input_len,
                                     uint8_t **out_data, size_t *out_len);

/*
 * Decompresses a standard bzip2 stream `input` (input_len bytes) -
 * either produced by sensor_bzip_compress() or by any standard bzip2
 * encoder (wire format compatible with Apache Commons Compress's
 * BZip2CompressorInputStream, used by this system's production server).
 *
 * On SAVVY_OK, *out_data is a newly malloc'd buffer of *out_len bytes
 * that the caller must release with free(); *out_data is always a valid
 * non-NULL pointer whenever SAVVY_OK is returned, even when *out_len is
 * 0.
 *
 * Design decisions (documented, not left implicit):
 *   - input == NULL is only accepted when input_len == 0; otherwise
 *     SAVVY_ERR_INVALID_ARGUMENT.
 *   - input_len == 0 IS treated as an error (SAVVY_ERR_PROTOCOL): a
 *     genuine bzip2 stream is never zero bytes long (even the minimal
 *     empty-content stream sensor_bzip_compress(NULL, 0, ...) produces
 *     is a small but nonzero number of bytes), so an empty buffer is
 *     never valid compressed input. This is an independent decision
 *     from compress()'s empty-input behavior above (the two are not
 *     required to mirror each other) and is documented/tested as such.
 *   - Any malformed, corrupt, truncated, or otherwise-invalid compressed
 *     input (including a stream that stops before reaching a valid
 *     end-of-stream marker) returns SAVVY_ERR_PROTOCOL. This never
 *     crashes, never reads or writes out of bounds, and never returns a
 *     partially-decompressed prefix while reporting SAVVY_OK - failure
 *     is always all-or-nothing.
 *   - This call processes at most UINT_MAX (~4 GiB) bytes of compressed
 *     input in one shot (see sensor_bzip_compress() for why); beyond
 *     that, SAVVY_ERR_OVERFLOW.
 *   - The decompressed output size is not known ahead of time (bzip2
 *     does not store it in the stream) and the output buffer grows as
 *     needed - arbitrarily large expansion ratios are handled
 *     correctly, not truncated.
 *   - out_data == NULL || out_len == NULL -> SAVVY_ERR_INVALID_ARGUMENT.
 *   - malloc/realloc failure, or an internal libbz2 allocation failure
 *     -> SAVVY_ERR_OUT_OF_MEMORY.
 *   - On any non-SAVVY_OK return, *out_data and *out_len are left
 *     untouched and no memory is leaked (BZ2_bzDecompressEnd is always
 *     called exactly once for every successful BZ2_bzDecompressInit).
 *
 * Neither function ever falls back to returning the raw/uncompressed
 * input when compression "fails" or "would grow", or when decompression
 * fails: every non-SAVVY_OK return is an explicit, unambiguous error
 * status, never a disguised passthrough of the original bytes.
 */
savvy_status_t sensor_bzip_decompress(const uint8_t *input, size_t input_len,
                                       uint8_t **out_data, size_t *out_len);

#ifdef __cplusplus
}
#endif

#endif /* SENSOR_STREAM_BZIP_H */
