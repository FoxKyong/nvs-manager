// SPDX-License-Identifier: MIT
//
// Desktop stand-in for ESP-IDF's esp_attr.h: placement attributes mean
// nothing off the chip.
#pragma once

#define IRAM_ATTR
#define DRAM_ATTR

// Empty outside ESP-IDF's own CI builds, as in esp_common/include/esp_attr.h.
#define IDF_DEPRECATED(REASON)
