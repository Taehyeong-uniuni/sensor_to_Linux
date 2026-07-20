#ifndef SAVVY_CORE_CLOCK_H
#define SAVVY_CORE_CLOCK_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Monotonic clock/timer abstraction (FND-04). Never wraps CLOCK_REALTIME:
 * callers use this for timeouts/scheduling, never for wall-clock display. */
typedef struct savvy_time {
    uint64_t monotonic_ns;
} savvy_time_t;

savvy_time_t savvy_clock_now(void);
uint64_t savvy_clock_diff_ms(savvy_time_t later, savvy_time_t earlier);

typedef struct savvy_deadline {
    savvy_time_t started_at;
    uint32_t timeout_ms;
} savvy_deadline_t;

void savvy_deadline_arm(savvy_deadline_t *d, uint32_t timeout_ms);
bool savvy_deadline_expired(const savvy_deadline_t *d);
uint32_t savvy_deadline_remaining_ms(const savvy_deadline_t *d);

#ifdef __cplusplus
}
#endif

#endif
