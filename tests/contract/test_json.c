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
        st = savvy_config_parse(json, &b);
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
        st = savvy_device_parse(json, &b);
        CHECK("001 Device: fully-populated round-trip struct-identical",
              st == SAVVY_OK && memcmp(&a, &b, sizeof(a)) == 0);
        free(json);
    }

    /* Missing fields: absent keys keep the Android-source default. */
    {
        savvy_config_t cfg;
        savvy_config_set_defaults(&cfg);
        savvy_status_t st = savvy_config_parse("{\"serverIp\":\"1.2.3.4\"}", &cfg);
        CHECK("001 Config: missing fields parse ok", st == SAVVY_OK);
        CHECK("001 Config: missing dangerCount kept default (4)", cfg.danger_count == 4);
        CHECK("001 Config: missing decibel kept default (100000)", cfg.decibel == 100000);
        CHECK("001 Config: present serverIp applied", strcmp(cfg.server_ip, "1.2.3.4") == 0);
    }
    {
        savvy_device_t dev;
        savvy_device_set_defaults(&dev);
        savvy_status_t st = savvy_device_parse("{\"deviceSerial\":\"X\"}", &dev);
        CHECK("001 Device: missing fields parse ok", st == SAVVY_OK);
        CHECK("001 Device: missing dataCollection kept default (0)", dev.data_collection == 0);
    }

    /* Unknown key: ignored, not rejected (forward-compat). */
    {
        savvy_config_t cfg;
        savvy_config_set_defaults(&cfg);
        savvy_status_t st = savvy_config_parse("{\"neverHeardOfThisField\":true,\"dangerCount\":9}", &cfg);
        CHECK("001 Config: unknown key ignored, known key still applied",
              st == SAVVY_OK && cfg.danger_count == 9);
    }

    /* Type mismatch: rejected. */
    {
        savvy_config_t cfg;
        savvy_config_set_defaults(&cfg);
        savvy_status_t st = savvy_config_parse("{\"videoTime\":\"four\"}", &cfg);
        CHECK("001 Config: int field with string value rejected", st == SAVVY_ERR_PROTOCOL);

        st = savvy_config_parse("{\"serverIp\":12345}", &cfg);
        CHECK("001 Config: string field with number value rejected", st == SAVVY_ERR_PROTOCOL);
    }

    /* Explicit null: rejected, not silently coerced to 0/"". */
    {
        savvy_config_t cfg;
        savvy_config_set_defaults(&cfg);
        savvy_status_t st = savvy_config_parse("{\"dangerCount\":null}", &cfg);
        CHECK("001 Config: null on int field rejected", st == SAVVY_ERR_PROTOCOL);

        st = savvy_config_parse("{\"serverIp\":null}", &cfg);
        CHECK("001 Config: null on string field rejected", st == SAVVY_ERR_PROTOCOL);
    }

    /* Explicit 0: valid value, distinct from missing (must not be
     * re-overwritten by the field's non-zero default). */
    {
        savvy_config_t cfg;
        savvy_config_set_defaults(&cfg);
        savvy_status_t st = savvy_config_parse("{\"decibel\":0}", &cfg);
        CHECK("001 Config: explicit 0 accepted and NOT replaced by default 100000",
              st == SAVVY_OK && cfg.decibel == 0);
    }

    /* Duplicate key in a schema-controlled object: rejected. */
    {
        savvy_config_t cfg;
        savvy_config_set_defaults(&cfg);
        savvy_status_t st = savvy_config_parse("{\"dangerCount\":1,\"dangerCount\":2}", &cfg);
        CHECK("001 Config: duplicate key rejected", st == SAVVY_ERR_PROTOCOL);
    }
    {
        savvy_device_t dev;
        savvy_device_set_defaults(&dev);
        savvy_status_t st = savvy_device_parse("{\"mic\":1,\"mic\":0}", &dev);
        CHECK("001 Device: duplicate key rejected", st == SAVVY_ERR_PROTOCOL);
    }
}

/* ---- CT-JSON-002: DataResult result semantics ---- */
static void test_json_002(void)
{
    savvy_data_result_t dr;
    savvy_status_t st;

    st = savvy_data_result_parse("{\"result\":4}", &dr);
    CHECK("002 result=4 parses ok and is normal",
          st == SAVVY_OK && dr.result == 4 && savvy_data_result_is_normal(&dr));

    st = savvy_data_result_parse("{\"result\":7}", &dr);
    CHECK("002 result=7 parses ok and is non-normal/danger",
          st == SAVVY_OK && dr.result == 7 && !savvy_data_result_is_normal(&dr));

    int others[] = {5, 6, 8, 99};
    int all_ok = 1;
    for (size_t i = 0; i < sizeof(others) / sizeof(others[0]); i++) {
        char buf[32];
        snprintf(buf, sizeof(buf), "{\"result\":%d}", others[i]);
        st = savvy_data_result_parse(buf, &dr);
        if (st != SAVVY_OK || savvy_data_result_is_normal(&dr) || dr.result != others[i]) {
            all_ok = 0;
        }
    }
    CHECK("002 result=5/6/8/99 all parse ok and are all non-normal/danger", all_ok);

    st = savvy_data_result_parse("{}", &dr);
    CHECK("002 missing result key -> parse error (never silently 4)", st == SAVVY_ERR_PROTOCOL);

    st = savvy_data_result_parse("{\"result\":null}", &dr);
    CHECK("002 null result -> parse error", st == SAVVY_ERR_PROTOCOL);

    st = savvy_data_result_parse("{\"result\":\"4\"}", &dr);
    CHECK("002 string-typed result -> parse error (type mismatch)", st == SAVVY_ERR_PROTOCOL);

    st = savvy_data_result_parse("{\"result\":4,\"result\":7}", &dr);
    CHECK("002 duplicate result key -> parse error", st == SAVVY_ERR_PROTOCOL);
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
