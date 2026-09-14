#include "nvs/NvsReader.h"

#include <algorithm>
#include <set>

#include <esp_partition.h>
#include <nvs.h>
#include <nvs_flash.h>

#include "nvs/NvsHandle.h"

namespace nvsm {
namespace {

constexpr size_t kEntrySize = 32;

ValueType fromNvsType(nvs_type_t type) {
    switch (type) {
    case NVS_TYPE_U8: return ValueType::U8;
    case NVS_TYPE_I8: return ValueType::I8;
    case NVS_TYPE_U16: return ValueType::U16;
    case NVS_TYPE_I16: return ValueType::I16;
    case NVS_TYPE_U32: return ValueType::U32;
    case NVS_TYPE_I32: return ValueType::I32;
    case NVS_TYPE_U64: return ValueType::U64;
    case NVS_TYPE_I64: return ValueType::I64;
    case NVS_TYPE_STR: return ValueType::Str;
    case NVS_TYPE_BLOB: return ValueType::Blob;
    default: return ValueType::Unknown;
    }
}

// Scalars take one entry; strings and blob chunks take a header entry plus
// their data rounded up to whole entries. Blobs larger than one chunk also
// carry an index entry, which this estimate ignores.
size_t estimateEntries(ValueType type, size_t dataSize) {
    if (type != ValueType::Str && type != ValueType::Blob) return 1;
    return 1 + (dataSize + kEntrySize - 1) / kEntrySize;
}

} // namespace

const char *typeName(ValueType type) {
    switch (type) {
    case ValueType::U8: return "u8";
    case ValueType::I8: return "i8";
    case ValueType::U16: return "u16";
    case ValueType::I16: return "i16";
    case ValueType::U32: return "u32";
    case ValueType::I32: return "i32";
    case ValueType::U64: return "u64";
    case ValueType::I64: return "i64";
    case ValueType::Str: return "str";
    case ValueType::Blob: return "blob";
    case ValueType::Unknown: return "?";
    }
    return "?";
}

std::vector<PartitionInfo> NvsReader::findPartitions() {
    std::vector<PartitionInfo> out;
    esp_partition_iterator_t it = esp_partition_find(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_NVS, nullptr);
    for (; it != nullptr; it = esp_partition_next(it)) {
        const esp_partition_t *p = esp_partition_get(it);
        out.push_back({p->label, p->address, p->size, p->encrypted});
    }
    esp_partition_iterator_release(it); // accepts nullptr
    return out;
}

esp_err_t NvsReader::readPartition(PartitionInfo &info, std::vector<uint8_t> &out) const {
    const esp_partition_t *p =
        esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_NVS, label_.c_str());
    if (p == nullptr) return ESP_ERR_NOT_FOUND;
    info = {p->label, p->address, p->size, p->encrypted};
    out.resize(p->size);
    return esp_partition_read(p, 0, out.data(), out.size());
}

esp_err_t NvsReader::init() const {
    // Already-initialised partitions return ESP_OK. On any error the caller
    // shows it; nothing here falls back to erasing.
    return nvs_flash_init_partition(label_.c_str());
}

esp_err_t NvsReader::stats(Stats &out) const {
    nvs_stats_t s{};
    esp_err_t err = nvs_get_stats(label_.c_str(), &s);
    if (err != ESP_OK) return err;
    out.usedEntries = s.used_entries;
    out.freeEntries = s.free_entries;
    out.availableEntries = s.available_entries;
    out.totalEntries = s.total_entries;
    out.namespaceCount = s.namespace_count;
    return ESP_OK;
}

esp_err_t NvsReader::namespaces(std::vector<NamespaceInfo> &out) const {
    out.clear();

    // A partition-wide iteration only tells which namespaces hold keys. Its
    // namespace_name cannot be used for counting: ESP-IDF 5.5
    // Storage::fillEntryInfo keeps the previous name for an entry whose
    // namespace entry is gone, so orphaned entries show up under an unrelated
    // namespace (seen on a real device, docs/RESEARCH.md).
    std::set<std::string> names;
    nvs_iterator_t it = nullptr;
    esp_err_t err = nvs_entry_find(label_.c_str(), nullptr, NVS_TYPE_ANY, &it);
    while (err == ESP_OK) {
        nvs_entry_info_t info;
        nvs_entry_info(it, &info);
        if (info.namespace_name[0] != '\0') names.insert(info.namespace_name);
        err = nvs_entry_next(&it);
    }
    nvs_release_iterator(it);
    if (err != ESP_ERR_NVS_NOT_FOUND) return err;

    for (const auto &name : names) {
        ScopedHandle handle(label_, name, NVS_READONLY);
        if (handle.error() == ESP_ERR_NVS_NOT_FOUND) continue; // stale name that is not a namespace
        if (handle.error() != ESP_OK) return handle.error();

        size_t used = 0;
        err = nvs_get_used_entry_count(handle.get(), &used);
        if (err != ESP_OK) return err;

        // Counting inside one namespace filters by its index, so orphans stay out.
        size_t keyCount = 0;
        nvs_iterator_t nsIt = nullptr;
        err = nvs_entry_find(label_.c_str(), name.c_str(), NVS_TYPE_ANY, &nsIt);
        while (err == ESP_OK) {
            ++keyCount;
            err = nvs_entry_next(&nsIt);
        }
        nvs_release_iterator(nsIt);
        if (err != ESP_ERR_NVS_NOT_FOUND) return err;

        out.push_back({name, used + 1, keyCount});
    }

    std::sort(out.begin(), out.end(), [](const NamespaceInfo &a, const NamespaceInfo &b) {
        return a.usedEntries != b.usedEntries ? a.usedEntries > b.usedEntries : a.name < b.name;
    });
    return ESP_OK;
}

esp_err_t NvsReader::keys(const std::string &ns, std::vector<KeyInfo> &out) const {
    out.clear();
    ScopedHandle handle(label_, ns, NVS_READONLY);
    if (handle.error() != ESP_OK) return handle.error();

    nvs_iterator_t it = nullptr;
    esp_err_t err = nvs_entry_find(label_.c_str(), ns.c_str(), NVS_TYPE_ANY, &it);
    while (err == ESP_OK) {
        nvs_entry_info_t info;
        nvs_entry_info(it, &info);

        KeyInfo key;
        key.name = info.key;
        key.type = fromNvsType(info.type);
        if (key.type == ValueType::Str) {
            err = nvs_get_str(handle.get(), info.key, nullptr, &key.dataSize);
        } else if (key.type == ValueType::Blob) {
            err = nvs_get_blob(handle.get(), info.key, nullptr, &key.dataSize);
        }
        if (err != ESP_OK) break; // report the size query failure as is
        key.entriesEstimate = estimateEntries(key.type, key.dataSize);
        out.push_back(std::move(key));

        err = nvs_entry_next(&it);
    }
    nvs_release_iterator(it);
    if (err != ESP_ERR_NVS_NOT_FOUND) return err;

    std::sort(out.begin(), out.end(), [](const KeyInfo &a, const KeyInfo &b) { return a.name < b.name; });
    return ESP_OK;
}

esp_err_t NvsReader::readValue(const std::string &ns, const std::string &key, ValueType type, Value &out) const {
    out = Value{};
    out.type = type;
    ScopedHandle handle(label_, ns, NVS_READONLY);
    if (handle.error() != ESP_OK) return handle.error();
    const nvs_handle_t h = handle.get();
    const char *k = key.c_str();

    switch (type) {
    case ValueType::U8: {
        uint8_t v = 0;
        const esp_err_t err = nvs_get_u8(h, k, &v);
        out.u = v;
        return err;
    }
    case ValueType::I8: {
        int8_t v = 0;
        const esp_err_t err = nvs_get_i8(h, k, &v);
        out.i = v;
        return err;
    }
    case ValueType::U16: {
        uint16_t v = 0;
        const esp_err_t err = nvs_get_u16(h, k, &v);
        out.u = v;
        return err;
    }
    case ValueType::I16: {
        int16_t v = 0;
        const esp_err_t err = nvs_get_i16(h, k, &v);
        out.i = v;
        return err;
    }
    case ValueType::U32: {
        uint32_t v = 0;
        const esp_err_t err = nvs_get_u32(h, k, &v);
        out.u = v;
        return err;
    }
    case ValueType::I32: {
        int32_t v = 0;
        const esp_err_t err = nvs_get_i32(h, k, &v);
        out.i = v;
        return err;
    }
    case ValueType::U64: return nvs_get_u64(h, k, &out.u);
    case ValueType::I64: return nvs_get_i64(h, k, &out.i);
    case ValueType::Str: {
        size_t len = 0;
        esp_err_t err = nvs_get_str(h, k, nullptr, &len);
        if (err != ESP_OK) return err;
        std::string s(len, '\0');
        err = nvs_get_str(h, k, &s[0], &len);
        if (err != ESP_OK) return err;
        if (!s.empty() && s.back() == '\0') s.pop_back();
        out.str = std::move(s);
        return ESP_OK;
    }
    case ValueType::Blob: {
        size_t len = 0;
        esp_err_t err = nvs_get_blob(h, k, nullptr, &len);
        if (err != ESP_OK) return err;
        out.blob.resize(len);
        return nvs_get_blob(h, k, out.blob.data(), &len);
    }
    case ValueType::Unknown: break;
    }
    return ESP_ERR_NVS_TYPE_MISMATCH;
}

} // namespace nvsm
