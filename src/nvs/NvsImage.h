#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace nvsm {

struct ImageCheck {
    bool ok = false;
    std::string problem;     // why not, when !ok
    size_t pages = 0;
    size_t usedPages = 0;    // initialised pages: active, full or being freed
    size_t corruptPages = 0; // marked corrupt; NVS keeps them for diagnostics only
};

// Checks that a raw dump is shaped like an NVS partition of `partitionSize`
// bytes: whole 4 KiB pages, a known state in every page header, and a header
// checksum that matches on every initialised page, computed the way
// nvs::Page::Header does. The entries inside the pages are not checked.
ImageCheck checkImage(const std::vector<uint8_t> &image, size_t partitionSize);

} // namespace nvsm
