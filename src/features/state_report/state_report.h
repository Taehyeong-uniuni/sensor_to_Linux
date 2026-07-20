#ifndef SENSOR_CORE_STATE_REPORT_H
#define SENSOR_CORE_STATE_REPORT_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* SNS-CORE-003b: same-state suppression for GETSTATE-style sensor state
 * reports.
 *
 * Android evidence (pinned savvy_sensor@48e2d1442cd867cc60f8ff3186d53fce
 * 1c08f308, MainActivity.java lines 1156-1188): callBroadcastGetstateSensor()
 * keeps one IFCOMM_DEVICE_VALUE slot per SENSOR_TYPE ordinal (mSensorState[]),
 * each starting at a DEVICE_NONE sentinel no real report would ever equal. It
 * reads the slot and, only if the new value differs, updates the slot and
 * sends - the slot update happens unconditionally before the send is even
 * attempted, not after a confirmed delivery. */

typedef enum sensor_report_sensor_type {
    SENSOR_REPORT_TYPE_MIC = 0,
    SENSOR_REPORT_TYPE_LED,
    SENSOR_REPORT_TYPE_BUZZER,
    SENSOR_REPORT_TYPE_TOF,
    SENSOR_REPORT_TYPE_PIR,
    SENSOR_REPORT_TYPE_COUNT
} sensor_report_sensor_type_t;

typedef struct sensor_state_report_tracker {
    bool has_value[SENSOR_REPORT_TYPE_COUNT];
    int32_t last_value[SENSOR_REPORT_TYPE_COUNT];
} sensor_state_report_tracker_t;

void sensor_state_report_tracker_init(sensor_state_report_tracker_t *tracker);

/* Mirrors Android callBroadcastGetstateSensor() EXACTLY, including its
 * naive/optimistic behavior: returns true (caller should attempt to send)
 * iff this is the very first report for `type` OR new_value differs from
 * the last recorded value for `type`; whenever it returns true, it ALSO
 * immediately updates the recorded value to new_value - unconditionally,
 * regardless of whether the caller's subsequent send actually succeeds or
 * gets dropped by mgr_ipc's pre-connect-drop path. Android's cache write is
 * not conditioned on send success either, so do not add a "was it actually
 * sent" rollback/retry here - that would be a behavior change beyond what's
 * evidenced. */
bool sensor_state_report_should_send(sensor_state_report_tracker_t *tracker,
                                      sensor_report_sensor_type_t type,
                                      int32_t new_value);

/* --- Typed payload shapes only (no logic, no dedup - Foundation's
 * ipc_action_catalog.h documents these action/payload names; these structs
 * just give CC-SENSOR-CORE's own code a typed local representation of the
 * same shape). Field presence flags exist because the TOF property fields
 * are documented as optional AND nullable in Foundation's action catalog. */

#define SENSOR_REPORT_STR_LEN 64

typedef struct sensor_property_report {
    bool has_tof_temperature;
    int32_t tof_temperature;
    bool has_tof_temper_drv;
    int32_t tof_temper_drv;
    bool has_smoke_value;
    int32_t smoke_value;
    bool has_mic_value;
    int32_t mic_value;
} sensor_property_report_t;

typedef struct sensor_alert_report {
    int32_t ifcomm_start; /* Foundation's IFCOMM_START key, sent as a stringified byte */
} sensor_alert_report_t;

typedef struct sensor_upload_report {
    char target_file_path[SENSOR_REPORT_STR_LEN];
    char target_file_nm[SENSOR_REPORT_STR_LEN];
} sensor_upload_report_t;

typedef struct sensor_threshold_result {
    bool result; /* Foundation's rslt key, sent as the literal string "True"/"False" */
} sensor_threshold_result_t;

#ifdef __cplusplus
}
#endif

#endif
