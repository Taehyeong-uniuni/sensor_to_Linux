/* clock_gettime()/CLOCK_MONOTONIC are POSIX.1-2008; glibc hides them under
 * -std=c11 without this (Apple's libc exposes them regardless, which is
 * why this only surfaces when building on Linux). Must be defined before
 * any system header is included. */
#define _POSIX_C_SOURCE 200809L

#include "savvy/core/clock.h"
#include <time.h>

savvy_time_t savvy_clock_now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    savvy_time_t t;
    t.monotonic_ns = (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
    return t;
}

uint64_t savvy_clock_diff_ms(savvy_time_t later, savvy_time_t earlier)
{
    if (later.monotonic_ns <= earlier.monotonic_ns) {
        return 0;
    }
    return (later.monotonic_ns - earlier.monotonic_ns) / 1000000ULL;
}

void savvy_deadline_arm(savvy_deadline_t *d, uint32_t timeout_ms)
{
    d->started_at = savvy_clock_now();
    d->timeout_ms = timeout_ms;
}

bool savvy_deadline_expired(const savvy_deadline_t *d)
{
    return savvy_clock_diff_ms(savvy_clock_now(), d->started_at) >= d->timeout_ms;
}

uint32_t savvy_deadline_remaining_ms(const savvy_deadline_t *d)
{
    uint64_t elapsed = savvy_clock_diff_ms(savvy_clock_now(), d->started_at);
    if (elapsed >= d->timeout_ms) {
        return 0;
    }
    return d->timeout_ms - (uint32_t)elapsed;
}
