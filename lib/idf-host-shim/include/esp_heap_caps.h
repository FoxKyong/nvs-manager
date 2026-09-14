// SPDX-License-Identifier: MIT
//
// Desktop stand-in for ESP-IDF's esp_heap_caps.h. nvs_flash only calls
// heap_caps_* with CONFIG_NVS_ALLOCATE_CACHE_IN_SPIRAM, which stays off here.
#pragma once
