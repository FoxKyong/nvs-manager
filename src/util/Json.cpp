#include "util/Json.h"

#include <cinttypes>
#include <cstdio>

namespace util {

void JsonWriter::newline() {
    out_ += '\n';
    out_.append(empty_.size() * 2, ' ');
}

void JsonWriter::beforeValue() {
    if (afterKey_) {
        afterKey_ = false;
        return;
    }
    if (empty_.empty()) return;
    if (!empty_.back()) out_ += ',';
    empty_.back() = false;
    newline();
}

void JsonWriter::quoted(const std::string &text) {
    out_ += '"';
    for (unsigned char c : text) {
        switch (c) {
        case '"': out_ += "\\\""; break;
        case '\\': out_ += "\\\\"; break;
        case '\n': out_ += "\\n"; break;
        case '\r': out_ += "\\r"; break;
        case '\t': out_ += "\\t"; break;
        default:
            if (c < 0x20 || c >= 0x80) {
                char buf[8];
                snprintf(buf, sizeof buf, "\\u%04x", c);
                out_ += buf;
            } else {
                out_ += static_cast<char>(c);
            }
        }
    }
    out_ += '"';
}

void JsonWriter::open(char bracket) {
    beforeValue();
    out_ += bracket;
    empty_.push_back(true);
}

void JsonWriter::close(char bracket) {
    const bool wasEmpty = empty_.back();
    empty_.pop_back();
    if (!wasEmpty) newline();
    out_ += bracket;
    if (empty_.empty()) out_ += '\n';
}

JsonWriter &JsonWriter::beginObject() {
    open('{');
    return *this;
}

JsonWriter &JsonWriter::endObject() {
    close('}');
    return *this;
}

JsonWriter &JsonWriter::beginArray() {
    open('[');
    return *this;
}

JsonWriter &JsonWriter::endArray() {
    close(']');
    return *this;
}

JsonWriter &JsonWriter::key(const char *name) {
    beforeValue();
    quoted(name);
    out_ += ": ";
    afterKey_ = true;
    return *this;
}

JsonWriter &JsonWriter::str(const std::string &value) {
    beforeValue();
    quoted(value);
    return *this;
}

JsonWriter &JsonWriter::i64(int64_t value) {
    beforeValue();
    char buf[24];
    snprintf(buf, sizeof buf, "%" PRId64, value);
    out_ += buf;
    return *this;
}

JsonWriter &JsonWriter::u64(uint64_t value) {
    beforeValue();
    char buf[24];
    snprintf(buf, sizeof buf, "%" PRIu64, value);
    out_ += buf;
    return *this;
}

JsonWriter &JsonWriter::boolean(bool value) {
    beforeValue();
    out_ += value ? "true" : "false";
    return *this;
}

JsonWriter &JsonWriter::null() {
    beforeValue();
    out_ += "null";
    return *this;
}

} // namespace util
