#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <esp_err.h>

#include "nvs/NvsTypes.h"

namespace nvsm {

struct WriteResult {
    esp_err_t err = ESP_OK;
    const char *step = ""; // "open", "erase", "set", "commit" or "verify" when err != ESP_OK
    bool ok() const { return err == ESP_OK; }
};

// The only code in the application that changes NVS. Every call is one
// explicit user action: open read-write, change, commit, close, then read
// back to verify. A result is ESP_OK only when all of that succeeded.
class NvsWriter {
public:
    explicit NvsWriter(std::string label) : label_(std::move(label)) {}

    WriteResult eraseKey(const std::string &ns, const std::string &key) const;

    // Erases every key of the namespace. The namespace entry itself stays:
    // the public API offers no way to remove it.
    WriteResult eraseNamespace(const std::string &ns) const;

    // Integers and strings only; the key keeps its type.
    WriteResult setValue(const std::string &ns, const std::string &key, const Value &value) const;

    // Replaces the whole partition with a raw image of the same size: closes
    // NVS, then erases, writes and reads back one 4 KiB sector at a time, and
    // opens NVS again. The image must already be checked (nvs/NvsImage.h).
    WriteResult restorePartition(const std::vector<uint8_t> &image) const;

private:
    std::string label_;
};

} // namespace nvsm
