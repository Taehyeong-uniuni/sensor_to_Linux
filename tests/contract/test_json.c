/*
 * CT-JSON-001~002 (session_tasks/CC-FOUNDATION.md "Required tests").
 * Usage: test_json <001|002>
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "savvy/protocol/config_codec.h"
#include "savvy/protocol/device_codec.h"
#include "savvy/protocol/data_result_codec.h"

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(name, cond) do { \
        if (cond) { g_pass++; printf("[PASS] %s\n", (name)); } \
        else      { g_fail++; printf("[FAIL] %s\n", (name)); } \
    } while (0)

/* F-13: capture unknown_key_log_fn invocations so the test can confirm
 * both that logging fires and what it was called with (never the
 * unknown key's value - the callback doesn't even receive one). */
static char g_logged_object[128];
static char g_logged_key[128];
static int g_log_calls;

static void capture_unknown_key(const char *object_name, const char *key_name)
{
    g_log_calls++;
    strncpy(g_logged_object, object_name, sizeof(g_logged_object) - 1);
    g_logged_object[sizeof(g_logged_object) - 1] = '\0';
    strncpy(g_logged_key, key_name, sizeof(g_logged_key) - 1);
    g_logged_key[sizeof(g_logged_key) - 1] = '\0';
}

static void reset_log_capture(void)
{
    g_log_calls = 0;
    g_logged_object[0] = '\0';
    g_logged_key[0] = '\0';
}

/* ---- CT-JSON-001: Config/Device full field matrix behavior ---- */
static void test_json_001(void)
{
    /* Full field round-trip: build a fully-populated struct, build to
     * JSON, parse back, expect byte-for-byte struct equality. */
    {
        savvy_config_t a, b;
        savvy_config_set_defaults(&a);
        strcpy(a.server_ip, "10.0.0.5");
        strcpy(a.ftp_ip, "10.0.0.6");
        strcpy(a.select_wifi, "MyWifi");
        strcpy(a.select_beacon, "MyBeacon");
        a.danger_count = 7;
        a.decibel = 55;
        strcpy(a.keep_server_ip, "192.168.1.1");
        a.use_rknn = 1;

        char *json = NULL;
        savvy_status_t st = savvy_config_build(&a, &json);
        CHECK("001 Config: fully-populated build ok", st == SAVVY_OK);

        savvy_config_set_defaults(&b);
        st = savvy_config_parse(json, strlen(json), &b, NULL);
        CHECK("001 Config: fully-populated round-trip struct-identical",
              st == SAVVY_OK && memcmp(&a, &b, sizeof(a)) == 0);
        free(json);
    }
    {
        savvy_device_t a, b;
        savvy_device_set_defaults(&a);
        strcpy(a.device_serial, "SAVVY-0000001");
        strcpy(a.device_mac, "AA:BB:CC:DD:EE:FF");
        a.data_collection = 1;
        a.smoke_value = 42;

        char *json = NULL;
        savvy_status_t st = savvy_device_build(&a, &json);
        CHECK("001 Device: fully-populated build ok", st == SAVVY_OK);

        savvy_device_set_defaults(&b);
        st = savvy_device_parse(json, strlen(json), &b, NULL);
        CHECK("001 Device: fully-populated round-trip struct-identical",
              st == SAVVY_OK && memcmp(&a, &b, sizeof(a)) == 0);
        free(json);
    }

    /* Missing fields: absent keys keep the Android-source default. */
    {
        savvy_config_t cfg;
        savvy_config_set_defaults(&cfg);
        const char *json = "{\"serverIp\":\"1.2.3.4\"}";
        savvy_status_t st = savvy_config_parse(json, strlen(json), &cfg, NULL);
        CHECK("001 Config: missing fields parse ok", st == SAVVY_OK);
        CHECK("001 Config: missing dangerCount kept default (4)", cfg.danger_count == 4);
        CHECK("001 Config: missing decibel kept default (100000)", cfg.decibel == 100000);
        CHECK("001 Config: present serverIp applied", strcmp(cfg.server_ip, "1.2.3.4") == 0);
    }
    {
        savvy_device_t dev;
        savvy_device_set_defaults(&dev);
        const char *json = "{\"deviceSerial\":\"X\"}";
        savvy_status_t st = savvy_device_parse(json, strlen(json), &dev, NULL);
        CHECK("001 Device: missing fields parse ok", st == SAVVY_OK);
        CHECK("001 Device: missing dataCollection kept default (0)", dev.data_collection == 0);
    }

    /* Unknown key: ignored, not rejected (forward-compat), and F-13's
     * log_fn is invoked exactly once with (object_name, key_name) - never
     * the key's value. */
    {
        savvy_config_t cfg;
        savvy_config_set_defaults(&cfg);
        reset_log_capture();
        const char *json = "{\"neverHeardOfThisField\":true,\"dangerCount\":9}";
        savvy_status_t st = savvy_config_parse(json, strlen(json), &cfg, capture_unknown_key);
        CHECK("001 Config: unknown key ignored, known key still applied",
              st == SAVVY_OK && cfg.danger_count == 9);
        CHECK("001 Config: unknown key logged exactly once with object/key name",
              g_log_calls == 1 &&
              strcmp(g_logged_object, "jsonConfigDto") == 0 &&
              strcmp(g_logged_key, "neverHeardOfThisField") == 0);
    }

    /* Type mismatch: rejected. */
    {
        savvy_config_t cfg;
        savvy_config_set_defaults(&cfg);
        const char *json1 = "{\"videoTime\":\"four\"}";
        savvy_status_t st = savvy_config_parse(json1, strlen(json1), &cfg, NULL);
        CHECK("001 Config: int field with string value rejected", st == SAVVY_ERR_PROTOCOL);

        const char *json2 = "{\"serverIp\":12345}";
        st = savvy_config_parse(json2, strlen(json2), &cfg, NULL);
        CHECK("001 Config: string field with number value rejected", st == SAVVY_ERR_PROTOCOL);
    }

    /* Fractional/non-integer number on an int32 field: rejected, not
     * truncated (F-04/M-06 - shares savvy_json_number_to_int32 with
     * DataResult's {"result":4.9} regression, so the field table must
     * reject it the same way). */
    {
        savvy_config_t cfg;
        savvy_config_set_defaults(&cfg);
        const char *json = "{\"dangerCount\":4.9}";
        savvy_status_t st = savvy_config_parse(json, strlen(json), &cfg, NULL);
        CHECK("001 Config: fractional number on int field rejected, not truncated",
              st == SAVVY_ERR_PROTOCOL);
    }

    /* Invalid UTF-8 anywhere in the tree: rejected (F-10). Byte sequence
     * is a valid 2-byte UTF-8 lead byte (0xC3) immediately followed by
     * the JSON string's closing quote instead of a continuation byte. */
    {
        savvy_config_t cfg;
        savvy_config_set_defaults(&cfg);
        const char *json = "{\"serverIp\":\"caf\xc3\"}";
        savvy_status_t st = savvy_config_parse(json, strlen(json), &cfg, NULL);
        CHECK("001 Config: truncated multibyte UTF-8 in string value rejected",
              st == SAVVY_ERR_PROTOCOL);
    }

    /* Explicit null: rejected, not silently coerced to 0/"". */
    {
        savvy_config_t cfg;
        savvy_config_set_defaults(&cfg);
        const char *json1 = "{\"dangerCount\":null}";
        savvy_status_t st = savvy_config_parse(json1, strlen(json1), &cfg, NULL);
        CHECK("001 Config: null on int field rejected", st == SAVVY_ERR_PROTOCOL);

        const char *json2 = "{\"serverIp\":null}";
        st = savvy_config_parse(json2, strlen(json2), &cfg, NULL);
        CHECK("001 Config: null on string field rejected", st == SAVVY_ERR_PROTOCOL);
    }

    /* Explicit 0: valid value, distinct from missing (must not be
     * re-overwritten by the field's non-zero default). */
    {
        savvy_config_t cfg;
        savvy_config_set_defaults(&cfg);
        const char *json = "{\"decibel\":0}";
        savvy_status_t st = savvy_config_parse(json, strlen(json), &cfg, NULL);
        CHECK("001 Config: explicit 0 accepted and NOT replaced by default 100000",
              st == SAVVY_OK && cfg.decibel == 0);
    }

    /* Duplicate key in a schema-controlled object: rejected. */
    {
        savvy_config_t cfg;
        savvy_config_set_defaults(&cfg);
        const char *json = "{\"dangerCount\":1,\"dangerCount\":2}";
        savvy_status_t st = savvy_config_parse(json, strlen(json), &cfg, NULL);
        CHECK("001 Config: duplicate key rejected", st == SAVVY_ERR_PROTOCOL);
    }
    {
        savvy_device_t dev;
        savvy_device_set_defaults(&dev);
        const char *json = "{\"mic\":1,\"mic\":0}";
        savvy_status_t st = savvy_device_parse(json, strlen(json), &dev, NULL);
        CHECK("001 Device: duplicate key rejected", st == SAVVY_ERR_PROTOCOL);
    }
}

/* ---- CT-JSON-002: DataResult result semantics ---- */
static void test_json_002(void)
{
    savvy_data_result_t dr;
    savvy_status_t st;
    const char *json;

    json = "{\"result\":4}";
    st = savvy_data_result_parse(json, strlen(json), &dr);
    CHECK("002 result=4 parses ok and is normal",
          st == SAVVY_OK && dr.result == 4 && savvy_data_result_is_normal(&dr));

    json = "{\"result\":7}";
    st = savvy_data_result_parse(json, strlen(json), &dr);
    CHECK("002 result=7 parses ok and is non-normal/danger",
          st == SAVVY_OK && dr.result == 7 && !savvy_data_result_is_normal(&dr));

    int others[] = {5, 6, 8, 99};
    int all_ok = 1;
    for (size_t i = 0; i < sizeof(others) / sizeof(others[0]); i++) {
        char buf[32];
        snprintf(buf, sizeof(buf), "{\"result\":%d}", others[i]);
        st = savvy_data_result_parse(buf, strlen(buf), &dr);
        if (st != SAVVY_OK || savvy_data_result_is_normal(&dr) || dr.result != others[i]) {
            all_ok = 0;
        }
    }
    CHECK("002 result=5/6/8/99 all parse ok and are all non-normal/danger", all_ok);

    json = "{}";
    st = savvy_data_result_parse(json, strlen(json), &dr);
    CHECK("002 missing result key -> parse error (never silently 4)", st == SAVVY_ERR_PROTOCOL);

    json = "{\"result\":null}";
    st = savvy_data_result_parse(json, strlen(json), &dr);
    CHECK("002 null result -> parse error", st == SAVVY_ERR_PROTOCOL);

    json = "{\"result\":\"4\"}";
    st = savvy_data_result_parse(json, strlen(json), &dr);
    CHECK("002 string-typed result -> parse error (type mismatch)", st == SAVVY_ERR_PROTOCOL);

    json = "{\"result\":4,\"result\":7}";
    st = savvy_data_result_parse(json, strlen(json), &dr);
    CHECK("002 duplicate result key -> parse error", st == SAVVY_ERR_PROTOCOL);

    /* F-04/M-06 regression: a fractional result must NOT silently
     * truncate to 4 (normal) - this is the exact case the header's
     * savvy_data_result_parse doc comment calls out. */
    json = "{\"result\":4.9}";
    st = savvy_data_result_parse(json, strlen(json), &dr);
    CHECK("002 fractional result (4.9) -> parse error, not truncated to 4",
          st == SAVVY_ERR_PROTOCOL);
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: %s <001|002>\n", argv[0]);
        return 2;
    }

    if (strcmp(argv[1], "001") == 0) {
        test_json_001();
    } else if (strcmp(argv[1], "002") == 0) {
        test_json_002();
    } else {
        fprintf(stderr, "unknown test id: %s\n", argv[1]);
        return 2;
    }

    printf("\n=== CT-JSON-%s: %d passed, %d failed ===\n", argv[1], g_pass, g_fail);
    return (g_fail == 0) ? 0 : 1;
}
