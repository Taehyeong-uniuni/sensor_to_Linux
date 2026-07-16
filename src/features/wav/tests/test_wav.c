/*
 * SNS-STR-004 (WAV half): sensor_wav_wrap() header/format/overflow
 * coverage. The BZip round-trip half of SNS-STR-004 (compress/decompress
 * an actual sensor_wav_wrap() output byte-for-byte) lives in
 * src/features/compression/tests/test_bzip.c instead, per this session's
 * directory-ownership split.
 *
 * All field checks below decode the produced bytes back out manually,
 * field by field, using LOCAL helpers (rd_u16le/rd_u32le) that are
 * intentionally independent of wav_encoder.c's own put_u16_le/put_u32_le -
 * so a bug in the encoder's byte-order logic can't cancel itself out
 * against an equally-wrong "expectation" computed the same buggy way.
 *
 * Usage: test_wav <001|002|003|004>
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sensor_stream/wav.h"

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(name, cond) do { \
        if (cond) { g_pass++; printf("[PASS] %s\n", (name)); } \
        else      { g_fail++; printf("[FAIL] %s\n", (name)); } \
    } while (0)

static uint16_t rd_u16le(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t rd_u32le(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* Deterministic, non-trivial fake PCM content (not real audio - just
 * needs to be non-repeating byte content so a copy-offset bug would be
 * caught by memcmp). */
static void fill_fake_pcm(uint8_t *buf, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        buf[i] = (uint8_t)((i * 31u + 7u) ^ (i >> 3));
    }
}

/* ---- 001: 44-byte header, magic strings, fixed values, LE encoding ---- */
static void test_001(void)
{
    size_t sizes[] = { 0, 3000 };
    for (size_t si = 0; si < sizeof(sizes) / sizeof(sizes[0]); si++) {
        size_t pcm_len = sizes[si];
        uint8_t *pcm = NULL;
        if (pcm_len > 0) {
            pcm = (uint8_t *)malloc(pcm_len);
            fill_fake_pcm(pcm, pcm_len);
        }

        uint8_t *wav = NULL;
        size_t wav_len = 0;
        savvy_status_t st = sensor_wav_wrap(pcm, pcm_len, &wav, &wav_len);

        char name[96];
        snprintf(name, sizeof(name), "001 pcm_len=%zu wrap status OK", pcm_len);
        CHECK(name, st == SAVVY_OK);

        snprintf(name, sizeof(name), "001 pcm_len=%zu header is exactly 44 bytes (wav_len >= 44)", pcm_len);
        CHECK(name, wav != NULL && wav_len >= 44);

        if (st == SAVVY_OK && wav != NULL && wav_len >= 44) {
            snprintf(name, sizeof(name), "001 pcm_len=%zu RIFF magic", pcm_len);
            CHECK(name, memcmp(wav + 0, "RIFF", 4) == 0);

            snprintf(name, sizeof(name), "001 pcm_len=%zu WAVE magic", pcm_len);
            CHECK(name, memcmp(wav + 8, "WAVE", 4) == 0);

            snprintf(name, sizeof(name), "001 pcm_len=%zu 'fmt ' magic", pcm_len);
            CHECK(name, memcmp(wav + 12, "fmt ", 4) == 0);

            snprintf(name, sizeof(name), "001 pcm_len=%zu 'data' magic", pcm_len);
            CHECK(name, memcmp(wav + 36, "data", 4) == 0);

            snprintf(name, sizeof(name), "001 pcm_len=%zu RIFF chunk size (36+pcm_len) LE @4", pcm_len);
            CHECK(name, rd_u32le(wav + 4) == (uint32_t)(36 + pcm_len));

            snprintf(name, sizeof(name), "001 pcm_len=%zu fmt chunk size == 16 LE @16", pcm_len);
            CHECK(name, rd_u32le(wav + 16) == 16u);

            snprintf(name, sizeof(name), "001 pcm_len=%zu audio format == 1 (PCM) LE @20", pcm_len);
            CHECK(name, rd_u16le(wav + 20) == 1u);

            snprintf(name, sizeof(name), "001 pcm_len=%zu numChannels == 1 (mono) LE @22", pcm_len);
            CHECK(name, rd_u16le(wav + 22) == 1u);

            snprintf(name, sizeof(name), "001 pcm_len=%zu sampleRate == 8000 LE @24", pcm_len);
            CHECK(name, rd_u32le(wav + 24) == 8000u);

            snprintf(name, sizeof(name), "001 pcm_len=%zu byteRate == 16000 LE @28", pcm_len);
            CHECK(name, rd_u32le(wav + 28) == 16000u);

            snprintf(name, sizeof(name), "001 pcm_len=%zu blockAlign == 2 LE @32", pcm_len);
            CHECK(name, rd_u16le(wav + 32) == 2u);

            snprintf(name, sizeof(name), "001 pcm_len=%zu bitsPerSample == 16 LE @34", pcm_len);
            CHECK(name, rd_u16le(wav + 34) == 16u);

            snprintf(name, sizeof(name), "001 pcm_len=%zu data chunk size == pcm_len LE @40", pcm_len);
            CHECK(name, rd_u32le(wav + 40) == (uint32_t)pcm_len);
        }

        free(wav);
        free(pcm);
    }
}

/* ---- 002: chunk-size fields match actual input length; PCM bytes
 * appear byte-for-byte immediately after the 44-byte header. ---- */
static void test_002(void)
{
    size_t sizes[] = { 0, 4096 };
    for (size_t si = 0; si < sizeof(sizes) / sizeof(sizes[0]); si++) {
        size_t pcm_len = sizes[si];
        uint8_t *pcm = NULL;
        if (pcm_len > 0) {
            pcm = (uint8_t *)malloc(pcm_len);
            fill_fake_pcm(pcm, pcm_len);
        }

        uint8_t *wav = NULL;
        size_t wav_len = 0;
        savvy_status_t st = sensor_wav_wrap(pcm, pcm_len, &wav, &wav_len);

        char name[96];
        snprintf(name, sizeof(name), "002 pcm_len=%zu status OK", pcm_len);
        CHECK(name, st == SAVVY_OK);

        if (st == SAVVY_OK) {
            snprintf(name, sizeof(name), "002 pcm_len=%zu total length == 44+pcm_len", pcm_len);
            CHECK(name, wav_len == 44 + pcm_len);

            snprintf(name, sizeof(name), "002 pcm_len=%zu RIFF chunk size field == 36+pcm_len", pcm_len);
            CHECK(name, rd_u32le(wav + 4) == (uint32_t)(36 + pcm_len));

            snprintf(name, sizeof(name), "002 pcm_len=%zu data chunk size field == pcm_len", pcm_len);
            CHECK(name, rd_u32le(wav + 40) == (uint32_t)pcm_len);

            if (pcm_len > 0) {
                snprintf(name, sizeof(name), "002 pcm_len=%zu PCM bytes verbatim immediately after 44-byte header", pcm_len);
                CHECK(name, memcmp(wav + 44, pcm, pcm_len) == 0);
            } else {
                snprintf(name, sizeof(name), "002 pcm_len=0 produces exactly 44 bytes total (empty data chunk)");
                CHECK(name, wav_len == 44);
            }
        }

        free(wav);
        free(pcm);
    }
}

/* ---- 003: invalid-argument and overflow rejection. ---- */
static void test_003(void)
{
    uint8_t dummy_pcm[16];
    fill_fake_pcm(dummy_pcm, sizeof(dummy_pcm));
    uint8_t *wav = NULL;
    size_t wav_len = 0;
    savvy_status_t st;

    /* NULL pcm with nonzero length. */
    st = sensor_wav_wrap(NULL, 16, &wav, &wav_len);
    CHECK("003 pcm==NULL with pcm_len>0 rejected as SAVVY_ERR_INVALID_ARGUMENT", st == SAVVY_ERR_INVALID_ARGUMENT);

    /* NULL pcm with zero length is explicitly NOT an error. */
    wav = NULL; wav_len = 0;
    st = sensor_wav_wrap(NULL, 0, &wav, &wav_len);
    CHECK("003 pcm==NULL with pcm_len==0 succeeds (empty payload is valid)",
          st == SAVVY_OK && wav != NULL && wav_len == 44);
    free(wav);

    /* out_wav == NULL. */
    st = sensor_wav_wrap(dummy_pcm, sizeof(dummy_pcm), NULL, &wav_len);
    CHECK("003 out_wav==NULL rejected as SAVVY_ERR_INVALID_ARGUMENT", st == SAVVY_ERR_INVALID_ARGUMENT);

    /* out_wav_len == NULL. */
    wav = NULL;
    st = sensor_wav_wrap(dummy_pcm, sizeof(dummy_pcm), &wav, NULL);
    CHECK("003 out_wav_len==NULL rejected as SAVVY_ERR_INVALID_ARGUMENT", st == SAVVY_ERR_INVALID_ARGUMENT);

    /* Overflow: pcm_len alone exceeds the 32-bit `data` chunk size field.
     * Safe to pass a small real dummy buffer with a bogus huge claimed
     * length here because sensor_wav_wrap() must perform this check
     * BEFORE ever touching/copying `pcm_len` bytes from `pcm` (mirrors
     * tests/contract/test_packet.c's CT-PKT-001 case F pattern). */
#if SIZE_MAX > UINT32_MAX
    wav = NULL; wav_len = 0;
    st = sensor_wav_wrap(dummy_pcm, (size_t)UINT32_MAX + 1, &wav, &wav_len);
    CHECK("003 pcm_len == UINT32_MAX+1 rejected with SAVVY_ERR_OVERFLOW specifically", st == SAVVY_ERR_OVERFLOW);
#endif

    /* Overflow: pcm_len fits in 32 bits by itself, but 36+pcm_len does
     * not fit in the RIFF chunk size's 32-bit field. */
    wav = NULL; wav_len = 0;
    st = sensor_wav_wrap(dummy_pcm, (size_t)UINT32_MAX, &wav, &wav_len);
    CHECK("003 pcm_len == UINT32_MAX (36+pcm_len overflows 32-bit field) rejected with SAVVY_ERR_OVERFLOW",
          st == SAVVY_ERR_OVERFLOW);
}

/* ---- 004: output pointer non-NULL, out_wav_len == 44+pcm_len on every
 * success case, across several boundary-ish sizes. ---- */
static void test_004(void)
{
    size_t sizes[] = { 0, 1, 2, 44, 255, 256, 65536 };
    for (size_t si = 0; si < sizeof(sizes) / sizeof(sizes[0]); si++) {
        size_t pcm_len = sizes[si];
        uint8_t *pcm = NULL;
        if (pcm_len > 0) {
            pcm = (uint8_t *)malloc(pcm_len);
            fill_fake_pcm(pcm, pcm_len);
        }

        uint8_t *wav = NULL;
        size_t wav_len = 0;
        savvy_status_t st = sensor_wav_wrap(pcm, pcm_len, &wav, &wav_len);

        char name[96];
        snprintf(name, sizeof(name), "004 pcm_len=%zu status OK", pcm_len);
        CHECK(name, st == SAVVY_OK);

        snprintf(name, sizeof(name), "004 pcm_len=%zu output pointer non-NULL", pcm_len);
        CHECK(name, wav != NULL);

        snprintf(name, sizeof(name), "004 pcm_len=%zu out_wav_len == 44+pcm_len exactly", pcm_len);
        CHECK(name, wav_len == 44 + pcm_len);

        free(wav);
        free(pcm);
    }
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: %s <001|002|003|004>\n", argv[0]);
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
    } else {
        fprintf(stderr, "unknown test id: %s\n", argv[1]);
        return 2;
    }

    printf("\n=== SNS-STR-004-WAV-%s: %d passed, %d failed ===\n", argv[1], g_pass, g_fail);
    return (g_fail == 0) ? 0 : 1;
}
