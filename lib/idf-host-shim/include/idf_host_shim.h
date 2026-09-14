// SPDX-License-Identifier: MIT
//
// Entry point of the desktop shim: makes a raw partition dump visible to
// esp_partition_* (and so to nvs_flash) as if it were on flash.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Registers a private RAM copy of `size` bytes as an NVS data partition named
// `label`. `size` must be a non-zero multiple of the 4096-byte sector. Writes
// by nvs_flash change only the copy, never the source.
esp_err_t idf_host_register_nvs_image(const char *label, const uint8_t *data, size_t size);

// Current content of a registered partition, including every change nvs_flash
// made to it. The pointer stays valid until the process ends.
esp_err_t idf_host_get_image(const char *label, const uint8_t **data, size_t *size);

#ifdef __cplusplus
}
#endif
