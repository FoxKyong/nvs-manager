#pragma once

#include <string>
#include <vector>

#include "app/NvsModel.h"

// Files written to removable storage. Neither function changes NVS.
namespace app {

struct ExportResult {
    bool ok = false;
    std::string error;              // why not, when !ok
    std::vector<std::string> files; // written, in order
    size_t bytes = 0;               // size of the main file
    std::string sha256;             // of the main file, hex
};

// /NVSManager/exports/nvs-NNNN.json with partition, stats, namespaces and key
// metadata; with `includeValues`, nvs-NNNN-values.json that also holds every
// readable value (64-bit integers as strings, blobs as hex).
ExportResult exportJson(const NvsModel &model, bool includeValues);

// /NVSManager/backups/nvs-NNNN.bin, the partition byte for byte, and
// nvs-NNNN.json, a manifest with its SHA-256.
ExportResult rawBackup(const NvsModel &model);

} // namespace app
