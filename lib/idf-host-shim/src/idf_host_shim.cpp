// SPDX-License-Identifier: MIT

#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "esp_err.h"
#include "esp_partition.h"
#include "esp_rom_crc.h"
#include "idf_host_shim.h"
#include "nvs.h"
#include "spi_flash_mmap.h"

namespace {

struct HostPartition {
    esp_partition_t info{};
    std::vector<uint8_t> data;
};

std::vector<std::unique_ptr<HostPartition>> g_partitions;

HostPartition *lookup(const esp_partition_t *partition) {
    for (auto &p : g_partitions) {
        if (&p->info == partition) return p.get();
    }
    return nullptr;
}

bool matches(const esp_partition_t &p, esp_partition_type_t type, esp_partition_subtype_t subtype,
             const std::string *label) {
    if (type != ESP_PARTITION_TYPE_ANY && p.type != type) return false;
    if (subtype != ESP_PARTITION_SUBTYPE_ANY && p.subtype != subtype) return false;
    return label == nullptr || *label == p.label;
}

esp_err_t checkRange(const HostPartition *p, size_t offset, size_t size) {
    if (p == nullptr) return ESP_ERR_INVALID_ARG;
    if (offset > p->data.size() || size > p->data.size() - offset) return ESP_ERR_INVALID_SIZE;
    return ESP_OK;
}

} // namespace

struct esp_partition_iterator_opaque_ {
    esp_partition_type_t type;
    esp_partition_subtype_t subtype;
    bool anyLabel;
    std::string label;
    size_t position; // index of the current match in g_partitions
};

namespace {

// Moves `it` to the first match at or after `from`; false when none is left.
bool seek(esp_partition_iterator_opaque_ *it, size_t from) {
    for (size_t i = from; i < g_partitions.size(); ++i) {
        if (matches(g_partitions[i]->info, it->type, it->subtype, it->anyLabel ? nullptr : &it->label)) {
            it->position = i;
            return true;
        }
    }
    return false;
}

} // namespace

extern "C" {

esp_err_t idf_host_register_nvs_image(const char *label, const uint8_t *data, size_t size) {
    if (label == nullptr || data == nullptr || size == 0 || size % SPI_FLASH_SEC_SIZE != 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (std::strlen(label) >= sizeof(esp_partition_t::label)) return ESP_ERR_INVALID_ARG;

    auto p = std::make_unique<HostPartition>();
    p->info.type = ESP_PARTITION_TYPE_DATA;
    p->info.subtype = ESP_PARTITION_SUBTYPE_DATA_NVS;
    p->info.address = 0x9000; // where the Cardputer family keeps NVS; only informative here
    p->info.size = static_cast<uint32_t>(size);
    p->info.erase_size = SPI_FLASH_SEC_SIZE;
    std::strncpy(p->info.label, label, sizeof p->info.label - 1);
    p->data.assign(data, data + size);
    g_partitions.push_back(std::move(p));
    return ESP_OK;
}

esp_err_t idf_host_get_image(const char *label, const uint8_t **data, size_t *size) {
    if (label == nullptr || data == nullptr || size == nullptr) return ESP_ERR_INVALID_ARG;
    for (const auto &p : g_partitions) {
        if (std::strcmp(p->info.label, label) == 0) {
            *data = p->data.data();
            *size = p->data.size();
            return ESP_OK;
        }
    }
    return ESP_ERR_NOT_FOUND;
}

esp_partition_iterator_t esp_partition_find(esp_partition_type_t type, esp_partition_subtype_t subtype,
                                            const char *label) {
    auto *it = new esp_partition_iterator_opaque_{type, subtype, label == nullptr, label ? label : "", 0};
    if (!seek(it, 0)) {
        delete it;
        return nullptr;
    }
    return it;
}

const esp_partition_t *esp_partition_find_first(esp_partition_type_t type, esp_partition_subtype_t subtype,
                                                const char *label) {
    esp_partition_iterator_t it = esp_partition_find(type, subtype, label);
    if (it == nullptr) return nullptr;
    const esp_partition_t *p = esp_partition_get(it);
    esp_partition_iterator_release(it);
    return p;
}

const esp_partition_t *esp_partition_get(esp_partition_iterator_t iterator) {
    return iterator == nullptr ? nullptr : &g_partitions[iterator->position]->info;
}

esp_partition_iterator_t esp_partition_next(esp_partition_iterator_t iterator) {
    if (iterator == nullptr) return nullptr;
    if (!seek(iterator, iterator->position + 1)) {
        delete iterator;
        return nullptr;
    }
    return iterator;
}

void esp_partition_iterator_release(esp_partition_iterator_t iterator) { delete iterator; }

esp_err_t esp_partition_read(const esp_partition_t *partition, size_t src_offset, void *dst, size_t size) {
    const HostPartition *p = lookup(partition);
    esp_err_t err = checkRange(p, src_offset, size);
    if (err != ESP_OK) return err;
    if (dst == nullptr) return ESP_ERR_INVALID_ARG;
    std::memcpy(dst, p->data.data() + src_offset, size);
    return ESP_OK;
}

esp_err_t esp_partition_read_raw(const esp_partition_t *partition, size_t src_offset, void *dst, size_t size) {
    return esp_partition_read(partition, src_offset, dst, size);
}

esp_err_t esp_partition_write(const esp_partition_t *partition, size_t dst_offset, const void *src, size_t size) {
    HostPartition *p = lookup(partition);
    esp_err_t err = checkRange(p, dst_offset, size);
    if (err != ESP_OK) return err;
    if (src == nullptr) return ESP_ERR_INVALID_ARG;
    if (p->info.readonly) return ESP_ERR_NOT_ALLOWED;
    // NOR flash can only clear bits; a write without a prior erase ANDs.
    const auto *bytes = static_cast<const uint8_t *>(src);
    for (size_t i = 0; i < size; ++i) p->data[dst_offset + i] &= bytes[i];
    return ESP_OK;
}

esp_err_t esp_partition_write_raw(const esp_partition_t *partition, size_t dst_offset, const void *src, size_t size) {
    return esp_partition_write(partition, dst_offset, src, size);
}

esp_err_t esp_partition_erase_range(const esp_partition_t *partition, size_t offset, size_t size) {
    HostPartition *p = lookup(partition);
    esp_err_t err = checkRange(p, offset, size);
    if (err != ESP_OK) return err;
    if (offset % p->info.erase_size != 0 || size % p->info.erase_size != 0) return ESP_ERR_INVALID_SIZE;
    if (p->info.readonly) return ESP_ERR_NOT_ALLOWED;
    std::memset(p->data.data() + offset, 0xFF, size);
    return ESP_OK;
}

uint32_t esp_partition_get_main_flash_sector_size(void) { return SPI_FLASH_SEC_SIZE; }

uint32_t esp_rom_crc32_le(uint32_t crc, uint8_t const *buf, uint32_t len) {
    crc = ~crc;
    for (uint32_t i = 0; i < len; ++i) {
        crc ^= buf[i];
        for (int bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

const char *esp_err_to_name(esp_err_t code) {
#define NAME(c)                                                                                                        \
    case c: return #c;
    switch (code) {
        NAME(ESP_OK)
        NAME(ESP_FAIL)
        NAME(ESP_ERR_NO_MEM)
        NAME(ESP_ERR_INVALID_ARG)
        NAME(ESP_ERR_INVALID_STATE)
        NAME(ESP_ERR_INVALID_SIZE)
        NAME(ESP_ERR_NOT_FOUND)
        NAME(ESP_ERR_NOT_SUPPORTED)
        NAME(ESP_ERR_TIMEOUT)
        NAME(ESP_ERR_INVALID_RESPONSE)
        NAME(ESP_ERR_INVALID_CRC)
        NAME(ESP_ERR_INVALID_VERSION)
        NAME(ESP_ERR_INVALID_MAC)
        NAME(ESP_ERR_NOT_FINISHED)
        NAME(ESP_ERR_NOT_ALLOWED)
        NAME(ESP_ERR_FLASH_OP_FAIL)
        NAME(ESP_ERR_FLASH_OP_TIMEOUT)
        NAME(ESP_ERR_NVS_NOT_INITIALIZED)
        NAME(ESP_ERR_NVS_NOT_FOUND)
        NAME(ESP_ERR_NVS_TYPE_MISMATCH)
        NAME(ESP_ERR_NVS_READ_ONLY)
        NAME(ESP_ERR_NVS_NOT_ENOUGH_SPACE)
        NAME(ESP_ERR_NVS_INVALID_NAME)
        NAME(ESP_ERR_NVS_INVALID_HANDLE)
        NAME(ESP_ERR_NVS_REMOVE_FAILED)
        NAME(ESP_ERR_NVS_KEY_TOO_LONG)
        NAME(ESP_ERR_NVS_PAGE_FULL)
        NAME(ESP_ERR_NVS_INVALID_STATE)
        NAME(ESP_ERR_NVS_INVALID_LENGTH)
        NAME(ESP_ERR_NVS_NO_FREE_PAGES)
        NAME(ESP_ERR_NVS_VALUE_TOO_LONG)
        NAME(ESP_ERR_NVS_PART_NOT_FOUND)
        NAME(ESP_ERR_NVS_NEW_VERSION_FOUND)
        NAME(ESP_ERR_NVS_XTS_ENCR_FAILED)
        NAME(ESP_ERR_NVS_XTS_DECR_FAILED)
        NAME(ESP_ERR_NVS_XTS_CFG_FAILED)
        NAME(ESP_ERR_NVS_XTS_CFG_NOT_FOUND)
        NAME(ESP_ERR_NVS_ENCR_NOT_SUPPORTED)
        NAME(ESP_ERR_NVS_KEYS_NOT_INITIALIZED)
        NAME(ESP_ERR_NVS_CORRUPT_KEY_PART)
        NAME(ESP_ERR_NVS_WRONG_ENCRYPTION)
        NAME(ESP_ERR_NVS_CONTENT_DIFFERS)
    default: return "UNKNOWN ERROR";
    }
#undef NAME
}

} // extern "C"
