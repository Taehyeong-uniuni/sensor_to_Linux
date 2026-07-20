/* SNS-CORE-001 (state_report half) and SNS-CORE-003b: same-state
 * re-report suppression for GETSTATE-style sensor state reports.
 * Dispatches on argv[1], mirroring tests/unit/sensor_core/config/test_config_store.c. */
#include "state_report.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

/* SNS-CORE-001 lists both config and state_report as execution targets.
 * config's own SNS-CORE-001 test proves the 9 stateful Device fields are
 * zeroed at startup; this half proves the state_report-side consequence:
 * a freshly-init()ed tracker (this feature's own notion of "just booted,
 * nothing reported yet") has no has_value[] entries set for ANY of the 5
 * sensor types, so the first real report after startup is never
 * suppressed - a fresh boot cannot spuriously inherit a prior process's
 * last-reported value. */
static void test_001_fresh_tracker_after_startup(void) {
    sensor_state_report_tracker_t tracker;
    sensor_state_report_tracker_init(&tracker);

    assert(sensor_state_report_should_send(&tracker, SENSOR_REPORT_TYPE_MIC, 0) == true);
    assert(sensor_state_report_should_send(&tracker, SENSOR_REPORT_TYPE_LED, 0) == true);
    assert(sensor_state_report_should_send(&tracker, SENSOR_REPORT_TYPE_BUZZER, 0) == true);
    assert(sensor_state_report_should_send(&tracker, SENSOR_REPORT_TYPE_TOF, 0) == true);
    assert(sensor_state_report_should_send(&tracker, SENSOR_REPORT_TYPE_PIR, 0) == true);

    /* Immediately repeating the same post-boot value IS suppressed - the
     * "fresh start" exemption applies only to the very first report. */
    assert(sensor_state_report_should_send(&tracker, SENSOR_REPORT_TYPE_PIR, 0) == false);

    printf("SNS-CORE-001(state_report): OK\n");
}

static void test_003b_getstate_dedup(void) {
    sensor_state_report_tracker_t tracker;
    sensor_state_report_tracker_init(&tracker);

    /* First report for PIR ever. */
    assert(sensor_state_report_should_send(&tracker, SENSOR_REPORT_TYPE_PIR, 5) == true);

    /* Same value recurs - simulates the previous send getting dropped
     * because MGR wasn't connected, then the same local state recurring;
     * the dedup check only looks at the value, not delivery. */
    assert(sensor_state_report_should_send(&tracker, SENSOR_REPORT_TYPE_PIR, 5) == false);

    /* Value actually changes. */
    assert(sensor_state_report_should_send(&tracker, SENSOR_REPORT_TYPE_PIR, 7) == true);

    /* Same new value recurs - suppressed at the new value. */
    assert(sensor_state_report_should_send(&tracker, SENSOR_REPORT_TYPE_PIR, 7) == false);

    /* Independent per-type slots: TOF's first report of value 5 must still
     * send even though PIR already reported value 5. */
    assert(sensor_state_report_should_send(&tracker, SENSOR_REPORT_TYPE_TOF, 5) == true);

    /* 0 is a legitimate first value, not confused with "never reported". */
    assert(sensor_state_report_should_send(&tracker, SENSOR_REPORT_TYPE_MIC, 0) == true);

    printf("SNS-CORE-003b: OK\n");
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s <subtest-id>\n", argv[0]);
        return 2;
    }

    if (strcmp(argv[1], "001") == 0) {
        test_001_fresh_tracker_after_startup();
    } else if (strcmp(argv[1], "003b") == 0) {
        test_003b_getstate_dedup();
    } else {
        fprintf(stderr, "unknown subtest id: %s\n", argv[1]);
        return 2;
    }

    return 0;
}
