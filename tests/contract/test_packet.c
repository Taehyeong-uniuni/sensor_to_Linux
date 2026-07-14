/*
 * CT-PKT-001~003 (session_tasks/CC-FOUNDATION.md "Required tests").
 *
 * Golden vectors: per B-002 (08_BLOCKERS.md), no real packet capture is
 * available yet, so goldens are hand-constructed from the wire spec
 * (01_BASELINE.md 4.1), not derived by calling the encoder under test.
 * The one exception is the 4-byte CRC32 field: those 4 bytes are checked
 * against a direct savvy_crc32() call (independent of encode()/decode()),
 * because the CRC32 algorithm itself was already verified separately
 * against the standard CRC-32/ISO-HDLC check value ("123456789" ->
 * 0xCBF43926, see docs referenced in SESSION_RESULT.md) - so this test
 * still catches offset/endianness/wiring bugs without hand-computing a
 * 32-bit CRC by hand.
 *
 * Usage: test_packet <001|002|003>
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "savvy/protocol/packet_codec.h"
#include "savvy/protocol/stream_parser.h"

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(name, cond) do { \
        if (cond) { g_pass++; printf("[PASS] %s\n", (name)); } \
        else      { g_fail++; printf("[FAIL] %s\n", (name)); } \
    } while (0)

static const uint8_t SERIAL_A[SAVVY_PACKET_SERIAL_LEN] = {1,2,3,4,5,6,7,8,9,10,11,12,13,14};

/* ---- CT-PKT-001: 0B / 1B / JSON / binary-file body golden vectors ---- */
static void test_pkt_001(void)
{
    /* Case A: 0-byte body. */
    {
        uint8_t golden[SAVVY_PACKET_HEADER_LEN] = {
            0x01, 0x02, 0x03, 0x04,             /* start,command,device,config */
            0x00, 0x00, 0x00, 0x00,             /* length = 0, big-endian */
            1,2,3,4,5,6,7,8,9,10,11,12,13,14,   /* serial */
            0x00, 0x00, 0x00, 0x00              /* CRC32("") == 0 (well-known: init 0xFFFFFFFF, no bytes, final XOR 0xFFFFFFFF) */
        };
        uint8_t out[SAVVY_PACKET_HEADER_LEN];
        size_t written = 0;
        savvy_status_t st = savvy_packet_encode(0x01, 0x02, 0x03, 0x04, SERIAL_A, SAVVY_PACKET_SERIAL_LEN,
                                                 NULL, 0, out, sizeof(out), &written);
        CHECK("001a encode status OK", st == SAVVY_OK);
        CHECK("001a encode byte-for-byte == golden", written == sizeof(golden) && memcmp(out, golden, sizeof(golden)) == 0);

        savvy_packet_header_t hdr;
        const uint8_t *data = NULL; size_t data_len = 0;
        st = savvy_packet_decode(golden, sizeof(golden), &hdr, &data, &data_len);
        CHECK("001a decode status OK", st == SAVVY_OK);
        CHECK("001a decode matches golden input", hdr.start == 0x01 && hdr.command == 0x02 && hdr.device == 0x03 &&
              hdr.config == 0x04 && hdr.length == 0 && data_len == 0 &&
              memcmp(hdr.serial, SERIAL_A, SAVVY_PACKET_SERIAL_LEN) == 0);
        CHECK("001a CRC32 verification passes (Data>=0 comparable)", savvy_crc32(data, data_len) == hdr.crc32);
    }

    /* Case B: 1-byte body. */
    {
        const uint8_t body[1] = { 0x41 }; /* 'A' */
        uint32_t crc = savvy_crc32(body, 1); /* cross-check reference; see file header comment */
        uint8_t golden[SAVVY_PACKET_HEADER_LEN + 1] = {
            0x11, 0x12, 0x13, 0x14,
            0x00, 0x00, 0x00, 0x01,
            1,2,3,4,5,6,7,8,9,10,11,12,13,14,
            (uint8_t)(crc >> 24), (uint8_t)(crc >> 16), (uint8_t)(crc >> 8), (uint8_t)crc,
            0x41
        };
        uint8_t out[SAVVY_PACKET_HEADER_LEN + 1];
        size_t written = 0;
        savvy_status_t st = savvy_packet_encode(0x11, 0x12, 0x13, 0x14, SERIAL_A, SAVVY_PACKET_SERIAL_LEN,
                                                 body, 1, out, sizeof(out), &written);
        CHECK("001b encode status OK", st == SAVVY_OK);
        CHECK("001b encode byte-for-byte == golden", written == sizeof(golden) && memcmp(out, golden, sizeof(golden)) == 0);

        savvy_packet_header_t hdr;
        const uint8_t *data = NULL; size_t data_len = 0;
        st = savvy_packet_decode(golden, sizeof(golden), &hdr, &data, &data_len);
        CHECK("001b decode status OK", st == SAVVY_OK);
        CHECK("001b decode data matches golden input", data_len == 1 && data[0] == 0x41);
        CHECK("001b CRC32 verification passes", savvy_crc32(data, data_len) == hdr.crc32);
    }

    /* Case C: JSON body. */
    {
        const char *json = "{\"server_ip\":\"10.0.0.5\",\"danger_count\":3}";
        size_t json_len = strlen(json);
        uint32_t crc = savvy_crc32((const uint8_t *)json, json_len);
        uint8_t out[SAVVY_PACKET_HEADER_LEN + 128];
        size_t written = 0;
        savvy_status_t st = savvy_packet_encode(0x21, 0x22, 0x00, 0x00, SERIAL_A, SAVVY_PACKET_SERIAL_LEN,
                                                 (const uint8_t *)json, json_len, out, sizeof(out), &written);
        CHECK("001c encode status OK", st == SAVVY_OK);
        CHECK("001c length field == json_len", written == SAVVY_PACKET_HEADER_LEN + json_len);
        CHECK("001c length bytes big-endian", out[4] == 0 && out[5] == 0 &&
              out[6] == (uint8_t)(json_len >> 8) && out[7] == (uint8_t)json_len);
        CHECK("001c CRC bytes match reference crc32", out[22] == (uint8_t)(crc >> 24) && out[23] == (uint8_t)(crc >> 16) &&
              out[24] == (uint8_t)(crc >> 8) && out[25] == (uint8_t)crc);
        CHECK("001c body bytes verbatim", memcmp(out + SAVVY_PACKET_HEADER_LEN, json, json_len) == 0);

        savvy_packet_header_t hdr;
        const uint8_t *data = NULL; size_t data_len = 0;
        st = savvy_packet_decode(out, written, &hdr, &data, &data_len);
        CHECK("001c decode round-trip", st == SAVVY_OK && data_len == json_len && memcmp(data, json, json_len) == 0);
    }

    /* Case D: binary "file" body (non-text bytes, including 0x00 inside Data). */
    {
        uint8_t body[8] = { 0x00, 0xFF, 0x10, 0x80, 0x7F, 0x01, 0xAB, 0xCD };
        uint32_t crc = savvy_crc32(body, sizeof(body));
        uint8_t out[SAVVY_PACKET_HEADER_LEN + sizeof(body)];
        size_t written = 0;
        savvy_status_t st = savvy_packet_encode(0x31, 0x32, 0x33, 0x34, SERIAL_A, SAVVY_PACKET_SERIAL_LEN,
                                                 body, sizeof(body), out, sizeof(out), &written);
        CHECK("001d encode status OK", st == SAVVY_OK);
        CHECK("001d CRC bytes match reference crc32", out[22] == (uint8_t)(crc >> 24) && out[23] == (uint8_t)(crc >> 16) &&
              out[24] == (uint8_t)(crc >> 8) && out[25] == (uint8_t)crc);
        CHECK("001d binary body verbatim (incl. embedded 0x00)", memcmp(out + SAVVY_PACKET_HEADER_LEN, body, sizeof(body)) == 0);

        savvy_packet_header_t hdr;
        const uint8_t *data = NULL; size_t data_len = 0;
        st = savvy_packet_decode(out, written, &hdr, &data, &data_len);
        CHECK("001d decode round-trip incl. embedded 0x00", st == SAVVY_OK && data_len == sizeof(body) &&
              memcmp(data, body, sizeof(body)) == 0);
    }
}

/* ---- CT-PKT-002: partial header, split body, coalesced packets ---- */
static void test_pkt_002(void)
{
    uint8_t pkt_a[SAVVY_PACKET_HEADER_LEN + 3];
    uint8_t pkt_b[SAVVY_PACKET_HEADER_LEN + 5];
    size_t written_a = 0, written_b = 0;
    const uint8_t body_a[3] = {0xAA, 0xBB, 0xCC};
    const uint8_t body_b[5] = {1, 2, 3, 4, 5};
    savvy_packet_encode(0x01, 0x01, 0x01, 0x01, SERIAL_A, SAVVY_PACKET_SERIAL_LEN, body_a, 3, pkt_a, sizeof(pkt_a), &written_a);
    savvy_packet_encode(0x02, 0x02, 0x02, 0x02, SERIAL_A, SAVVY_PACKET_SERIAL_LEN, body_b, 5, pkt_b, sizeof(pkt_b), &written_b);

    /* Sub-case 1: header arrives 1 byte at a time, then the rest. */
    {
        uint8_t buf[256];
        savvy_stream_parser_t p;
        savvy_stream_parser_init(&p, buf, sizeof(buf));

        int reached_ready = 0;
        for (size_t i = 0; i < written_a; i++) {
            savvy_stream_parser_push(&p, &pkt_a[i], 1);
            savvy_packet_header_t hdr;
            const uint8_t *data = NULL; size_t data_len = 0;
            savvy_stream_result_t r = savvy_stream_parser_try_extract(&p, &hdr, &data, &data_len);
            if (i + 1 < written_a) {
                CHECK("002.1 partial header/body waits for more", r == SAVVY_STREAM_NEED_MORE_DATA);
            } else {
                reached_ready = (r == SAVVY_STREAM_PACKET_READY && data_len == 3 && memcmp(data, body_a, 3) == 0);
            }
        }
        CHECK("002.1 exact byte-by-byte assembly succeeds on last byte", reached_ready);
    }

    /* Sub-case 2: body arrives in several fragments. */
    {
        uint8_t buf[256];
        savvy_stream_parser_t p;
        savvy_stream_parser_init(&p, buf, sizeof(buf));
        savvy_stream_parser_push(&p, pkt_b, SAVVY_PACKET_HEADER_LEN); /* whole header at once */
        savvy_packet_header_t hdr; const uint8_t *data = NULL; size_t data_len = 0;
        savvy_stream_result_t r = savvy_stream_parser_try_extract(&p, &hdr, &data, &data_len);
        CHECK("002.2 header-only is not enough", r == SAVVY_STREAM_NEED_MORE_DATA);

        savvy_stream_parser_push(&p, pkt_b + SAVVY_PACKET_HEADER_LEN, 2);     /* 2 of 5 body bytes */
        r = savvy_stream_parser_try_extract(&p, &hdr, &data, &data_len);
        CHECK("002.2 partial body still waits", r == SAVVY_STREAM_NEED_MORE_DATA);

        savvy_stream_parser_push(&p, pkt_b + SAVVY_PACKET_HEADER_LEN + 2, 3); /* remaining 3 bytes */
        r = savvy_stream_parser_try_extract(&p, &hdr, &data, &data_len);
        CHECK("002.2 assembled correctly after final fragment", r == SAVVY_STREAM_PACKET_READY &&
              data_len == 5 && memcmp(data, body_b, 5) == 0);
    }

    /* Sub-case 3: two complete packets coalesced in a single push(). */
    {
        uint8_t buf[256];
        uint8_t coalesced[sizeof(pkt_a) + sizeof(pkt_b)];
        memcpy(coalesced, pkt_a, written_a);
        memcpy(coalesced + written_a, pkt_b, written_b);

        savvy_stream_parser_t p;
        savvy_stream_parser_init(&p, buf, sizeof(buf));
        savvy_stream_parser_push(&p, coalesced, written_a + written_b);

        savvy_packet_header_t hdr1, hdr2;
        const uint8_t *d1 = NULL, *d2 = NULL; size_t l1 = 0, l2 = 0;
        savvy_stream_result_t r1 = savvy_stream_parser_try_extract(&p, &hdr1, &d1, &l1);
        savvy_stream_result_t r2 = savvy_stream_parser_try_extract(&p, &hdr2, &d2, &l2);
        savvy_packet_header_t hdr3; const uint8_t *d3 = NULL; size_t l3 = 0;
        savvy_stream_result_t r3 = savvy_stream_parser_try_extract(&p, &hdr3, &d3, &l3);

        CHECK("002.3 first coalesced packet parsed separately", r1 == SAVVY_STREAM_PACKET_READY &&
              hdr1.start == 0x01 && l1 == 3 && memcmp(d1, body_a, 3) == 0);
        CHECK("002.3 second coalesced packet parsed separately, no loss/dup", r2 == SAVVY_STREAM_PACKET_READY &&
              hdr2.start == 0x02 && l2 == 5 && memcmp(d2, body_b, 5) == 0);
        CHECK("002.3 nothing left after both extracted", r3 == SAVVY_STREAM_NEED_MORE_DATA);
    }
}

/* ---- CT-PKT-003: endpoint CRC policy, length overflow, serial F02 cases ---- */
static void test_pkt_003(void)
{
    const uint8_t body[4] = { 'T', 'E', 'S', 'T' };
    uint8_t good[SAVVY_PACKET_HEADER_LEN + 4];
    size_t written = 0;
    savvy_packet_encode(0x01, 0x01, 0x01, 0x01, SERIAL_A, SAVVY_PACKET_SERIAL_LEN, body, 4, good, sizeof(good), &written);

    uint8_t corrupted[SAVVY_PACKET_HEADER_LEN + 4];
    memcpy(corrupted, good, sizeof(good));
    corrupted[26] ^= 0xFF; /* flip a Data byte so its CRC no longer matches the header */

    savvy_packet_header_t hdr_good, hdr_bad;
    const uint8_t *dg = NULL, *db = NULL; size_t lg = 0, lb = 0;
    savvy_packet_decode(good, sizeof(good), &hdr_good, &dg, &lg);
    savvy_packet_decode(corrupted, sizeof(corrupted), &hdr_bad, &db, &lb);

    /* Foundation codec: verification is the CALLER's choice per endpoint,
     * per 01_BASELINE.md 4.2 and CC-FOUNDATION.md FND-01. These four
     * sub-cases simulate each endpoint policy at the call site. */

    /* MGR IfComm inbound: compare CRC when Data >= 1B (here Data=4B, so compare). */
    CHECK("003 MGR-IfComm: clean packet passes CRC compare", savvy_crc32(dg, lg) == hdr_good.crc32);
    CHECK("003 MGR-IfComm: corrupted packet fails CRC compare", savvy_crc32(db, lb) != hdr_bad.crc32);

    /* MGR IfComm inbound, 0B Data: comparison is skipped entirely (no reject). */
    {
        uint8_t zero_body_pkt[SAVVY_PACKET_HEADER_LEN];
        size_t w = 0;
        savvy_packet_encode(0x01, 0x01, 0x01, 0x01, SERIAL_A, SAVVY_PACKET_SERIAL_LEN, NULL, 0, zero_body_pkt, sizeof(zero_body_pkt), &w);
        savvy_packet_header_t hdr0; const uint8_t *d0 = NULL; size_t l0 = 0;
        savvy_status_t st = savvy_packet_decode(zero_body_pkt, w, &hdr0, &d0, &l0);
        /* Policy: 0B Data => endpoint must not attempt CRC comparison at all;
         * the codec itself never forces this, it only needs to decode cleanly. */
        CHECK("003 MGR-IfComm 0B Data decodes cleanly (comparison is skipped by policy, not by codec)", st == SAVVY_OK && l0 == 0);
    }

    /* MGR BT inbound: no CRC recalculation verification at all - decode
     * must succeed regardless of whether the embedded CRC is "valid". */
    CHECK("003 MGR-BT: decode succeeds without any CRC check (policy performs none)",
          savvy_packet_decode(corrupted, sizeof(corrupted), &hdr_bad, &db, &lb) == SAVVY_OK);

    /* Sensor 8141 response inbound: response body CRC IS compared. */
    CHECK("003 Sensor-8141: clean response passes CRC compare", savvy_crc32(dg, lg) == hdr_good.crc32);
    CHECK("003 Sensor-8141: corrupted response fails CRC compare", savvy_crc32(db, lb) != hdr_bad.crc32);

    /* keep_alive/streaming server inbound: CRC field is read but body is
     * not recompared - again, decode must not force verification. */
    CHECK("003 keepalive/streaming: CRC field readable without recompare", hdr_bad.crc32 == hdr_good.crc32 /* same original crc value present */
          && savvy_crc32(db, lb) != hdr_bad.crc32 /* recompute would show mismatch, but policy never asks for it */);

    /* Length overflow: header claims more Data than the buffer holds. */
    {
        uint8_t overflow_pkt[SAVVY_PACKET_HEADER_LEN + 4];
        memcpy(overflow_pkt, good, sizeof(good));
        overflow_pkt[4] = 0xFF; overflow_pkt[5] = 0xFF; overflow_pkt[6] = 0xFF; overflow_pkt[7] = 0xFF;
        savvy_packet_header_t hdr; const uint8_t *d = NULL; size_t l = 0;
        savvy_status_t st = savvy_packet_decode(overflow_pkt, sizeof(overflow_pkt), &hdr, &d, &l);
        CHECK("003 length overflow rejected (no out-of-bounds read)", st == SAVVY_ERR_PROTOCOL);
    }

    /* Serial F02 cases (09_TRACEABILITY_MATRIX.md F02): empty/13/14/15/multibyte. */
    {
        savvy_status_t st;
        uint8_t out[64]; size_t w = 0;

        st = savvy_packet_encode(1, 1, 1, 1, SERIAL_A, 0, body, 4, out, sizeof(out), &w); /* empty */
        CHECK("003 F02 serial empty rejected", st == SAVVY_ERR_INVALID_ARGUMENT);

        st = savvy_packet_encode(1, 1, 1, 1, SERIAL_A, 13, body, 4, out, sizeof(out), &w); /* 13B */
        CHECK("003 F02 serial 13B rejected", st == SAVVY_ERR_INVALID_ARGUMENT);

        st = savvy_packet_encode(1, 1, 1, 1, SERIAL_A, 14, body, 4, out, sizeof(out), &w); /* exactly 14B */
        CHECK("003 F02 serial 14B accepted", st == SAVVY_OK);

        uint8_t serial15[15]; memset(serial15, 0x41, sizeof(serial15));
        st = savvy_packet_encode(1, 1, 1, 1, serial15, 15, body, 4, out, sizeof(out), &w); /* 15B */
        CHECK("003 F02 serial 15B rejected", st == SAVVY_ERR_INVALID_ARGUMENT);

        /* multibyte (UTF-8) byte length != 14: e.g. a 5-codepoint UTF-8
         * string whose BYTE length is not 14 must be rejected the same way. */
        const uint8_t multibyte[] = { 0xEC, 0x95, 0x88, 0xEB, 0x85, 0x95 }; /* "안녕", 6 bytes, 2 codepoints */
        st = savvy_packet_encode(1, 1, 1, 1, multibyte, sizeof(multibyte), body, 4, out, sizeof(out), &w);
        CHECK("003 F02 multibyte byte-length != 14 rejected", st == SAVVY_ERR_INVALID_ARGUMENT);
    }
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: %s <001|002|003>\n", argv[0]);
        return 2;
    }

    if (strcmp(argv[1], "001") == 0) {
        test_pkt_001();
    } else if (strcmp(argv[1], "002") == 0) {
        test_pkt_002();
    } else if (strcmp(argv[1], "003") == 0) {
        test_pkt_003();
    } else {
        fprintf(stderr, "unknown test id: %s\n", argv[1]);
        return 2;
    }

    printf("\n=== CT-PKT-%s: %d passed, %d failed ===\n", argv[1], g_pass, g_fail);
    return (g_fail == 0) ? 0 : 1;
}
