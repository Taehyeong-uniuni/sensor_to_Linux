/* SNS-STR-002: DataResult matrix (4 / non-4 / missing / null / numeric
 * string / negative integer / fractional number / fractional string /
 * int32 min-max / out-of-range / nonnumeric / bool / array / object /
 * duplicate last-wins) plus the Stream danger-count threshold contract,
 * the Voice no-counting contract, and the unquoted-key wire normalization
 * (DEC-20260715-SENSOR-STREAM-DATARESULT-WIRE-NORMALIZE). */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>

#include "sensor_stream/result_policy.h"

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond, msg) do { \
    if (cond) { g_pass++; } \
    else { g_fail++; fprintf(stderr, "FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__); } \
} while (0)

typedef struct {
    int calls;
    uint8_t last_start;
} alert_probe_t;

static void probe_alert(uint8_t channel_start, void *ctx)
{
    alert_probe_t *p = (alert_probe_t *)ctx;
    p->calls++;
    p->last_start = channel_start;
}

/* ---- 001: quoted-key DataResult matrix, direct Foundation passthrough ---- */
static void test_001(void)
{
    struct { const char *json; bool expect_ok; bool expect_normal; int32_t expect_value; } cases[] = {
        { "{\"result\":4}", true, true, 4 },
        { "{\"result\":7}", true, false, 7 },
        { "{}", true, false, 0 },                 /* missing key -> success, result=0, danger */
        { "{\"result\":null}", true, false, 0 },   /* null -> success, result=0, danger */
        { "{\"result\":\"4\"}", true, true, 4 },   /* numeric string, integer format -> success */
        { "{\"result\":\"-1\"}", true, false, -1 }, /* negative integer string -> success */
        { "{\"result\":4.9}", false, false, 0 },   /* fractional number -> parse error */
        { "{\"result\":\"4.9\"}", false, false, 0 }, /* fractional string -> parse error */
        { "{\"result\":2147483647}", true, false, 2147483647 }, /* int32 max -> success */
        { "{\"result\":-2147483648}", true, false, INT32_MIN }, /* int32 min -> success */
        { "{\"result\":2147483648}", false, false, 0 }, /* out of int32 range -> parse error */
        { "{\"result\":\"abc\"}", false, false, 0 }, /* nonnumeric string -> parse error */
        { "{\"result\":true}", false, false, 0 },   /* bool -> parse error */
        { "{\"result\":[1]}", false, false, 0 },    /* array -> parse error */
        { "{\"result\":{}}", false, false, 0 },     /* object -> parse error */
        { "{\"result\":4,\"result\":7}", true, false, 7 }, /* duplicate key, last wins -> danger */
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        sensor_result_policy_t *p = NULL;
        alert_probe_t probe = {0};
        savvy_status_t st = sensor_result_policy_create(&p, SENSOR_RESULT_ROLE_VOICE, 4, probe_alert, &probe);
        CHECK(st == SAVVY_OK, "create voice policy for matrix case");

        sensor_result_policy_on_response(p, cases[i].json, strlen(cases[i].json));

        if (cases[i].expect_ok) {
            /* Voice fires an alert iff the parsed value is non-normal (danger). */
            bool expect_alert = !cases[i].expect_normal;
            CHECK(probe.calls == (expect_alert ? 1 : 0), cases[i].json);
        } else {
            /* Parse failure: documented no-op, never fires an alert. */
            CHECK(probe.calls == 0, cases[i].json);
        }
        sensor_result_policy_destroy(p);
    }
}

/* ---- 002: real-server unquoted-key wire quirk is bridged transparently ---- */
static void test_002(void)
{
    /* Exact bytes the pinned streaming_server_v2 actually emits. */
    const char *unquoted_normal = "{result:4}";
    const char *unquoted_danger = "{result:7}";
    const char *unquoted_with_space = "{result: 4}"; /* the exception-fallback path's exact form */

    sensor_result_policy_t *p = NULL;
    alert_probe_t probe = {0};
    sensor_result_policy_create(&p, SENSOR_RESULT_ROLE_VOICE, 4, probe_alert, &probe);

    sensor_result_policy_on_response(p, unquoted_normal, strlen(unquoted_normal));
    CHECK(probe.calls == 0, "unquoted normal result must not alert");

    sensor_result_policy_on_response(p, unquoted_danger, strlen(unquoted_danger));
    CHECK(probe.calls == 1, "unquoted danger result must alert exactly once");
    CHECK(probe.last_start == (uint8_t)'V', "voice alert reports Start='V'");

    probe.calls = 0;
    sensor_result_policy_on_response(p, unquoted_with_space, strlen(unquoted_with_space));
    CHECK(probe.calls == 0, "unquoted-with-space normal result must not alert");

    /* Counted packet bodies are not C strings. These exact-size arrays
     * deliberately have no byte available at body[body_len]; sanitizer
     * coverage proves both quoted and rewritten parser inputs get their
     * own NUL terminator. */
    const char quoted_without_nul[] = {'{','"','r','e','s','u','l','t','"',':','4','}'};
    sensor_result_policy_on_response(p, quoted_without_nul, sizeof(quoted_without_nul));
    CHECK(probe.calls == 0, "quoted counted body without a terminator parses safely");

    const char unquoted_without_nul[] = {'{','r','e','s','u','l','t',':','4','}'};
    sensor_result_policy_on_response(p, unquoted_without_nul, sizeof(unquoted_without_nul));
    CHECK(probe.calls == 0, "unquoted counted body without a terminator parses safely");

    const char *unquoted_negative = "{result:-1}";
    sensor_result_policy_on_response(p, unquoted_negative, strlen(unquoted_negative));
    CHECK(probe.calls == 1, "unquoted negative result must alert exactly once");

    sensor_result_policy_destroy(p);

    /* Negative control: the normalizer must NOT rewrite input that does
     * not exactly match the bareword-result-key pattern - e.g. a
     * different bareword key must still fail to parse (not be coerced
     * into "result"). */
    sensor_result_policy_t *p2 = NULL;
    alert_probe_t probe2 = {0};
    sensor_result_policy_create(&p2, SENSOR_RESULT_ROLE_VOICE, 4, probe_alert, &probe2);
    const char *resultx = "{resultx:7}";
    sensor_result_policy_on_response(p2, resultx, strlen(resultx));
    CHECK(probe2.calls == 0, "resultx bareword key must not be rewritten");

    const char *myresult = "{myresult:7}";
    sensor_result_policy_on_response(p2, myresult, strlen(myresult));
    CHECK(probe2.calls == 0, "myresult bareword key must not be rewritten");

    const char *result_in_string = "{\"text\":\"result:\",\"result\":4}";
    sensor_result_policy_on_response(p2, result_in_string, strlen(result_in_string));
    CHECK(probe2.calls == 0, "result: inside a string must remain unchanged");

    const char *truncated = "{result:4";
    sensor_result_policy_on_response(p2, truncated, strlen(truncated));
    CHECK(probe2.calls == 0, "truncated unquoted input must remain a parse-failure no-op");

    sensor_result_policy_on_response(p2, "", 0);
    CHECK(probe2.calls == 0, "empty input must remain a no-op");
    sensor_result_policy_destroy(p2);
}

/* ---- 003: Stream danger-count threshold - exact pre-increment->=compare, reset-on-normal ---- */
static void test_003(void)
{
    sensor_result_policy_t *p = NULL;
    alert_probe_t probe = {0};
    sensor_result_policy_create(&p, SENSOR_RESULT_ROLE_STREAM, 4, probe_alert, &probe);

    const char *danger = "{\"result\":7}";
    const char *normal = "{\"result\":4}";

    sensor_result_policy_on_response(p, danger, strlen(danger));
    CHECK(sensor_result_policy_danger_count(p) == 1, "count==1 after first danger");
    CHECK(probe.calls == 0, "no alert below threshold (1<4)");

    sensor_result_policy_on_response(p, danger, strlen(danger));
    sensor_result_policy_on_response(p, danger, strlen(danger));
    CHECK(sensor_result_policy_danger_count(p) == 3, "count==3 after three danger responses");
    CHECK(probe.calls == 0, "no alert below threshold (3<4)");

    sensor_result_policy_on_response(p, danger, strlen(danger)); /* 4th -> reaches threshold */
    CHECK(sensor_result_policy_danger_count(p) == 4, "count==4 at threshold");
    CHECK(probe.calls == 1, "alert fires exactly at threshold (4>=4)");
    CHECK(probe.last_start == (uint8_t)'S', "stream alert reports Start='S'");

    sensor_result_policy_on_response(p, danger, strlen(danger)); /* 5th -> still >= threshold, fires again */
    CHECK(probe.calls == 2, "alert continues firing above threshold, matching pinned per-response re-check");

    sensor_result_policy_on_response(p, normal, strlen(normal)); /* any normal resets to 0 */
    CHECK(sensor_result_policy_danger_count(p) == 0, "count resets to 0 on any normal result");

    sensor_result_policy_destroy(p);
}

/* ---- 004: Voice never counts - alert fires on the very first non-normal, no threshold ---- */
static void test_004(void)
{
    sensor_result_policy_t *p = NULL;
    alert_probe_t probe = {0};
    /* Threshold argument is accepted but must be entirely ignored for Voice. */
    sensor_result_policy_create(&p, SENSOR_RESULT_ROLE_VOICE, 100, probe_alert, &probe);

    const char *danger = "{\"result\":7}";
    sensor_result_policy_on_response(p, danger, strlen(danger));
    CHECK(probe.calls == 1, "voice alerts on the very first non-normal result, no threshold");
    CHECK(sensor_result_policy_danger_count(p) == 0, "voice never maintains a counter");

    sensor_result_policy_destroy(p);
}

/* ---- 005: parse-error / no-body no-ops (malformed JSON, CRC-failed-as-empty) never touch state ---- */
static void test_005(void)
{
    sensor_result_policy_t *p = NULL;
    alert_probe_t probe = {0};
    sensor_result_policy_create(&p, SENSOR_RESULT_ROLE_STREAM, 2, probe_alert, &probe);

    const char *danger = "{\"result\":7}";
    sensor_result_policy_on_response(p, danger, strlen(danger));
    CHECK(sensor_result_policy_danger_count(p) == 1, "count==1 before no-op probes");

    const char *malformed = "not json at all {{{";
    sensor_result_policy_on_response(p, malformed, strlen(malformed));
    CHECK(sensor_result_policy_danger_count(p) == 1, "malformed JSON must not change the counter");
    CHECK(probe.calls == 0, "malformed JSON must not alert");

    sensor_result_policy_on_response(p, NULL, 0); /* simulates a CRC-failed response */
    CHECK(sensor_result_policy_danger_count(p) == 1, "NULL/0-length body (CRC-failed) must not change the counter");
    CHECK(probe.calls == 0, "NULL/0-length body must not alert");

    sensor_result_policy_on_response(p, "", 0);
    CHECK(sensor_result_policy_danger_count(p) == 1, "empty-string body must not change the counter");

    sensor_result_policy_destroy(p);
}

/* ---- 006: sensor_result_policy_reset() mirrors pinned setInitSecInfo() ---- */
static void test_006(void)
{
    sensor_result_policy_t *p = NULL;
    sensor_result_policy_create(&p, SENSOR_RESULT_ROLE_STREAM, 4, NULL, NULL);

    const char *danger = "{\"result\":7}";
    sensor_result_policy_on_response(p, danger, strlen(danger));
    sensor_result_policy_on_response(p, danger, strlen(danger));
    CHECK(sensor_result_policy_danger_count(p) == 2, "count==2 before reset");

    sensor_result_policy_reset(p);
    CHECK(sensor_result_policy_danger_count(p) == 0, "reset() zeroes the counter");

    sensor_result_policy_destroy(p);

    /* reset() on a Voice policy is a documented no-op (nothing to reset). */
    sensor_result_policy_t *pv = NULL;
    sensor_result_policy_create(&pv, SENSOR_RESULT_ROLE_VOICE, 4, NULL, NULL);
    sensor_result_policy_reset(pv); /* must not crash */
    CHECK(sensor_result_policy_danger_count(pv) == 0, "voice count is always 0");
    sensor_result_policy_destroy(pv);
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
    printf("\n=== SNS-STR-002 sub-%s: %d passed, %d failed ===\n", argv[1], g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
