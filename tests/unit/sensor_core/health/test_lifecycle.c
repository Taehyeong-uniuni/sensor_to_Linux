/* SNS-CORE-007 (health half: lifecycle hook module registration + shutdown
 * fan-out) plus a supplementary idempotent-start/stop check. mgr_ipc's own
 * test suite separately covers the blocking-recv-wakes-on-shutdown half of
 * SNS-CORE-007 using its own cancel source - this test only exercises the
 * health/lifecycle registrar in isolation, with fake module hooks standing
 * in for real (out-of-scope) worker modules. */
#include "sensor_health.h"
#include "sensor_lifecycle.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct fake_module {
    int start_calls;
    int config_applied_calls;
    int shutdown_calls;
    int order_slot; /* set from a shared counter to verify call order */
} fake_module_t;

static int g_call_order = 0;

static void on_start(void *ud) {
    fake_module_t *m = (fake_module_t *)ud;
    m->start_calls++;
    m->order_slot = g_call_order++;
}
static void on_config_applied(void *ud) {
    fake_module_t *m = (fake_module_t *)ud;
    m->config_applied_calls++;
}
static void on_shutdown(void *ud) {
    fake_module_t *m = (fake_module_t *)ud;
    m->shutdown_calls++;
}

static void test_007_shutdown_fanout_and_registration_order(void) {
    sensor_lifecycle_t lc;
    assert(sensor_lifecycle_init(&lc) == SAVVY_OK);

    fake_module_t mod_a = {0, 0, 0, -1};
    fake_module_t mod_b = {0, 0, 0, -1};
    fake_module_t mod_c = {0, 0, 0, -1};

    sensor_lifecycle_hooks_t hooks_a = {"module-a", on_start, on_config_applied, on_shutdown, &mod_a};
    sensor_lifecycle_hooks_t hooks_b = {"module-b", on_start, on_config_applied, on_shutdown, &mod_b};
    sensor_lifecycle_hooks_t hooks_c = {"module-c", NULL, NULL, on_shutdown, &mod_c};

    assert(sensor_lifecycle_register_module(&lc, &hooks_a) == SAVVY_OK);
    assert(sensor_lifecycle_register_module(&lc, &hooks_b) == SAVVY_OK);
    assert(sensor_lifecycle_register_module(&lc, &hooks_c) == SAVVY_OK);

    g_call_order = 0;
    assert(sensor_lifecycle_start(&lc) == SAVVY_OK);
    assert(sensor_lifecycle_get_state(&lc) == SAVVY_LIFECYCLE_RUNNING);

    /* a/b get on_start exactly once, in registration order; c has no
     * on_start hook and must not be touched. */
    assert(mod_a.start_calls == 1);
    assert(mod_b.start_calls == 1);
    assert(mod_c.start_calls == 0);
    assert(mod_a.order_slot == 0);
    assert(mod_b.order_slot == 1);

    sensor_lifecycle_notify_config_applied(&lc);
    assert(mod_a.config_applied_calls == 1);
    assert(mod_b.config_applied_calls == 1);
    assert(mod_c.config_applied_calls == 0);

    /* Health status reflects RUNNING + whatever guard state is injected -
     * this module has no update_guard linkage of its own (Stage A). */
    sensor_health_status_t status = sensor_health_snapshot(&lc, false);
    assert(status.lifecycle_state == SAVVY_LIFECYCLE_RUNNING);
    assert(status.update_guard_tripped == false);
    status = sensor_health_snapshot(&lc, true);
    assert(status.update_guard_tripped == true);

    /* Shutdown: every registered module (including the on_start-less
     * module-c) gets on_shutdown exactly once. */
    assert(sensor_lifecycle_stop(&lc) == SAVVY_OK);
    assert(sensor_lifecycle_get_state(&lc) == SAVVY_LIFECYCLE_STOPPED);
    assert(mod_a.shutdown_calls == 1);
    assert(mod_b.shutdown_calls == 1);
    assert(mod_c.shutdown_calls == 1);

    /* Idempotent: a second stop() must not re-fire any hook. */
    assert(sensor_lifecycle_stop(&lc) == SAVVY_OK);
    assert(mod_a.shutdown_calls == 1);
    assert(mod_b.shutdown_calls == 1);
    assert(mod_c.shutdown_calls == 1);

    sensor_lifecycle_destroy(&lc);
    printf("SNS-CORE-007(health): OK\n");
}

static void test_idempotent_start_stop(void) {
    sensor_lifecycle_t lc;
    assert(sensor_lifecycle_init(&lc) == SAVVY_OK);

    fake_module_t mod = {0, 0, 0, -1};
    sensor_lifecycle_hooks_t hooks = {"solo", on_start, NULL, on_shutdown, &mod};
    assert(sensor_lifecycle_register_module(&lc, &hooks) == SAVVY_OK);

    /* Calling start() repeatedly must fire on_start exactly once. */
    assert(sensor_lifecycle_start(&lc) == SAVVY_OK);
    assert(sensor_lifecycle_start(&lc) == SAVVY_OK);
    assert(sensor_lifecycle_start(&lc) == SAVVY_OK);
    assert(mod.start_calls == 1);

    assert(sensor_lifecycle_stop(&lc) == SAVVY_OK);
    assert(sensor_lifecycle_stop(&lc) == SAVVY_OK);
    assert(mod.shutdown_calls == 1);

    /* Restart after stop is allowed and fires on_start again (this is a
     * fresh STOPPED->RUNNING transition, not a redundant call). */
    assert(sensor_lifecycle_start(&lc) == SAVVY_OK);
    assert(mod.start_calls == 2);

    sensor_lifecycle_stop(&lc);
    sensor_lifecycle_destroy(&lc);
    printf("SENSOR-CORE-LIFECYCLE-IDEMPOTENT: OK\n");
}

static void test_overflow_rejected(void) {
    sensor_lifecycle_t lc;
    assert(sensor_lifecycle_init(&lc) == SAVVY_OK);

    fake_module_t mod = {0, 0, 0, -1};
    char ids[SENSOR_LIFECYCLE_MAX_MODULES + 1][16];
    for (int i = 0; i < SENSOR_LIFECYCLE_MAX_MODULES; i++) {
        snprintf(ids[i], sizeof(ids[i]), "m%d", i);
        sensor_lifecycle_hooks_t hooks = {ids[i], NULL, NULL, NULL, &mod};
        assert(sensor_lifecycle_register_module(&lc, &hooks) == SAVVY_OK);
    }
    sensor_lifecycle_hooks_t one_too_many = {"overflow", NULL, NULL, NULL, &mod};
    assert(sensor_lifecycle_register_module(&lc, &one_too_many) == SAVVY_ERR_OVERFLOW);

    sensor_lifecycle_destroy(&lc);
    printf("SENSOR-CORE-LIFECYCLE-OVERFLOW: OK\n");
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s <subtest-id>\n", argv[0]);
        return 2;
    }

    if (strcmp(argv[1], "007") == 0) {
        test_007_shutdown_fanout_and_registration_order();
    } else if (strcmp(argv[1], "idempotent") == 0) {
        test_idempotent_start_stop();
        test_overflow_rejected();
    } else {
        fprintf(stderr, "unknown subtest id: %s\n", argv[1]);
        return 2;
    }

    return 0;
}
