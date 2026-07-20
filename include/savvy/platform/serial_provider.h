#ifndef SAVVY_PLATFORM_SERIAL_PROVIDER_H
#define SAVVY_PLATFORM_SERIAL_PROVIDER_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SAVVY_SERIAL_LEN 14

/* Injected adapter for sourcing the wire-format 14-byte Serial field
 * (FND-04 "platform adapters injected through interfaces"). FND-01 defines
 * only this shape; concrete providers (file-backed, fallback, etc.) and
 * the B-010 normalization policy belong to the consuming Wave 1 session
 * (see CC-MGR-CORE.md MGC-02: "file provider 우선, fallback provider"),
 * not to Foundation. */
typedef struct savvy_serial_provider {
    /* On success, writes exactly SAVVY_SERIAL_LEN bytes to out and sets
     * *out_valid = true. Sets *out_valid = false if no valid 14-byte
     * serial is currently available - the packet codec must not be called
     * with invalid/absent serial data. */
    void (*get_serial)(struct savvy_serial_provider *self, uint8_t out[SAVVY_SERIAL_LEN], bool *out_valid);
    void *impl;
} savvy_serial_provider_t;

#ifdef __cplusplus
}
#endif

#endif
