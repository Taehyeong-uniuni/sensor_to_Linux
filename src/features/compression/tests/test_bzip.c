/*
 * SNS-STR-005 (BZip compress/decompress contract) plus the BZip half of
 * SNS-STR-004 ("유효 WAV를 libbz2로 compress·decompress, 원본과
 * byte-for-byte 일치" - sub-case 005 below, which composes with Part 1's
 * sensor_wav_wrap() directly, proving the two features integrate
 * end-to-end). The WAV-header half of SNS-STR-004 lives in
 * src/features/wav/tests/test_wav.c instead, per this session's
 * directory-ownership split.
 *
 * Usage: test_bzip <001|002|003|004|005|006>
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sensor_stream/bzip.h"
#include "sensor_stream/wav.h"

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(name, cond) do { \
        if (cond) { g_pass++; printf("[PASS] %s\n", (name)); } \
        else      { g_fail++; printf("[FAIL] %s\n", (name)); } \
    } while (0)

/* Deterministic xorshift32 PRNG - NOT cryptographic, just needs to be
 * high-entropy/incompressible from a general-purpose compressor's point
 * of view (bzip2's BWT+Huffman stages find no exploitable statistical
 * redundancy in this output), and reproducible across runs. */
static uint32_t g_rng_state;

static uint32_t next_rand(void)
{
    uint32_t x = g_rng_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    g_rng_state = x;
    return x;
}

static void fill_random(uint8_t *buf, size_t n, uint32_t seed)
{
    g_rng_state = seed ? seed : 1u;
    for (size_t i = 0; i < n; i++) {
        buf[i] = (uint8_t)(next_rand() & 0xFFu);
    }
}

/* Deliberately repetitive/structured "realistic-ish" content - the kind
 * of data bzip2's BWT stage compresses well, as a contrast case to the
 * random data above. */
static void fill_repetitive(uint8_t *buf, size_t n)
{
    static const char pattern[] = "sensor-stream SNS-04 WAV/BZip fixture data ";
    size_t plen = sizeof(pattern) - 1;
    for (size_t i = 0; i < n; i++) {
        buf[i] = (uint8_t)pattern[i % plen];
    }
}

static void check_roundtrip(const char *label, const uint8_t *original, size_t original_len)
{
    char name[160];

    uint8_t *compressed = NULL;
    size_t compressed_len = 0;
    savvy_status_t st = sensor_bzip_compress(original, original_len, &compressed, &compressed_len);

    snprintf(name, sizeof(name), "%s compress status OK", label);
    CHECK(name, st == SAVVY_OK);

    snprintf(name, sizeof(name), "%s compress output pointer non-NULL", label);
    CHECK(name, compressed != NULL);

    if (st != SAVVY_OK || compressed == NULL) {
        free(compressed);
        return;
    }

    uint8_t *decompressed = NULL;
    size_t decompressed_len = 0;
    st = sensor_bzip_decompress(compressed, compressed_len, &decompressed, &decompressed_len);

    snprintf(name, sizeof(name), "%s decompress status OK", label);
    CHECK(name, st == SAVVY_OK);

    snprintf(name, sizeof(name), "%s decompressed length == original length", label);
    CHECK(name, decompressed_len == original_len);

    snprintf(name, sizeof(name), "%s decompressed bytes byte-for-byte identical to original", label);
    CHECK(name, (original_len == 0) || (decompressed != NULL && memcmp(decompressed, original, original_len) == 0));

    free(compressed);
    free(decompressed);
}

/* ---- 001: round-trip empty/small/few-KB/high-entropy-random inputs. ---- */
static void test_001(void)
{
    check_roundtrip("001 empty", NULL, 0);

    uint8_t small[7] = { 'a', 'b', 'c', 0x00, 0xFF, 0x7F, 0x80 };
    check_roundtrip("001 small(7B, incl. embedded 0x00/high-bit bytes)", small, sizeof(small));

    size_t kb_len = 5000;
    uint8_t *kb_buf = (uint8_t *)malloc(kb_len);
    fill_repetitive(kb_buf, kb_len);
    check_roundtrip("001 few-KB realistic-ish repetitive data", kb_buf, kb_len);
    free(kb_buf);

    size_t rand_len = 8192;
    uint8_t *rand_buf = (uint8_t *)malloc(rand_len);
    fill_random(rand_buf, rand_len, 0xC0FFEEu);
    check_roundtrip("001 high-entropy/random data", rand_buf, rand_len);
    free(rand_buf);
}

/* ---- 002: compressed output verifiably LARGER than input for
 * high-entropy/incompressible data; must still be correct, not
 * truncated/clamped/falling back to raw input. ---- */
static void test_002(void)
{
    size_t input_len = 4096;
    uint8_t *input = (uint8_t *)malloc(input_len);
    fill_random(input, input_len, 0x5EED5EEDu);

    uint8_t *compressed = NULL;
    size_t compressed_len = 0;
    savvy_status_t st = sensor_bzip_compress(input, input_len, &compressed, &compressed_len);
    CHECK("002 compress status OK", st == SAVVY_OK);
    CHECK("002 compressed output IS larger than input for high-entropy data (no clamping)",
          st == SAVVY_OK && compressed_len > input_len);
    /* Also confirm it isn't a disguised passthrough of the raw input
     * under a different length claim. */
    CHECK("002 compressed bytes are NOT simply the raw input (real compression occurred)",
          st == SAVVY_OK && (compressed_len != input_len || memcmp(compressed, input, input_len) != 0));

    if (st == SAVVY_OK) {
        uint8_t *decompressed = NULL;
        size_t decompressed_len = 0;
        st = sensor_bzip_decompress(compressed, compressed_len, &decompressed, &decompressed_len);
        CHECK("002 decompress of the larger-than-input stream status OK", st == SAVVY_OK);
        CHECK("002 decompressed length matches original exactly", st == SAVVY_OK && decompressed_len == input_len);
        CHECK("002 decompressed bytes byte-for-byte identical to original (no truncation)",
              st == SAVVY_OK && decompressed != NULL && memcmp(decompressed, input, input_len) == 0);
        free(decompressed);
    }

    free(compressed);
    free(input);
}

/* ---- 003: decompressing invalid/corrupt/truncated data returns
 * SAVVY_ERR_PROTOCOL cleanly - never crashes. ---- */
static void test_003(void)
{
    uint8_t *out = NULL;
    size_t out_len = 0;
    savvy_status_t st;

    /* Pure garbage, not even a valid bzip2 magic header. */
    uint8_t garbage[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x11, 0x22, 0x33, 0x44, 0x55 };
    st = sensor_bzip_decompress(garbage, sizeof(garbage), &out, &out_len);
    CHECK("003 pure garbage input rejected as SAVVY_ERR_PROTOCOL", st == SAVVY_ERR_PROTOCOL);
    CHECK("003 pure garbage input: out left untouched (no partial-success)", out == NULL);

    /* A real compressed stream, truncated partway through. */
    uint8_t *real_input = NULL;
    size_t real_input_len = 6000;
    real_input = (uint8_t *)malloc(real_input_len);
    fill_repetitive(real_input, real_input_len);

    uint8_t *compressed = NULL;
    size_t compressed_len = 0;
    st = sensor_bzip_compress(real_input, real_input_len, &compressed, &compressed_len);
    CHECK("003 (setup) compress of real_input succeeds", st == SAVVY_OK && compressed_len > 4);

    if (st == SAVVY_OK && compressed_len > 4) {
        size_t truncated_len = compressed_len / 2;
        out = NULL; out_len = 0;
        st = sensor_bzip_decompress(compressed, truncated_len, &out, &out_len);
        CHECK("003 truncated valid stream rejected as SAVVY_ERR_PROTOCOL (not crashed, not a false success)",
              st == SAVVY_ERR_PROTOCOL);
        CHECK("003 truncated valid stream: out left untouched", out == NULL);

        /* Flip a byte in the middle of an otherwise-complete stream. */
        uint8_t *corrupted = (uint8_t *)malloc(compressed_len);
        memcpy(corrupted, compressed, compressed_len);
        corrupted[compressed_len / 2] ^= 0xFF;
        out = NULL; out_len = 0;
        st = sensor_bzip_decompress(corrupted, compressed_len, &out, &out_len);
        CHECK("003 single-byte-corrupted stream rejected as SAVVY_ERR_PROTOCOL",
              st == SAVVY_ERR_PROTOCOL);
        CHECK("003 single-byte-corrupted stream: out left untouched", out == NULL);
        free(corrupted);
    }

    free(compressed);
    free(real_input);
}

/* ---- 004: empty-input compress documented behavior - succeeds,
 * produces a real (nonzero-length) bzip2 stream, decompress round-trips
 * it back to 0 bytes. ---- */
static void test_004(void)
{
    uint8_t *compressed = NULL;
    size_t compressed_len = 0;
    savvy_status_t st = sensor_bzip_compress(NULL, 0, &compressed, &compressed_len);
    CHECK("004 compress(NULL, 0) succeeds (documented: empty input is not an error)", st == SAVVY_OK);
    CHECK("004 compress(NULL, 0) output pointer non-NULL", compressed != NULL);
    CHECK("004 compress(NULL, 0) produces a real (nonzero-length) bzip2 stream", compressed_len > 0);

    if (st == SAVVY_OK && compressed != NULL) {
        uint8_t *decompressed = NULL;
        size_t decompressed_len = 12345; /* poison value to prove it gets overwritten to 0 */
        st = sensor_bzip_decompress(compressed, compressed_len, &decompressed, &decompressed_len);
        CHECK("004 decompressing that stream succeeds", st == SAVVY_OK);
        CHECK("004 decompressing that stream yields exactly 0 bytes", st == SAVVY_OK && decompressed_len == 0);
        CHECK("004 decompress output pointer still non-NULL even for a 0-byte result",
              st == SAVVY_OK && decompressed != NULL);
        free(decompressed);
    }
    free(compressed);

    /* decompress(NULL, 0) is documented as the opposite decision:
     * rejected, since 0 bytes is never a valid bzip2 stream on the wire. */
    uint8_t *out = NULL;
    size_t out_len = 0;
    st = sensor_bzip_decompress(NULL, 0, &out, &out_len);
    CHECK("004 decompress(NULL, 0) rejected as SAVVY_ERR_PROTOCOL (documented: 0 bytes is never a valid stream)",
          st == SAVVY_ERR_PROTOCOL);
}

/* ---- 005: SNS-STR-004 BZip half - compose with Part 1's
 * sensor_wav_wrap(): compress/decompress actual WAV-wrapped bytes and
 * verify byte-for-byte equality against the original WAV bytes. ---- */
static void test_005(void)
{
    size_t pcm_len = 4000;
    uint8_t *pcm = (uint8_t *)malloc(pcm_len);
    fill_repetitive(pcm, pcm_len);

    uint8_t *wav = NULL;
    size_t wav_len = 0;
    savvy_status_t st = sensor_wav_wrap(pcm, pcm_len, &wav, &wav_len);
    CHECK("005 (setup) sensor_wav_wrap succeeds", st == SAVVY_OK && wav != NULL && wav_len == 44 + pcm_len);

    if (st == SAVVY_OK) {
        check_roundtrip("005 SNS-STR-004: compress/decompress actual sensor_wav_wrap() output", wav, wav_len);
    }

    free(wav);
    free(pcm);
}

/* ---- 006: repeated compress/decompress loop, no memory leaks. ----
 *
 * Leak-checking method actually used (see this session's final report
 * for full detail): LeakSanitizer's `detect_leaks` is NOT supported on
 * macOS/Darwin at all (confirmed empirically - ASan itself hangs during
 * its own shadow-memory init on this host's macOS/Xcode combination,
 * reproduced with a completely trivial one-malloc ASan program unrelated
 * to this code, so ASan/LSan verification was not achievable here); this
 * loop was instead verified leak-free with macOS's `leaks` command-line
 * tool (`leaks --atExit -- ./test_bzip 006`), which reported "0 leaks
 * for 0 total leaked bytes" - and the same command was additionally run
 * against every other sub-case (001-005) to cover the compress/decompress
 * error-return paths this success-only loop does not exercise. UBSan
 * (`-fsanitize=undefined`, which does not depend on ASan's shadow memory
 * and works fine on this host) was also run clean across all sub-cases.
 * Every allocation in this loop is additionally freed before the next
 * iteration begins, visible directly by inspection below. */
static void test_006(void)
{
    const int iterations = 100;
    int ok_count = 0;

    for (int i = 0; i < iterations; i++) {
        size_t len = (size_t)(100 + (i * 37) % 4000);
        uint8_t *buf = (uint8_t *)malloc(len);
        if (i % 2 == 0) {
            fill_repetitive(buf, len);
        } else {
            fill_random(buf, len, (uint32_t)(0x1000 + i));
        }

        uint8_t *compressed = NULL;
        size_t compressed_len = 0;
        savvy_status_t st1 = sensor_bzip_compress(buf, len, &compressed, &compressed_len);

        uint8_t *decompressed = NULL;
        size_t decompressed_len = 0;
        savvy_status_t st2 = SAVVY_ERR_UNKNOWN;
        if (st1 == SAVVY_OK) {
            st2 = sensor_bzip_decompress(compressed, compressed_len, &decompressed, &decompressed_len);
        }

        if (st1 == SAVVY_OK && st2 == SAVVY_OK &&
            decompressed_len == len && memcmp(decompressed, buf, len) == 0) {
            ok_count++;
        }

        free(compressed);
        free(decompressed);
        free(buf);
    }

    CHECK("006 100 compress/decompress iterations all round-trip correctly", ok_count == iterations);
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: %s <001|002|003|004|005|006>\n", argv[0]);
        return 2;
    }

    if (strcmp(argv[1], "001") == 0) {
        test_001();
    } else if (strcmp(argv[1], "002") == 0) {
        test_002();
    } else if (strcmp(argv[1], "003") == 0) {
        test_003();
    } else if (strcmp(argv[1], "004") == 0) {
        test_004();
    } else if (strcmp(argv[1], "005") == 0) {
        test_005();
    } else if (strcmp(argv[1], "006") == 0) {
        test_006();
    } else {
        fprintf(stderr, "unknown test id: %s\n", argv[1]);
        return 2;
    }

    printf("\n=== SNS-STR-005-BZIP-%s: %d passed, %d failed ===\n", argv[1], g_pass, g_fail);
    return (g_fail == 0) ? 0 : 1;
}
