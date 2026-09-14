#pragma once

#include <string>
#include <vector>

#include <esp_err.h>

#include "nvs/NvsTypes.h"

namespace nvsm {

// Read-only view of one NVS partition through the official ESP-IDF API.
// Every handle is opened NVS_READONLY; nothing in this class writes.
class NvsReader {
public:
    static std::vector<PartitionInfo> findPartitions();

    explicit NvsReader(std::string label) : label_(std::move(label)) {}

    const std::string &label() const { return label_; }

    // Initialises the partition if needed and returns the real result. Never
    // erases, whatever the error.
    esp_err_t init() const;

    esp_err_t stats(Stats &out) const;

    // Namespaces that currently hold at least one key, largest first. Empty
    // namespaces still count in Stats::namespaceCount but cannot be listed
    // through the public API.
    esp_err_t namespaces(std::vector<NamespaceInfo> &out) const;

    // Keys of one namespace, sorted by name.
    esp_err_t keys(const std::string &ns, std::vector<KeyInfo> &out) const;

    // Reads one value of the given type. Strings and blobs are read whole.
    esp_err_t readValue(const std::string &ns, const std::string &key, ValueType type, Value &out) const;

    // Copies the whole partition as it is on flash, for a raw backup.
    esp_err_t readPartition(PartitionInfo &info, std::vector<uint8_t> &out) const;

private:
    std::string label_;
};

} // namespace nvsm
