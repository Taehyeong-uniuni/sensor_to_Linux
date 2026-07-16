/* SNS-CORE-001 (config half), SNS-CORE-002, SNS-CORE-005 (config half).
 * Dispatches on argv[1] (numeric subtest id), mirroring the Foundation
 * contract-test convention (tests/contract/test_ipc.c etc). */
#include "config_store.h"
#include "device_store.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_001_device_stateful_field_reset(void) {
    sensor_device_store_t store;
    assert(sensor_device_store_init(&store) == SAVVY_OK);

    const char *cached =
        "{\"deviceSerial\":\"ABC123\",\"blueTooth\":1,\"mic\":1,\"wifi\":1,"
        "\"tof\":1,\"led\":1,\"buzzer\":1,\"moveSensor\":1,\"beacon\":1,"
        "\"reboot\":1,\"dataCollection\":1}";
    assert(sensor_device_store_load_cached(&store, cached, strlen(cached)) == SAVVY_OK);

    uint64_t version = 0;
    savvy_snapshot_handle_t *handle = sensor_device_store_acquire(&store, &version);
    assert(handle != NULL);
    const savvy_device_t *device = sensor_device_snapshot_payload(handle);

    /* The 9 stateful fields must be reset to 0 at startup regardless of
     * the cached JSON. */
    assert(device->blue_tooth == 0);
    assert(device->mic == 0);
    assert(device->wifi == 0);
    assert(device->tof == 0);
    assert(device->led == 0);
    assert(device->buzzer == 0);
    assert(device->move_sensor == 0);
    assert(device->beacon == 0);
    assert(device->reboot == 0);

    /* Non-stateful fields must survive the cache load untouched. */
    assert(strcmp(device->device_serial, "ABC123") == 0);
    assert(device->data_collection == 1);

    sensor_device_store_release(&store, handle);
    sensor_device_store_destroy(&store);

    /* Empty cache -> pure defaults, still reset (all-zero defaults already
     * satisfy reset, but exercise the empty-cache branch explicitly). */
    sensor_device_store_t store2;
    assert(sensor_device_store_init(&store2) == SAVVY_OK);
    assert(sensor_device_store_load_cached(&store2, NULL, 0) == SAVVY_OK);
    handle = sensor_device_store_acquire(&store2, &version);
    assert(handle != NULL);
    device = sensor_device_snapshot_payload(handle);
    assert(device->blue_tooth == 0 && device->reboot == 0);
    sensor_device_store_release(&store2, handle);
    sensor_device_store_destroy(&store2);

    printf("SNS-CORE-001(config): OK\n");
}

static void test_002_config_full_replacement_and_raw_json(void) {
    sensor_config_store_t store;
    assert(sensor_config_store_init(&store) == SAVVY_OK);
    assert(sensor_config_store_load_cached(&store, NULL, 0) == SAVVY_OK);

    /* First apply provides a complete live value. Unknown keys remain
     * tolerated, while the exact wire JSON is retained with the snapshot. */
    const char *json1 =
        "{\"serverIp\":\"10.0.0.9\",\"decibel\":55,\"compress\":1,"
        "\"useRknn\":1,\"totallyUnknownKey\":123}";
    sensor_config_apply_result_t result;
    savvy_status_t st = sensor_config_store_apply_runtime(&store, json1, strlen(json1), &result);
    assert(st == SAVVY_OK);
    assert(result.server_ip_changed == true);
    assert(strcmp(result.server_ip, "10.0.0.9") == 0);
    assert(result.use_rknn_raw_changed == true);
    assert(result.use_rknn_raw_old == 0);
    assert(result.use_rknn_raw_new == 1);

    uint64_t version = 0;
    savvy_snapshot_handle_t *handle = sensor_config_store_acquire(&store, &version);
    assert(handle != NULL);
    const savvy_config_t *cfg = sensor_config_snapshot_payload(handle);
    /* decibel/compress are unused keys from CORE's perspective: stored
     * faithfully, but not part of the selective-apply reaction. */
    assert(cfg->decibel == 55);
    assert(cfg->compress == 1);
    size_t raw_len = 0;
    const char *raw = sensor_config_snapshot_raw_json(handle, &raw_len);
    assert(raw_len == strlen(json1));
    assert(memcmp(raw, json1, raw_len) == 0);
    sensor_config_store_release(&store, handle);

    /* A partial runtime message replaces rather than merges. Fields omitted
     * from json2 must be Foundation defaults, not the old live values. */
    const char *json2 = "{\"decibel\":70,\"totallyUnknownKey\":456}";
    st = sensor_config_store_apply_runtime(&store, json2, strlen(json2), &result);
    assert(st == SAVVY_OK);
    assert(result.server_ip_changed == true);
    assert(result.use_rknn_raw_changed == true);

    handle = sensor_config_store_acquire(&store, &version);
    cfg = sensor_config_snapshot_payload(handle);
    savvy_config_t defaults;
    savvy_config_set_defaults(&defaults);
    assert(cfg->decibel == 70);
    assert(strcmp(cfg->server_ip, defaults.server_ip) == 0);
    assert(cfg->use_rknn == defaults.use_rknn);
    assert(cfg->compress == defaults.compress);
    raw = sensor_config_snapshot_raw_json(handle, &raw_len);
    assert(raw_len == strlen(json2));
    assert(memcmp(raw, json2, raw_len) == 0);
    sensor_config_store_release(&store, handle);

    sensor_config_store_destroy(&store);
    printf("SNS-CORE-002: OK\n");
}

static void test_005_malformed_config_device(void) {
    /* --- Config side --- */
    sensor_config_store_t cstore;
    assert(sensor_config_store_init(&cstore) == SAVVY_OK);
    assert(sensor_config_store_load_cached(&cstore, NULL, 0) == SAVVY_OK);

    sensor_config_apply_result_t cresult;
    size_t raw_len = 0;
    const char *raw = NULL;

    const char *full = "{\"serverIp\":\"10.0.0.9\",\"decibel\":5,\"useRknn\":1}";
    assert(sensor_config_store_apply_runtime(&cstore, full, strlen(full), &cresult) == SAVVY_OK);

    uint64_t version = 0;
    savvy_snapshot_handle_t *h = sensor_config_store_acquire(&cstore, &version);
    const savvy_config_t *cfg_before = sensor_config_snapshot_payload(h);
    char server_ip_before[SAVVY_CONFIG_STR_LEN];
    memcpy(server_ip_before, cfg_before->server_ip, sizeof(server_ip_before));
    int32_t decibel_before = cfg_before->decibel;
    size_t raw_before_len = 0;
    const char *raw_before = sensor_config_snapshot_raw_json(h, &raw_before_len);
    char raw_before_copy[256];
    assert(raw_before_len < sizeof(raw_before_copy));
    memcpy(raw_before_copy, raw_before, raw_before_len);
    sensor_config_store_release(&cstore, h);

    /* Syntax-malformed JSON -> rejected, state untouched. */
    const char *bad_syntax = "{\"decibel\":";
    assert(sensor_config_store_apply_runtime(&cstore, bad_syntax, strlen(bad_syntax), &cresult) != SAVVY_OK);

    /* Wrong type for a known int field -> rejected. */
    const char *wrong_type = "{\"decibel\":\"not_a_number\"}";
    assert(sensor_config_store_apply_runtime(&cstore, wrong_type, strlen(wrong_type), &cresult) != SAVVY_OK);

    /* Null for a known field -> rejected. */
    const char *null_field = "{\"serverIp\":null}";
    assert(sensor_config_store_apply_runtime(&cstore, null_field, strlen(null_field), &cresult) != SAVVY_OK);

    /* Duplicate key anywhere in the tree -> rejected. */
    const char *dup_key = "{\"decibel\":1,\"decibel\":2}";
    assert(sensor_config_store_apply_runtime(&cstore, dup_key, strlen(dup_key), &cresult) != SAVVY_OK);

    /* After all the rejects, serverIp must be exactly what it was before
     * any of the rejected calls (no partial mutation leaked through). */
    h = sensor_config_store_acquire(&cstore, &version);
    const savvy_config_t *cfg_after = sensor_config_snapshot_payload(h);
    assert(strcmp(cfg_after->server_ip, server_ip_before) == 0);
    assert(cfg_after->decibel == decibel_before);
    raw = sensor_config_snapshot_raw_json(h, &raw_len);
    assert(raw_len == raw_before_len);
    assert(memcmp(raw, raw_before_copy, raw_len) == 0);
    sensor_config_store_release(&cstore, h);

    /* Extra/unknown key alongside valid ones is tolerated, and a partial
     * replacement resets the omitted old serverIp to defaults. */
    const char *extra_key = "{\"decibel\":9,\"neverHeardOfThisKey\":true}";
    assert(sensor_config_store_apply_runtime(&cstore, extra_key, strlen(extra_key), &cresult) == SAVVY_OK);
    h = sensor_config_store_acquire(&cstore, &version);
    cfg_after = sensor_config_snapshot_payload(h);
    savvy_config_t config_defaults;
    savvy_config_set_defaults(&config_defaults);
    assert(strcmp(cfg_after->server_ip, config_defaults.server_ip) == 0);
    assert(cfg_after->decibel == 9);
    sensor_config_store_release(&cstore, h);

    sensor_config_store_destroy(&cstore);

    /* --- Device side --- */
    sensor_device_store_t dstore;
    assert(sensor_device_store_init(&dstore) == SAVVY_OK);
    assert(sensor_device_store_load_cached(&dstore, NULL, 0) == SAVVY_OK);

    sensor_device_apply_result_t dresult;
    const char *device_full = "{\"deviceSerial\":\"old\",\"mic\":1,\"dataCollection\":1}";
    assert(sensor_device_store_apply_runtime(&dstore, device_full, strlen(device_full), &dresult) == SAVVY_OK);
    const char *bad_device_syntax = "{\"blueTooth\":";
    assert(sensor_device_store_apply_runtime(&dstore, bad_device_syntax, strlen(bad_device_syntax), &dresult) != SAVVY_OK);

    const char *bad_device_type = "{\"blueTooth\":\"on\"}";
    assert(sensor_device_store_apply_runtime(&dstore, bad_device_type, strlen(bad_device_type), &dresult) != SAVVY_OK);

    const char *device_dup_key = "{\"mic\":1,\"mic\":0}";
    assert(sensor_device_store_apply_runtime(&dstore, device_dup_key, strlen(device_dup_key), &dresult) != SAVVY_OK);

    h = sensor_device_store_acquire(&dstore, &version);
    const savvy_device_t *device_before = sensor_device_snapshot_payload(h);
    assert(strcmp(device_before->device_serial, "old") == 0);
    raw = sensor_device_snapshot_raw_json(h, &raw_len);
    assert(raw_len == strlen(device_full));
    assert(memcmp(raw, device_full, raw_len) == 0);
    sensor_device_store_release(&dstore, h);

    const char *device_partial = "{\"dataCollection\":0,\"unknown\":true}";
    assert(sensor_device_store_apply_runtime(&dstore, device_partial, strlen(device_partial), &dresult) == SAVVY_OK);
    h = sensor_device_store_acquire(&dstore, &version);
    const savvy_device_t *device = sensor_device_snapshot_payload(h);
    savvy_device_t device_defaults;
    savvy_device_set_defaults(&device_defaults);
    assert(strcmp(device->device_serial, device_defaults.device_serial) == 0);
    assert(device->mic == device_defaults.mic);
    raw = sensor_device_snapshot_raw_json(h, &raw_len);
    assert(raw_len == strlen(device_partial));
    assert(memcmp(raw, device_partial, raw_len) == 0);
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
