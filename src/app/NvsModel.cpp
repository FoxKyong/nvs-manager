#include "app/NvsModel.h"

#include "app/Config.h"
#include "platform/Platform.h"

namespace app {
namespace {

void logError(const char *what, esp_err_t err) {
    platform::logf("ERR", "%s: %s (0x%x)", what, esp_err_to_name(err), static_cast<unsigned>(err));
}

} // namespace

Capacity capacityOf(const nvsm::Stats &stats) {
    Capacity c;
    c.usable = stats.totalEntries > config::kReservedEntries ? stats.totalEntries - config::kReservedEntries : 0;
    c.available = stats.availableEntries < c.usable ? stats.availableEntries : c.usable;
    c.percent = c.usable > 0 ? static_cast<int>(c.available * 100 / c.usable) : 0;

    if (c.available == 0) {
        c.health = Health::Full;
    } else if (c.available * 100 < c.usable * config::kCriticalPercent) {
        c.health = Health::Critical;
    } else if (c.available * 100 <= c.usable * config::kLowPercent) {
        c.health = Health::Low;
    } else {
        c.health = Health::Ok;
    }
    return c;
}

const char *healthName(Health health) {
    switch (health) {
    case Health::Ok: return "OK";
    case Health::Low: return "LOW";
    case Health::Critical: return "CRITICAL";
    case Health::Full: return "FULL";
    }
    return "?";
}

size_t NvsModel::unattributedEntries() const {
    size_t listed = 0;
    for (const auto &ns : namespaces_) listed += ns.usedEntries;
    return stats_.usedEntries > listed ? stats_.usedEntries - listed : 0;
}

void NvsModel::refresh() {
    hasPartition_ = false;
    stats_ = nvsm::Stats{};
    namespaces_.clear();
    namespacesError_ = ESP_OK;

    for (const auto &p : nvsm::NvsReader::findPartitions()) {
        if (p.label == reader_.label()) {
            partition_ = p;
            hasPartition_ = true;
        }
    }

    errorStep_ = "init nvs";
    error_ = reader_.init();
    if (error_ == ESP_OK) {
        errorStep_ = "nvs_get_stats";
        error_ = reader_.stats(stats_);
    }
    if (error_ != ESP_OK) {
        logError(errorStep_, error_);
        return;
    }
    errorStep_ = "";

    namespacesError_ = reader_.namespaces(namespaces_);
    if (namespacesError_ != ESP_OK) logError("list namespaces", namespacesError_);
}

ChangeOutcome NvsModel::runChange(const char *tag, const std::string &target,
                                  const std::function<nvsm::WriteResult()> &change) {
    ChangeOutcome outcome;
    const bool haveBefore = reader_.stats(outcome.before) == ESP_OK;

    platform::logf(tag, "%s request", target.c_str());
    outcome.result = change();
    if (outcome.result.ok()) {
        platform::logf(tag, "%s commit=ESP_OK verified", target.c_str());
    } else {
        platform::logf(tag, "%s failed at %s: %s (0x%x)", target.c_str(), outcome.result.step,
                       esp_err_to_name(outcome.result.err), static_cast<unsigned>(outcome.result.err));
    }

    refresh();
    outcome.after = stats_;
    outcome.statsValid = haveBefore && error_ == ESP_OK;
    if (outcome.statsValid) {
        platform::logf("NVS", "available before=%u after=%u, used before=%u after=%u",
                       static_cast<unsigned>(outcome.before.availableEntries),
                       static_cast<unsigned>(outcome.after.availableEntries),
                       static_cast<unsigned>(outcome.before.usedEntries), static_cast<unsigned>(outcome.after.usedEntries));
    }
    return outcome;
}

ChangeOutcome NvsModel::eraseKey(const std::string &ns, const std::string &key) {
    return runChange("DELETE", "namespace=" + ns + " key=" + key, [&]() { return writer_.eraseKey(ns, key); });
}

ChangeOutcome NvsModel::eraseNamespace(const std::string &ns) {
    return runChange("DELETE", "namespace=" + ns, [&]() { return writer_.eraseNamespace(ns); });
}

ChangeOutcome NvsModel::setValue(const std::string &ns, const std::string &key, const nvsm::Value &value) {
    // The target names the key and type only; values never reach the log.
    return runChange("EDIT", "namespace=" + ns + " key=" + key + " type=" + nvsm::typeName(value.type),
                     [&]() { return writer_.setValue(ns, key, value); });
}

ChangeOutcome NvsModel::restorePartition(const std::vector<uint8_t> &image, const std::string &source) {
    return runChange("RESTORE", "partition=" + label() + " from=" + source,
                     [&]() { return writer_.restorePartition(image); });
}

void NvsModel::logReport() const {
    for (const auto &p : nvsm::NvsReader::findPartitions()) {
        platform::logf("NVS", "partition %s @0x%06x size 0x%x%s", p.label.c_str(), static_cast<unsigned>(p.address),
                       static_cast<unsigned>(p.size), p.encrypted ? " encrypted" : "");
    }
    if (error_ != ESP_OK || namespacesError_ != ESP_OK) return; // refresh() already logged why

    platform::logf("NVS", "used=%u free=%u available=%u total=%u namespaces=%u", static_cast<unsigned>(stats_.usedEntries),
                   static_cast<unsigned>(stats_.freeEntries), static_cast<unsigned>(stats_.availableEntries),
                   static_cast<unsigned>(stats_.totalEntries), static_cast<unsigned>(stats_.namespaceCount));
    for (const auto &ns : namespaces_) {
        platform::logf("NVS", "ns %-15s entries=%u keys=%u", ns.name.c_str(), static_cast<unsigned>(ns.usedEntries),
                       static_cast<unsigned>(ns.keyCount));
        std::vector<nvsm::KeyInfo> keys;
        const esp_err_t err = reader_.keys(ns.name, keys);
        if (err != ESP_OK) {
            logError("list keys", err);
            continue;
        }
        for (const auto &k : keys) {
            platform::logf("NVS", "  key %-15s %-4s size=%u entries~%u", k.name.c_str(), nvsm::typeName(k.type),
                           static_cast<unsigned>(k.dataSize), static_cast<unsigned>(k.entriesEstimate));
        }
    }
    // The public API cannot list namespaces without keys, nor entries whose
    // namespace entry is gone; both still occupy space.
    platform::logf("NVS", "listed namespaces use %u of %u used entries (%u in empty or orphaned namespaces)",
                   static_cast<unsigned>(stats_.usedEntries - unattributedEntries()),
                   static_cast<unsigned>(stats_.usedEntries), static_cast<unsigned>(unattributedEntries()));
}

} // namespace app
