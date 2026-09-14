#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Plain data describing an NVS partition. Filled by the backend, drawn by the
// UI; nothing here talks to flash.
namespace nvsm {

enum class ValueType : uint8_t { U8, I8, U16, I16, U32, I32, U64, I64, Str, Blob, Unknown };

const char *typeName(ValueType type);

struct PartitionInfo {
    std::string label;
    uint32_t address = 0;
    uint32_t size = 0;
    bool encrypted = false;
};

struct Stats {
    size_t usedEntries = 0;
    size_t freeEntries = 0;
    size_t availableEntries = 0; // what new data can still use; free minus the page kept for GC
    size_t totalEntries = 0;
    size_t namespaceCount = 0; // includes namespaces that no longer hold any key
};

struct NamespaceInfo {
    std::string name;
    size_t usedEntries = 0; // includes the namespace's own entry
    size_t keyCount = 0;
};

struct KeyInfo {
    std::string name;
    ValueType type = ValueType::Unknown;
    size_t dataSize = 0;         // bytes for Str (with the terminating NUL) and Blob, 0 otherwise
    size_t entriesEstimate = 0;  // entries this key occupies, estimated from its size
};

// One value read from NVS. Only the member matching `type` is meaningful.
// Strings and blobs are held whole, so keep a Value only while it is shown.
struct Value {
    ValueType type = ValueType::Unknown;
    int64_t i = 0;             // I8 .. I64
    uint64_t u = 0;            // U8 .. U64
    std::string str;           // Str, without the terminating NUL
    std::vector<uint8_t> blob; // Blob
};

} // namespace nvsm
