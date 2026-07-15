/* SNS-CORE-004: APK_UPDATE 후 반복 PIR-in 요청 -> guard true 후 후속
 * PIR-in 차단, reset 없음. */
#include "update_guard.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_004_apk_update_trips_guard_permanently(void) {
    sensor_update_guard_t guard;
    sensor_update_guard_init(&guard);

    assert(sensor_update_guard_should_allow_pir_in(&guard) == true);

    sensor_update_guard_on_apk_update(&guard);
    assert(sensor_update_guard_is_tripped(&guard) == true);
    assert(sensor_update_guard_should_allow_pir_in(&guard) == false);

    for (int i = 0; i < 5; i++) {
        assert(sensor_update_guard_should_allow_pir_in(&guard) == false);
    }

    /* Repeated broadcast: must not crash, must not un-trip (no reset
     * function exists to call). */
    sensor_update_guard_on_apk_update(&guard);
    assert(sensor_update_guard_is_tripped(&guard) == true);
    assert(sensor_update_guard_should_allow_pir_in(&guard) == false);

    sensor_update_guard_destroy(&guard);
    printf("SNS-CORE-004: OK\n");
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s <subtest-id>\n", argv[0]);
        return 2;
    }

    if (strcmp(argv[1], "004") == 0) {
        test_004_apk_update_trips_guard_permanently();
    } else {
        fprintf(stderr, "unknown subtest id: %s\n", argv[1]);
        return 2;
    }

    return 0;
}
