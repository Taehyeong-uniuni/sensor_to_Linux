/* SNS-CORE-002a / SNS-CORE-003: useRknn/dataCollection raw->runtime mode
 * derivation, ported 1:1 from
 * savvy_sensor@48e2d1442cd867cc60f8ff3186d53fce1c08f308 MainActivity.java
 * (onCreate/actionConfig/actionDevice). useRknn's live rule is diff-gated;
 * dataCollection's live rule is unconditional - see mode_state.h. */
#include "mode_state.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static void test_002a_use_rknn(void) {
    assert(sensor_mode_use_rknn_apply_startup(0) == false);
    assert(sensor_mode_use_rknn_apply_startup(1) == true);
    assert(sensor_mode_use_rknn_apply_startup(2) == true);
    assert(sensor_mode_use_rknn_apply_startup(-1) == true);
    assert(sensor_mode_use_rknn_apply_startup(INT32_MAX) == true);
    assert(sensor_mode_use_rknn_apply_startup(INT32_MIN) == true);

    sensor_mode_transition_t t;

    t = sensor_mode_use_rknn_apply_live(0, 1);
    assert(t.changed == true);
    assert(t.runtime_value == true);

    t = sensor_mode_use_rknn_apply_live(1, 1);
    assert(t.changed == false);

    t = sensor_mode_use_rknn_apply_live(0, 2);
    assert(t.changed == true);
    assert(t.runtime_value == false);

    t = sensor_mode_use_rknn_apply_live(0, 0);
    assert(t.changed == false);

    t = sensor_mode_use_rknn_apply_live(0, INT32_MAX);
    assert(t.changed == true);
    assert(t.runtime_value == false);

    t = sensor_mode_use_rknn_apply_live(INT32_MIN, INT32_MIN);
    assert(t.changed == false);

    printf("SNS-CORE-002a: OK\n");
}

static void test_003_data_collection(void) {
    assert(sensor_mode_data_collection_apply_startup(1) == true);
    assert(sensor_mode_data_collection_apply_startup(0) == false);
    assert(sensor_mode_data_collection_apply_startup(2) == false);
    assert(sensor_mode_data_collection_apply_startup(-1) == false);
    assert(sensor_mode_data_collection_apply_startup(INT32_MAX) == false);
    assert(sensor_mode_data_collection_apply_startup(INT32_MIN) == false);

    assert(sensor_mode_data_collection_apply_live(0) == false);
    assert(sensor_mode_data_collection_apply_live(1) == true);
    assert(sensor_mode_data_collection_apply_live(2) == true);
    assert(sensor_mode_data_collection_apply_live(-1) == true);
    assert(sensor_mode_data_collection_apply_live(INT32_MAX) == true);
    assert(sensor_mode_data_collection_apply_live(INT32_MIN) == true);

    printf("SNS-CORE-003: OK\n");
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s <subtest-id>\n", argv[0]);
        return 2;
    }

    if (strcmp(argv[1], "002a") == 0) {
        test_002a_use_rknn();
    } else if (strcmp(argv[1], "003") == 0) {
        test_003_data_collection();
    } else {
        fprintf(stderr, "unknown subtest id: %s\n", argv[1]);
        return 2;
    }

    return 0;
}
