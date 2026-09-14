#include "nvs/NvsWriter.h"

#include <nvs.h>

#include "nvs/NvsHandle.h"
#include "nvs/NvsReader.h"

namespace nvsm {
namespace {

WriteResult failed(esp_err_t err, const char *step) { return WriteResult{err, step}; }

// Opening read-write creates a missing namespace, which would itself write
// NVS. Confirm it exists with a read-only open first.
esp_err_t requireNamespace(const std::string &label, const std::string &ns) {
    ScopedHandle probe(label, ns, NVS_READONLY);
    return probe.error();
}

esp_err_t setTyped(nvs_handle_t h, const char *key, const Value &v) {
    switch (v.type) {
    case ValueType::U8: return nvs_set_u8(h, key, static_cast<uint8_t>(v.u));
    case ValueType::I8: return nvs_set_i8(h, key, static_cast<int8_t>(v.i));
    case ValueType::U16: return nvs_set_u16(h, key, static_cast<uint16_t>(v.u));
    case ValueType::I16: return nvs_set_i16(h, key, static_cast<int16_t>(v.i));
    case ValueType::U32: return nvs_set_u32(h, key, static_cast<uint32_t>(v.u));
    case ValueType::I32: return nvs_set_i32(h, key, static_cast<int32_t>(v.i));
    case ValueType::U64: return nvs_set_u64(h, key, v.u);
    case ValueType::I64: return nvs_set_i64(h, key, v.i);
    case ValueType::Str: return nvs_set_str(h, key, v.str.c_str());
    case ValueType::Blob:
    case ValueType::Unknown: break;
    }
    return ESP_ERR_NOT_SUPPORTED;
}

bool sameValue(const Value &a, const Value &b) {
    if (a.type != b.type) return false;
    switch (a.type) {
    case ValueType::I8:
    case ValueType::I16:
    case ValueType::I32:
    case ValueType::I64: return a.i == b.i;
    case ValueType::U8:
    case ValueType::U16:
    case ValueType::U32:
    case ValueType::U64: return a.u == b.u;
    case ValueType::Str: return a.str == b.str;
    default: return false;
    }
}

} // namespace

WriteResult NvsWriter::eraseKey(const std::string &ns, const std::string &key) const {
    esp_err_t err = requireNamespace(label_, ns);
    if (err != ESP_OK) return failed(err, "open");
    {
        ScopedHandle rw(label_, ns, NVS_READWRITE);
        if (rw.error() != ESP_OK) return failed(rw.error(), "open");
        err = nvs_erase_key(rw.get(), key.c_str());
        if (err != ESP_OK) return failed(err, "erase");
        err = nvs_commit(rw.get());
        if (err != ESP_OK) return failed(err, "commit");
    }

    ScopedHandle ro(label_, ns, NVS_READONLY);
    if (ro.error() != ESP_OK) return failed(ro.error(), "verify");
    err = nvs_find_key(ro.get(), key.c_str(), nullptr);
    if (err == ESP_OK) return failed(ESP_ERR_INVALID_STATE, "verify"); // still there
    if (err != ESP_ERR_NVS_NOT_FOUND) return failed(err, "verify");
    return {};
}

WriteResult NvsWriter::eraseNamespace(const std::string &ns) const {
    esp_err_t err = requireNamespace(label_, ns);
    if (err != ESP_OK) return failed(err, "open");
    {
        ScopedHandle rw(label_, ns, NVS_READWRITE);
        if (rw.error() != ESP_OK) return failed(rw.error(), "open");
        err = nvs_erase_all(rw.get());
        if (err != ESP_OK) return failed(err, "erase");
        err = nvs_commit(rw.get());
        if (err != ESP_OK) return failed(err, "commit");
    }

    nvs_iterator_t it = nullptr;
    err = nvs_entry_find(label_.c_str(), ns.c_str(), NVS_TYPE_ANY, &it);
    nvs_release_iterator(it);
    if (err == ESP_OK) return failed(ESP_ERR_INVALID_STATE, "verify"); // a key survived
    if (err != ESP_ERR_NVS_NOT_FOUND) return failed(err, "verify");
    return {};
}

WriteResult NvsWriter::setValue(const std::string &ns, const std::string &key, const Value &value) const {
    esp_err_t err = requireNamespace(label_, ns);
    if (err != ESP_OK) return failed(err, "open");
    {
        ScopedHandle rw(label_, ns, NVS_READWRITE);
        if (rw.error() != ESP_OK) return failed(rw.error(), "open");
        err = setTyped(rw.get(), key.c_str(), value);
        if (err != ESP_OK) return failed(err, "set");
        err = nvs_commit(rw.get());
        if (err != ESP_OK) return failed(err, "commit");
    }

    Value readBack;
    err = NvsReader(label_).readValue(ns, key, value.type, readBack);
    if (err != ESP_OK) return failed(err, "verify");
    if (!sameValue(value, readBack)) return failed(ESP_ERR_INVALID_STATE, "verify");
    return {};
}

} // namespace nvsm
