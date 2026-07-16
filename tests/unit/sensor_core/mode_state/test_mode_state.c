/* SNS-CORE-002a / SNS-CORE-003: useRknn/dataCollection raw->runtime mode
 * derivation, ported 1:1 from
 * savvy_sensor@48e2d1442cd867cc60f8ff3186d53fce1c08f308 MainActivity.java
 * (onCreate/actionConfig/actionDevice). useRknn's live rule is diff-gated;
 * dataCollection's live rule is unconditional - see mode_state.h. */
#include "mode_state.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* assert() is compiled out under -DNDEBUG (Release builds), which would
 * silently drop the verification of every value below. CHECK() is
 * deliberately independent of NDEBUG so Release and Debug run the
 * identical test body. */
#define CHECK(cond, msg) do { \
    if (!(cond)) { \
        fprintf(stderr, "CHECK failed: %s (%s:%d)\n", (msg), __FILE__, __LINE__); \
        abort(); \
    } \
} while (0)

static void test_002a_use_rknn(void) {
    CHECK(sensor_mode_use_rknn_apply_startup(0) == false, "startup(0) is false");
    CHECK(sensor_mode_use_rknn_apply_startup(1) == true, "startup(1) is true");
    CHECK(sensor_mode_use_rknn_apply_startup(2) == true, "startup(2) is true");
    CHECK(sensor_mode_use_rknn_apply_startup(-1) == true, "startup(-1) is true");
    CHECK(sensor_mode_use_rknn_apply_startup(INT32_MAX) == true, "startup(INT32_MAX) is true");
    CHECK(sensor_mode_use_rknn_apply_startup(INT32_MIN) == true, "startup(INT32_MIN) is true");

    sensor_mode_transition_t t;

    t = sensor_mode_use_rknn_apply_live(0, 1);
    CHECK(t.changed == true, "live(0,1) reports changed");
    CHECK(t.runtime_value == true, "live(0,1) runtime_value is true");

    t = sensor_mode_use_rknn_apply_live(1, 1);
    CHECK(t.changed == false, "live(1,1) reports unchanged");

    t = sensor_mode_use_rknn_apply_live(0, 2);
    CHECK(t.changed == true, "live(0,2) reports changed");
    CHECK(t.runtime_value == false, "live(0,2) runtime_value is false");

    t = sensor_mode_use_rknn_apply_live(0, 0);
    CHECK(t.changed == false, "live(0,0) reports unchanged");

    t = sensor_mode_use_rknn_apply_live(0, INT32_MAX);
    CHECK(t.changed == true, "live(0,INT32_MAX) reports changed");
    CHECK(t.runtime_value == false, "live(0,INT32_MAX) runtime_value is false");

    t = sensor_mode_use_rknn_apply_live(INT32_MIN, INT32_MIN);
    CHECK(t.changed == false, "live(INT32_MIN,INT32_MIN) reports unchanged");

    printf("SNS-CORE-002a: OK\n");
}

static void test_003_data_collection(void) {
    CHECK(sensor_mode_data_collection_apply_startup(1) == true, "startup(1) is true");
    CHECK(sensor_mode_data_collection_apply_startup(0) == false, "startup(0) is false");
    CHECK(sensor_mode_data_collection_apply_startup(2) == false, "startup(2) is false");
    CHECK(sensor_mode_data_collection_apply_startup(-1) == false, "startup(-1) is false");
    CHECK(sensor_mode_data_collection_apply_startup(INT32_MAX) == false, "startup(INT32_MAX) is false");
    CHECK(sensor_mode_data_collection_apply_startup(INT32_MIN) == false, "startup(INT32_MIN) is false");

    CHECK(sensor_mode_data_collection_apply_live(0) == false, "live(0) is false");
    CHECK(sensor_mode_data_collection_apply_live(1) == true, "live(1) is true");
    CHECK(sensor_mode_data_collection_apply_live(2) == true, "live(2) is true");
    CHECK(sensor_mode_data_collection_apply_live(-1) == true, "live(-1) is true");
    CHECK(sensor_mode_data_collection_apply_live(INT32_MAX) == true, "live(INT32_MAX) is true");
    CHECK(sensor_mode_data_collection_apply_live(INT32_MIN) == true, "live(INT32_MIN) is true");

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
