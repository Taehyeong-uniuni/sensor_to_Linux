#include "update_guard.h"

void sensor_update_guard_init(sensor_update_guard_t *guard) {
    pthread_mutex_init(&guard->lock, NULL);
    guard->tripped = false;
}

void sensor_update_guard_destroy(sensor_update_guard_t *guard) {
    pthread_mutex_destroy(&guard->lock);
}

void sensor_update_guard_on_apk_update(sensor_update_guard_t *guard) {
    pthread_mutex_lock(&guard->lock);
    guard->tripped = true;
    pthread_mutex_unlock(&guard->lock);
}

bool sensor_update_guard_should_allow_pir_in(sensor_update_guard_t *guard) {
    return !sensor_update_guard_is_tripped(guard);
}

bool sensor_update_guard_is_tripped(sensor_update_guard_t *guard) {
    pthread_mutex_lock(&guard->lock);
    bool tripped = guard->tripped;
    pthread_mutex_unlock(&guard->lock);
    return tripped;
}
