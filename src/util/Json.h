#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Small streaming JSON writer with two-space indentation; enough for exports
// and manifests without pulling in a JSON library.
namespace util {

class JsonWriter {
public:
    JsonWriter &beginObject();
    JsonWriter &endObject();
    JsonWriter &beginArray();
    JsonWriter &endArray();

    JsonWriter &key(const char *name); // inside an object, before its value

    JsonWriter &str(const std::string &value); // bytes >= 0x80 are written as \u00XX
    JsonWriter &i64(int64_t value);
    JsonWriter &u64(uint64_t value);
    JsonWriter &boolean(bool value);
    JsonWriter &null();

    const std::string &text() const { return out_; }

private:
    void beforeValue();
    void quoted(const std::string &text); // escaped string, no separator
    void open(char bracket);
    void close(char bracket);
    void newline();

    std::string out_;
    std::vector<bool> empty_; // per open container: nothing written yet
    bool afterKey_ = false;
};

} // namespace util
