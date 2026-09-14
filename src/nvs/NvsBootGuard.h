#pragma once

#include <cstdint>

#include <esp_err.h>

// Keeps the Arduino core from wiping NVS before setup() runs.
//
// initArduino() erases the whole NVS partition when nvs_flash_init() returns
// ESP_ERR_NVS_NO_FREE_PAGES or ESP_ERR_NVS_NEW_VERSION_FOUND — exactly the
// states this tool has to diagnose. The linker wraps both calls (see the
// --wrap flags in platformio.ini and docs/RESEARCH.md, section 3).
namespace nvsguard {

struct BootReport {
    bool initSeen;          // did anything call nvs_flash_init() before us
    esp_err_t initResult;   // the real result of that first call
    uint32_t blockedErases; // full-partition NVS erases refused so far
};

BootReport bootReport();

} // namespace nvsguard
