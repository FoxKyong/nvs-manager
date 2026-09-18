#include "nvs/NvsImage.h"

#include <cstdio>

#include <esp_rom_crc.h>

namespace nvsm {
namespace {

constexpr size_t kPageSize = 4096;

// Page states (nvs_page.hpp): each step clears more bits of an erased word.
constexpr uint32_t kUninitialized = 0xFFFFFFFF;
constexpr uint32_t kActive = 0xFFFFFFFE;
constexpr uint32_t kFull = 0xFFFFFFFC;
constexpr uint32_t kFreeing = 0xFFFFFFF8;
constexpr uint32_t kCorrupt = 0xFFFFFFF0;

// Page header: state, sequence number, version, 19 reserved bytes, CRC32 of
// everything from the sequence number up to the CRC itself.
constexpr size_t kSeqOffset = 4;
constexpr size_t kCrcOffset = 28;

uint32_t le32(const uint8_t *p) {
    return p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

} // namespace

ImageCheck checkImage(const std::vector<uint8_t> &image, size_t partitionSize) {
    ImageCheck r;
    char buf[96];
    if (image.size() != partitionSize) {
        snprintf(buf, sizeof buf, "%u bytes, the partition has %u", static_cast<unsigned>(image.size()),
                 static_cast<unsigned>(partitionSize));
        r.problem = buf;
        return r;
    }
    if (image.empty() || image.size() % kPageSize != 0) {
        r.problem = "not a whole number of 4 KiB pages";
        return r;
    }

    r.pages = image.size() / kPageSize;
    for (size_t p = 0; p < r.pages; ++p) {
        const uint8_t *page = image.data() + p * kPageSize;
        const uint32_t state = le32(page);
        if (state == kUninitialized) continue;
        if (state == kCorrupt) {
            ++r.corruptPages;
            continue;
        }
        if (state != kActive && state != kFull && state != kFreeing) {
            snprintf(buf, sizeof buf, "page %u has an unknown state 0x%08X", static_cast<unsigned>(p),
                     static_cast<unsigned>(state));
            r.problem = buf;
            return r;
        }
        const uint32_t crc = esp_rom_crc32_le(0xFFFFFFFF, page + kSeqOffset, kCrcOffset - kSeqOffset);
        if (crc != le32(page + kCrcOffset)) {
            snprintf(buf, sizeof buf, "page %u header checksum does not match", static_cast<unsigned>(p));
            r.problem = buf;
            return r;
        }
        ++r.usedPages;
    }
    r.ok = true;
    return r;
}

} // namespace nvsm
