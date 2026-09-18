#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Minimal JSON reader, enough to read back the manifests this application
// writes itself (util/Json.h). Not a general-purpose parser: numbers are kept
// as their literal text, and the depth is limited.
namespace util {

class JsonValue {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    Type type = Type::Null;
    bool boolean = false;
    std::string text;              // String: the decoded string; Number: the literal
    std::vector<JsonValue> items;  // Array
    std::vector<std::string> keys; // Object: member names, in file order
    std::vector<JsonValue> values; // Object: member values, parallel to `keys`

    // The member `key` of an object; nullptr when absent or not an object.
    const JsonValue *get(const std::string &key) const;

    // Member lookups with a fallback for a missing or differently typed member.
    std::string str(const std::string &key, const std::string &fallback = "") const;
    uint64_t u64(const std::string &key, uint64_t fallback = 0) const;
    bool flag(const std::string &key, bool fallback = false) const;
};

// Parses a whole document. On failure returns false and says where in `error`.
bool parseJson(const std::string &input, JsonValue &out, std::string &error);

} // namespace util
