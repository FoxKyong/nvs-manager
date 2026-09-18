#pragma once

#include <functional>
#include <string>
#include <vector>

#include <esp_err.h>

#include "nvs/NvsReader.h"
#include "nvs/NvsWriter.h"

namespace app {

enum class Health { Ok, Low, Critical, Full };

struct Capacity {
    size_t usable = 0;    // total entries minus the page reserved for garbage collection
    size_t available = 0; // entries new data can still use
    int percent = 0;      // available as a share of usable
    Health health = Health::Full;
};

Capacity capacityOf(const nvsm::Stats &stats);
const char *healthName(Health health);

// What one change did, measured by reading the stats before and after.
struct ChangeOutcome {
    nvsm::WriteResult result;
    nvsm::Stats before;
    nvsm::Stats after;
    bool statsValid = false; // both stats could be read
};

// Snapshot of one NVS partition for the UI. refresh() re-reads everything;
// between refreshes the UI works from this copy.
class NvsModel {
public:
    explicit NvsModel(std::string label) : reader_(label), writer_(std::move(label)) {}

    // Re-reads the partition. Failures are logged; the full report is not.
    void refresh();

    // Writes the full NVS report (metadata only, never values) to the log.
    void logReport() const;

    const std::string &label() const { return reader_.label(); }
    bool hasPartition() const { return hasPartition_; }
    const nvsm::PartitionInfo &partition() const { return partition_; }

    // ESP_OK when stats() is valid; otherwise the first failure and its step.
    esp_err_t error() const { return error_; }
    const char *errorStep() const { return errorStep_; }

    const nvsm::Stats &stats() const { return stats_; }
    const std::vector<nvsm::NamespaceInfo> &namespaces() const { return namespaces_; }
    esp_err_t namespacesError() const { return namespacesError_; }

    // Used entries the API cannot attribute to a listed namespace: namespaces
    // without keys and orphaned entries.
    size_t unattributedEntries() const;

    esp_err_t keys(const std::string &ns, std::vector<nvsm::KeyInfo> &out) const { return reader_.keys(ns, out); }
    esp_err_t readValue(const std::string &ns, const nvsm::KeyInfo &key, nvsm::Value &out) const {
        return reader_.readValue(ns, key.name, key.type, out);
    }
    esp_err_t readPartition(nvsm::PartitionInfo &info, std::vector<uint8_t> &out) const {
        return reader_.readPartition(info, out);
    }

    // Changes. Each is logged without values, followed by a refresh.
    ChangeOutcome eraseKey(const std::string &ns, const std::string &key);
    ChangeOutcome eraseNamespace(const std::string &ns);
    ChangeOutcome setValue(const std::string &ns, const std::string &key, const nvsm::Value &value);
    // Replaces the whole partition with a checked raw image; `source` names it in the log.
    ChangeOutcome restorePartition(const std::vector<uint8_t> &image, const std::string &source);

private:
    ChangeOutcome runChange(const char *tag, const std::string &target,
                            const std::function<nvsm::WriteResult()> &change);

    nvsm::NvsReader reader_;
    nvsm::NvsWriter writer_;
    bool hasPartition_ = false;
    nvsm::PartitionInfo partition_;
    esp_err_t error_ = ESP_ERR_INVALID_STATE;
    const char *errorStep_ = "not read yet";
    nvsm::Stats stats_;
    std::vector<nvsm::NamespaceInfo> namespaces_;
    esp_err_t namespacesError_ = ESP_OK;
};

} // namespace app
