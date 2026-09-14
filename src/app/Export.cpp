#include "app/Export.h"

#include <algorithm>
#include <cstdio>

#include "nvs/NvsProtection.h"
#include "platform/Platform.h"
#include "util/Json.h"
#include "util/Sha256.h"

namespace app {
namespace {

constexpr const char *kExportDir = "/NVSManager/exports";
constexpr const char *kBackupDir = "/NVSManager/backups";
constexpr int kFormatVersion = 1;
constexpr int kMaxSequence = 9999;

// Next free "<dir>/nvs-NNNN" such that none of `suffixes` exists yet. The
// device has no clock, so files are numbered instead of dated.
std::string nextBase(const char *dir, const std::vector<const char *> &suffixes) {
    char base[64];
    for (int n = 1; n <= kMaxSequence; ++n) {
        snprintf(base, sizeof base, "%s/nvs-%04d", dir, n);
        bool taken = false;
        for (const char *suffix : suffixes) taken = taken || platform::storageExists(std::string(base) + suffix);
        if (!taken) return base;
    }
    return {};
}

void writeHeader(util::JsonWriter &j, const NvsModel &model) {
    j.key("tool").str("NVS Manager");
    j.key("version").str(NVSM_VERSION);
    j.key("format").i64(kFormatVersion);
    j.key("device").str(platform::deviceName());
    j.key("chip").str(platform::chipModel());
    j.key("time").null(); // no real-time clock
    j.key("uptime_ms").u64(platform::millis());

    j.key("partition").beginObject();
    j.key("label").str(model.label());
    j.key("address").u64(model.hasPartition() ? model.partition().address : 0);
    j.key("size").u64(model.hasPartition() ? model.partition().size : 0);
    j.endObject();

    const nvsm::Stats &s = model.stats();
    j.key("stats").beginObject();
    j.key("used").u64(s.usedEntries);
    j.key("free").u64(s.freeEntries);
    j.key("available").u64(s.availableEntries);
    j.key("total").u64(s.totalEntries);
    j.key("namespaces").u64(s.namespaceCount);
    j.key("unattributed").u64(model.unattributedEntries());
    j.endObject();
}

void writeValue(util::JsonWriter &j, const nvsm::Value &v) {
    switch (v.type) {
    case nvsm::ValueType::I8:
    case nvsm::ValueType::I16:
    case nvsm::ValueType::I32: j.i64(v.i); break;
    case nvsm::ValueType::U8:
    case nvsm::ValueType::U16:
    case nvsm::ValueType::U32: j.u64(v.u); break;
    // Beyond 2^53 JSON numbers lose precision in most readers.
    case nvsm::ValueType::I64: j.str(std::to_string(v.i)); break;
    case nvsm::ValueType::U64: j.str(std::to_string(v.u)); break;
    case nvsm::ValueType::Str: j.str(v.str); break;
    case nvsm::ValueType::Blob: j.str(util::toHex(v.blob.data(), v.blob.size())); break;
    case nvsm::ValueType::Unknown: j.null(); break;
    }
}

// Writes, reads back and compares; returns an error text or "".
std::string writeVerified(const std::string &path, const std::string &content) {
    const auto *data = reinterpret_cast<const uint8_t *>(content.data());
    if (!platform::storageWrite(path, data, content.size())) return "cannot write " + path;
    std::vector<uint8_t> back;
    if (!platform::storageRead(path, back)) return "cannot read back " + path;
    if (back.size() != content.size() || !std::equal(back.begin(), back.end(), data)) return "read-back differs: " + path;
    return "";
}

} // namespace

ExportResult exportJson(const NvsModel &model, bool includeValues) {
    ExportResult r;
    if (!platform::storageMounted()) {
        r.error = "SD card not available";
        return r;
    }
    if (model.error() != ESP_OK) {
        r.error = std::string("NVS cannot be read: ") + esp_err_to_name(model.error());
        return r;
    }
    if (!platform::storageMakeDirs(kExportDir)) {
        r.error = std::string("cannot create ") + kExportDir;
        return r;
    }
    const char *suffix = includeValues ? "-values.json" : ".json";
    const std::string base = nextBase(kExportDir, {suffix});
    if (base.empty()) {
        r.error = "no free file name";
        return r;
    }

    util::JsonWriter j;
    j.beginObject();
    writeHeader(j, model);
    j.key("values_included").boolean(includeValues);
    j.key("namespaces").beginArray();
    for (const auto &ns : model.namespaces()) {
        j.beginObject();
        j.key("name").str(ns.name);
        j.key("class").str(nvsm::className(nvsm::classify(ns.name)));
        j.key("entries").u64(ns.usedEntries);
        std::vector<nvsm::KeyInfo> keys;
        const esp_err_t err = model.keys(ns.name, keys);
        if (err != ESP_OK) j.key("error").str(esp_err_to_name(err));
        j.key("keys").beginArray();
        for (const auto &k : keys) {
            j.beginObject();
            j.key("key").str(k.name);
            j.key("type").str(nvsm::typeName(k.type));
            j.key("size").u64(k.dataSize);
            j.key("entries").u64(k.entriesEstimate);
            if (includeValues) {
                nvsm::Value v;
                const esp_err_t readErr = model.readValue(ns.name, k, v);
                if (readErr == ESP_OK) {
                    j.key("value");
                    writeValue(j, v);
                } else {
                    j.key("value_error").str(esp_err_to_name(readErr));
                }
            }
            j.endObject();
        }
        j.endArray();
        j.endObject();
    }
    j.endArray();
    j.endObject();

    const std::string path = base + suffix;
    r.error = writeVerified(path, j.text());
    if (!r.error.empty()) return r;

    const auto digest = util::sha256(reinterpret_cast<const uint8_t *>(j.text().data()), j.text().size());
    r.ok = true;
    r.files.push_back(path);
    r.bytes = j.text().size();
    r.sha256 = util::toHex(digest.data(), digest.size());
    platform::logf("BACKUP", "export %s -> %s (%u B)", includeValues ? "with values" : "metadata", path.c_str(),
                   static_cast<unsigned>(r.bytes));
    return r;
}

ExportResult rawBackup(const NvsModel &model) {
    ExportResult r;
    if (!platform::storageMounted()) {
        r.error = "SD card not available";
        return r;
    }
    nvsm::PartitionInfo info;
    std::vector<uint8_t> image;
    const esp_err_t err = model.readPartition(info, image);
    if (err != ESP_OK) {
        r.error = std::string("cannot read partition: ") + esp_err_to_name(err);
        return r;
    }
    if (!platform::storageMakeDirs(kBackupDir)) {
        r.error = std::string("cannot create ") + kBackupDir;
        return r;
    }
    const std::string base = nextBase(kBackupDir, {".bin", ".json"});
    if (base.empty()) {
        r.error = "no free file name";
        return r;
    }

    const auto digest = util::sha256(image.data(), image.size());
    const std::string hex = util::toHex(digest.data(), digest.size());
    const std::string binPath = base + ".bin";
    if (!platform::storageWrite(binPath, image.data(), image.size())) {
        r.error = "cannot write " + binPath;
        return r;
    }
    std::vector<uint8_t> back;
    if (!platform::storageRead(binPath, back)) {
        r.error = "cannot read back " + binPath;
        return r;
    }
    const auto backDigest = util::sha256(back.data(), back.size());
    if (backDigest != digest) {
        r.error = "read-back SHA-256 differs: " + binPath;
        return r;
    }

    util::JsonWriter j;
    j.beginObject();
    writeHeader(j, model);
    j.key("file").str(binPath.substr(binPath.find_last_of('/') + 1));
    j.key("offset").u64(info.address);
    j.key("bytes").u64(image.size());
    j.key("sha256").str(hex);
    j.key("encrypted").boolean(info.encrypted);
    j.endObject();
    const std::string manifestPath = base + ".json";
    r.error = writeVerified(manifestPath, j.text());
    if (!r.error.empty()) return r;

    r.ok = true;
    r.files = {binPath, manifestPath};
    r.bytes = image.size();
    r.sha256 = hex;
    platform::logf("BACKUP", "raw %s @0x%x -> %s (%u B, sha256 %s)", info.label.c_str(),
                   static_cast<unsigned>(info.address), binPath.c_str(), static_cast<unsigned>(r.bytes), hex.c_str());
    return r;
}

} // namespace app
