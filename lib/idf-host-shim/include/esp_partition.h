// SPDX-License-Identifier: MIT
//
// Desktop stand-in for ESP-IDF's esp_partition.h, limited to what nvs_flash
// and NVS Manager use. Types and field names follow ESP-IDF v5.5; partitions
// live in RAM and are registered through idf_host_shim.h.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct esp_flash_t esp_flash_t;

typedef enum {
    ESP_PARTITION_TYPE_APP = 0x00,
    ESP_PARTITION_TYPE_DATA = 0x01,
    ESP_PARTITION_TYPE_ANY = 0xff,
} esp_partition_type_t;

typedef enum {
    ESP_PARTITION_SUBTYPE_DATA_OTA = 0x00,
    ESP_PARTITION_SUBTYPE_DATA_PHY = 0x01,
    ESP_PARTITION_SUBTYPE_DATA_NVS = 0x02,
    ESP_PARTITION_SUBTYPE_DATA_COREDUMP = 0x03,
    ESP_PARTITION_SUBTYPE_DATA_NVS_KEYS = 0x04,
    ESP_PARTITION_SUBTYPE_ANY = 0xff,
} esp_partition_subtype_t;

typedef struct esp_partition_iterator_opaque_ *esp_partition_iterator_t;

typedef struct {
    esp_flash_t *flash_chip;
    esp_partition_type_t type;
    esp_partition_subtype_t subtype;
    uint32_t address;
    uint32_t size;
    uint32_t erase_size;
    char label[17];
    bool encrypted;
    bool readonly;
} esp_partition_t;

esp_partition_iterator_t esp_partition_find(esp_partition_type_t type, esp_partition_subtype_t subtype,
                                            const char *label);
const esp_partition_t *esp_partition_find_first(esp_partition_type_t type, esp_partition_subtype_t subtype,
                                                const char *label);
const esp_partition_t *esp_partition_get(esp_partition_iterator_t iterator);
// Like ESP-IDF: returns NULL and releases the iterator when nothing follows.
esp_partition_iterator_t esp_partition_next(esp_partition_iterator_t iterator);
void esp_partition_iterator_release(esp_partition_iterator_t iterator);

esp_err_t esp_partition_read(const esp_partition_t *partition, size_t src_offset, void *dst, size_t size);
esp_err_t esp_partition_read_raw(const esp_partition_t *partition, size_t src_offset, void *dst, size_t size);
esp_err_t esp_partition_write(const esp_partition_t *partition, size_t dst_offset, const void *src, size_t size);
esp_err_t esp_partition_write_raw(const esp_partition_t *partition, size_t dst_offset, const void *src, size_t size);
esp_err_t esp_partition_erase_range(const esp_partition_t *partition, size_t offset, size_t size);

uint32_t esp_partition_get_main_flash_sector_size(void);

#ifdef __cplusplus
}
#endif
