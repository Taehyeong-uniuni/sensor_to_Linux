/* SNS-CORE-001 (config half), SNS-CORE-002, SNS-CORE-005 (config half).
 * Dispatches on argv[1] (numeric subtest id), mirroring the Foundation
 * contract-test convention (tests/contract/test_ipc.c etc). */
#include "config_store.h"
#include "device_store.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* assert() is compiled out under -DNDEBUG (Release builds), which would
 * silently drop both the side-effecting calls under test and the
 * verification of their results. CHECK() is deliberately independent of
 * NDEBUG so Release and Debug run the identical test body. */
#define CHECK(cond, msg) do { \
    if (!(cond)) { \
        fprintf(stderr, "CHECK failed: %s (%s:%d)\n", (msg), __FILE__, __LINE__); \
        abort(); \
    } \
} while (0)

static void test_001_device_stateful_field_reset(void) {
    sensor_device_store_t store;
    CHECK(sensor_device_store_init(&store) == SAVVY_OK, "device store init succeeds");

    const char *cached =
        "{\"deviceSerial\":\"ABC123\",\"blueTooth\":1,\"mic\":1,\"wifi\":1,"
        "\"tof\":1,\"led\":1,\"buzzer\":1,\"moveSensor\":1,\"beacon\":1,"
        "\"reboot\":1,\"dataCollection\":1}";
    CHECK(sensor_device_store_load_cached(&store, cached, strlen(cached)) == SAVVY_OK,
          "device store loads cached JSON");

    uint64_t version = 0;
    savvy_snapshot_handle_t *handle = sensor_device_store_acquire(&store, &version);
    CHECK(handle != NULL, "device store acquire returns a handle");
    const savvy_device_t *device = sensor_device_snapshot_payload(handle);

    /* The 9 stateful fields must be reset to 0 at startup regardless of
     * the cached JSON. */
    CHECK(device->blue_tooth == 0, "blueTooth reset to 0 at startup");
    CHECK(device->mic == 0, "mic reset to 0 at startup");
    CHECK(device->wifi == 0, "wifi reset to 0 at startup");
    CHECK(device->tof == 0, "tof reset to 0 at startup");
    CHECK(device->led == 0, "led reset to 0 at startup");
    CHECK(device->buzzer == 0, "buzzer reset to 0 at startup");
    CHECK(device->move_sensor == 0, "moveSensor reset to 0 at startup");
    CHECK(device->beacon == 0, "beacon reset to 0 at startup");
    CHECK(device->reboot == 0, "reboot reset to 0 at startup");

    /* Non-stateful fields must survive the cache load untouched. */
    CHECK(strcmp(device->device_serial, "ABC123") == 0, "deviceSerial survives cache load");
    CHECK(device->data_collection == 1, "dataCollection survives cache load");

    sensor_device_store_release(&store, handle);
    sensor_device_store_destroy(&store);

    /* Empty cache -> pure defaults, still reset (all-zero defaults already
     * satisfy reset, but exercise the empty-cache branch explicitly). */
    sensor_device_store_t store2;
    CHECK(sensor_device_store_init(&store2) == SAVVY_OK, "second device store init succeeds");
    CHECK(sensor_device_store_load_cached(&store2, NULL, 0) == SAVVY_OK,
          "device store loads empty cache");
    handle = sensor_device_store_acquire(&store2, &version);
    CHECK(handle != NULL, "second device store acquire returns a handle");
    device = sensor_device_snapshot_payload(handle);
    CHECK(device->blue_tooth == 0 && device->reboot == 0, "empty-cache defaults are reset");
    sensor_device_store_release(&store2, handle);
    sensor_device_store_destroy(&store2);

    printf("SNS-CORE-001(config): OK\n");
}

static void test_002_config_full_replacement_and_raw_json(void) {
    sensor_config_store_t store;
    CHECK(sensor_config_store_init(&store) == SAVVY_OK, "config store init succeeds");
    CHECK(sensor_config_store_load_cached(&store, NULL, 0) == SAVVY_OK,
          "config store loads empty cache");

    /* First apply provides a complete live value. Unknown keys remain
     * tolerated, while the exact wire JSON is retained with the snapshot. */
    const char *json1 =
        "{\"serverIp\":\"10.0.0.9\",\"decibel\":55,\"compress\":1,"
        "\"useRknn\":1,\"totallyUnknownKey\":123}";
    sensor_config_apply_result_t result;
    savvy_status_t st = sensor_config_store_apply_runtime(&store, json1, strlen(json1), &result);
    CHECK(st == SAVVY_OK, "first apply_runtime succeeds");
    CHECK(result.server_ip_changed == true, "first apply reports serverIp changed");
    CHECK(strcmp(result.server_ip, "10.0.0.9") == 0, "first apply reports new serverIp value");
    CHECK(result.use_rknn_raw_changed == true, "first apply reports useRknn raw changed");
    CHECK(result.use_rknn_raw_old == 0, "first apply reports old useRknn raw value");
    CHECK(result.use_rknn_raw_new == 1, "first apply reports new useRknn raw value");

    uint64_t version = 0;
    savvy_snapshot_handle_t *handle = sensor_config_store_acquire(&store, &version);
    CHECK(handle != NULL, "config store acquire returns a handle");
    const savvy_config_t *cfg = sensor_config_snapshot_payload(handle);
    /* decibel/compress are unused keys from CORE's perspective: stored
     * faithfully, but not part of the selective-apply reaction. */
    CHECK(cfg->decibel == 55, "decibel stored faithfully after first apply");
    CHECK(cfg->compress == 1, "compress stored faithfully after first apply");
    size_t raw_len = 0;
    const char *raw = sensor_config_snapshot_raw_json(handle, &raw_len);
    CHECK(raw_len == strlen(json1), "raw JSON length matches first apply payload");
    CHECK(memcmp(raw, json1, raw_len) == 0, "raw JSON bytes match first apply payload");
    sensor_config_store_release(&store, handle);

    /* A partial runtime message replaces rather than merges. Fields omitted
     * from json2 must be Foundation defaults, not the old live values. */
    const char *json2 = "{\"decibel\":70,\"totallyUnknownKey\":456}";
    st = sensor_config_store_apply_runtime(&store, json2, strlen(json2), &result);
    CHECK(st == SAVVY_OK, "second apply_runtime succeeds");
    CHECK(result.server_ip_changed == true, "second apply reports serverIp changed back to default");
    CHECK(result.use_rknn_raw_changed == true,
          "second apply reports useRknn raw changed back to default");

    handle = sensor_config_store_acquire(&store, &version);
    cfg = sensor_config_snapshot_payload(handle);
    savvy_config_t defaults;
    savvy_config_set_defaults(&defaults);
    CHECK(cfg->decibel == 70, "decibel updated by second apply");
    CHECK(strcmp(cfg->server_ip, defaults.server_ip) == 0, "serverIp reset to default by second apply");
    CHECK(cfg->use_rknn == defaults.use_rknn, "useRknn reset to default by second apply");
    CHECK(cfg->compress == defaults.compress, "compress reset to default by second apply");
    raw = sensor_config_snapshot_raw_json(handle, &raw_len);
    CHECK(raw_len == strlen(json2), "raw JSON length matches second apply payload");
    CHECK(memcmp(raw, json2, raw_len) == 0, "raw JSON bytes match second apply payload");
    sensor_config_store_release(&store, handle);

    sensor_config_store_destroy(&store);
    printf("SNS-CORE-002: OK\n");
}

static void test_005_malformed_config_device(void) {
    /* --- Config side --- */
    sensor_config_store_t cstore;
    CHECK(sensor_config_store_init(&cstore) == SAVVY_OK, "config store init succeeds");
    CHECK(sensor_config_store_load_cached(&cstore, NULL, 0) == SAVVY_OK,
          "config store loads empty cache");

    sensor_config_apply_result_t cresult;
    size_t raw_len = 0;
    const char *raw = NULL;

    const char *full = "{\"serverIp\":\"10.0.0.9\",\"decibel\":5,\"useRknn\":1}";
    CHECK(sensor_config_store_apply_runtime(&cstore, full, strlen(full), &cresult) == SAVVY_OK,
          "well-formed config apply succeeds");

    uint64_t version = 0;
    savvy_snapshot_handle_t *h = sensor_config_store_acquire(&cstore, &version);
    const savvy_config_t *cfg_before = sensor_config_snapshot_payload(h);
    char server_ip_before[SAVVY_CONFIG_STR_LEN];
    memcpy(server_ip_before, cfg_before->server_ip, sizeof(server_ip_before));
    int32_t decibel_before = cfg_before->decibel;
    size_t raw_before_len = 0;
    const char *raw_before = sensor_config_snapshot_raw_json(h, &raw_before_len);
    char raw_before_copy[256];
    CHECK(raw_before_len < sizeof(raw_before_copy), "raw_before fits in local copy buffer");
    memcpy(raw_before_copy, raw_before, raw_before_len);
    sensor_config_store_release(&cstore, h);

    /* Syntax-malformed JSON -> rejected, state untouched. */
    const char *bad_syntax = "{\"decibel\":";
    CHECK(sensor_config_store_apply_runtime(&cstore, bad_syntax, strlen(bad_syntax), &cresult) != SAVVY_OK,
          "syntax-malformed JSON is rejected");

    /* Wrong type for a known int field -> rejected. */
    const char *wrong_type = "{\"decibel\":\"not_a_number\"}";
    CHECK(sensor_config_store_apply_runtime(&cstore, wrong_type, strlen(wrong_type), &cresult) != SAVVY_OK,
          "wrong-typed decibel is rejected");

    /* Null for a known field -> rejected. */
    const char *null_field = "{\"serverIp\":null}";
    CHECK(sensor_config_store_apply_runtime(&cstore, null_field, strlen(null_field), &cresult) != SAVVY_OK,
          "null serverIp is rejected");

    /* Duplicate key anywhere in the tree -> rejected. */
    const char *dup_key = "{\"decibel\":1,\"decibel\":2}";
    CHECK(sensor_config_store_apply_runtime(&cstore, dup_key, strlen(dup_key), &cresult) != SAVVY_OK,
          "duplicate key is rejected");

    /* After all the rejects, serverIp must be exactly what it was before
     * any of the rejected calls (no partial mutation leaked through). */
    h = sensor_config_store_acquire(&cstore, &version);
    const savvy_config_t *cfg_after = sensor_config_snapshot_payload(h);
    CHECK(strcmp(cfg_after->server_ip, server_ip_before) == 0,
          "serverIp untouched after rejected applies");
    CHECK(cfg_after->decibel == decibel_before, "decibel untouched after rejected applies");
    raw = sensor_config_snapshot_raw_json(h, &raw_len);
    CHECK(raw_len == raw_before_len, "raw JSON length untouched after rejected applies");
    CHECK(memcmp(raw, raw_before_copy, raw_len) == 0,
          "raw JSON bytes untouched after rejected applies");
    sensor_config_store_release(&cstore, h);

    /* Extra/unknown key alongside valid ones is tolerated, and a partial
     * replacement resets the omitted old serverIp to defaults. */
    const char *extra_key = "{\"decibel\":9,\"neverHeardOfThisKey\":true}";
    CHECK(sensor_config_store_apply_runtime(&cstore, extra_key, strlen(extra_key), &cresult) == SAVVY_OK,
          "unknown key alongside valid ones is tolerated");
    h = sensor_config_store_acquire(&cstore, &version);
    cfg_after = sensor_config_snapshot_payload(h);
    savvy_config_t config_defaults;
    savvy_config_set_defaults(&config_defaults);
    CHECK(strcmp(cfg_after->server_ip, config_defaults.server_ip) == 0,
          "omitted serverIp resets to default");
    CHECK(cfg_after->decibel == 9, "decibel updated by partial apply");
    sensor_config_store_release(&cstore, h);

    sensor_config_store_destroy(&cstore);

    /* --- Device side --- */
    sensor_device_store_t dstore;
    CHECK(sensor_device_store_init(&dstore) == SAVVY_OK, "device store init succeeds");
    CHECK(sensor_device_store_load_cached(&dstore, NULL, 0) == SAVVY_OK,
          "device store loads empty cache");

    sensor_device_apply_result_t dresult;
    const char *device_full = "{\"deviceSerial\":\"old\",\"mic\":1,\"dataCollection\":1}";
    CHECK(sensor_device_store_apply_runtime(&dstore, device_full, strlen(device_full), &dresult) == SAVVY_OK,
          "well-formed device apply succeeds");
    const char *bad_device_syntax = "{\"blueTooth\":";
    CHECK(sensor_device_store_apply_runtime(&dstore, bad_device_syntax, strlen(bad_device_syntax), &dresult) != SAVVY_OK,
          "syntax-malformed device JSON is rejected");

    const char *bad_device_type = "{\"blueTooth\":\"on\"}";
    CHECK(sensor_device_store_apply_runtime(&dstore, bad_device_type, strlen(bad_device_type), &dresult) != SAVVY_OK,
          "wrong-typed blueTooth is rejected");

    const char *device_dup_key = "{\"mic\":1,\"mic\":0}";
    CHECK(sensor_device_store_apply_runtime(&dstore, device_dup_key, strlen(device_dup_key), &dresult) != SAVVY_OK,
          "duplicate device key is rejected");

    h = sensor_device_store_acquire(&dstore, &version);
    const savvy_device_t *device_before = sensor_device_snapshot_payload(h);
    CHECK(strcmp(device_before->device_serial, "old") == 0,
          "deviceSerial untouched after rejected applies");
    raw = sensor_device_snapshot_raw_json(h, &raw_len);
    CHECK(raw_len == strlen(device_full), "device raw JSON length untouched after rejected applies");
    CHECK(memcmp(raw, device_full, raw_len) == 0,
          "device raw JSON bytes untouched after rejected applies");
    sensor_device_store_release(&dstore, h);

    const char *device_partial = "{\"dataCollection\":0,\"unknown\":true}";
    CHECK(sensor_device_store_apply_runtime(&dstore, device_partial, strlen(device_partial), &dresult) == SAVVY_OK,
          "partial device apply with unknown key succeeds");
    h = sensor_device_store_acquire(&dstore, &version);
    const savvy_device_t *device = sensor_device_snapshot_payload(h);
    savvy_device_t device_defaults;
    savvy_device_set_defaults(&device_defaults);
    CHECK(strcmp(device->device_serial, device_defaults.device_serial) == 0,
          "omitted deviceSerial resets to default");
    CHECK(device->mic == device_defaults.mic, "omitted mic resets to default");
    raw = sensor_device_snapshot_raw_json(h, &raw_len);
    CHECK(raw_len == strlen(device_partial), "device raw JSON length matches partial payload");
    CHECK(memcmp(raw, device_partial, raw_len) == 0,
          "device raw JSON bytes match partial payload");
    sensor_device_store_release(&dstore, h);

    sensor_device_store_destroy(&dstore);

    printf("SNS-CORE-005(config+device): OK\n");
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s <subtest-id>\n", argv[0]);
        return 2;
    }

    if (strcmp(argv[1], "001") == 0) {
        test_001_device_stateful_field_reset();
    } else if (strcmp(argv[1], "002") == 0) {
        test_002_config_full_replacement_and_raw_json();
    } else if (strcmp(argv[1], "005") == 0) {
        test_005_malformed_config_device();
    } else {
        fprintf(stderr, "unknown subtest id: %s\n", argv[1]);
        return 2;
    }

    return 0;
}
