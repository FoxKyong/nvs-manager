// SPDX-License-Identifier: MIT
//
// Desktop stand-in for ESP-IDF's spi_flash_mmap.h; values match ESP-IDF v5.5
// (components/spi_flash/include/spi_flash_mmap.h).
#pragma once

#include "esp_err.h"

#define ESP_ERR_FLASH_OP_FAIL (ESP_ERR_FLASH_BASE + 1)
#define ESP_ERR_FLASH_OP_TIMEOUT (ESP_ERR_FLASH_BASE + 2)

#define SPI_FLASH_SEC_SIZE 4096
