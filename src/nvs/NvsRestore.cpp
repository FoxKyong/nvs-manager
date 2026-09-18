#include "nvs/NvsWriter.h"

#include <cstring>

#include <esp_partition.h>
#include <nvs.h>
#include <nvs_flash.h>

namespace nvsm {
namespace {

constexpr size_t kSectorSize = 4096;

WriteResult failed(esp_err_t err, const char *step) {
    WriteResult r;
    r.err = err;
    r.step = step;
    return r;
}

} // namespace

WriteResult NvsWriter::restorePartition(const std::vector<uint8_t> &image) const {
    const esp_partition_t *p =
        esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_NVS, label_.c_str());
    if (p == nullptr) return failed(ESP_ERR_NOT_FOUND, "find partition");
    if (image.size() != p->size || image.size() % kSectorSize != 0) return failed(ESP_ERR_INVALID_SIZE, "check size");

    // nvs_flash keeps page state in RAM; close the partition so nothing writes
    // to it while its sectors are replaced.
    esp_err_t err = nvs_flash_deinit_partition(label_.c_str());
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_INITIALIZED) return failed(err, "close nvs");

    // Sector by sector. The boot guard refuses only an erase of the whole
    // partition, so it stays in force for everything else.
    std::vector<uint8_t> back(kSectorSize);
    for (size_t off = 0; off < image.size(); off += kSectorSize) {
        if ((err = esp_partition_erase_range(p, off, kSectorSize)) != ESP_OK) return failed(err, "erase");
        if ((err = esp_partition_write(p, off, image.data() + off, kSectorSize)) != ESP_OK) return failed(err, "write");
        if ((err = esp_partition_read(p, off, back.data(), kSectorSize)) != ESP_OK) return failed(err, "read back");
        if (std::memcmp(back.data(), image.data() + off, kSectorSize) != 0) return failed(ESP_ERR_INVALID_CRC, "verify");
    }

    // Opening may complete a page move the backup caught in progress, just as
    // after a power loss; that is NVS's own recovery, not a change of data.
    if ((err = nvs_flash_init_partition(label_.c_str())) != ESP_OK) return failed(err, "open nvs");
    return WriteResult{};
}

} // namespace nvsm
