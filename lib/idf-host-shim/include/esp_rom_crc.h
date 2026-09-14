// SPDX-License-Identifier: MIT
//
// Desktop stand-in for ESP-IDF's esp_rom_crc.h.
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Same contract as the ROM function: CRC-32 (polynomial 0xEDB88320) with the
// running value inverted on entry and exit, i.e. zlib's crc32(crc, buf, len).
uint32_t esp_rom_crc32_le(uint32_t crc, uint8_t const *buf, uint32_t len);

#ifdef __cplusplus
}
#endif
