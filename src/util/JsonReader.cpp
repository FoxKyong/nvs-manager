#include "util/JsonReader.h"

#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <cstring>

namespace util {
namespace {

class Parser {
public:
    explicit Parser(const std::string &input) : s_(input) {}

    bool parse(JsonValue &out, std::string &error) {
        const bool ok = value(out, 0) && (skipSpace(), pos_ == s_.size() || fail("trailing characters"));
        if (!ok) error = error_;
        return ok;
    }

private:
    static constexpr int kMaxDepth = 16;

    bool fail(const char *what) {
        if (error_.empty()) error_ = std::string(what) + " at byte " + std::to_string(pos_);
        return false;
    }

    void skipSpace() {
        while (pos_ < s_.size() && (s_[pos_] == ' ' || s_[pos_] == '\n' || s_[pos_] == '\r' || s_[pos_] == '\t')) ++pos_;
    }

    bool at(char c) const { return pos_ < s_.size() && s_[pos_] == c; }

    bool literal(const char *word) {
        const size_t n = std::strlen(word);
        if (s_.compare(pos_, n, word) != 0) return false;
        pos_ += n;
        return true;
    }

    bool value(JsonValue &v, int depth) {
        if (depth > kMaxDepth) return fail("nested too deeply");
        skipSpace();
        if (pos_ >= s_.size()) return fail("unexpected end");
        const char c = s_[pos_];
        if (c == '{') return object(v, depth);
        if (c == '[') return array(v, depth);
        if (c == '"') {
            v.type = JsonValue::Type::String;
            return string(v.text);
        }
        if (literal("true") || literal("false")) {
            v.type = JsonValue::Type::Bool;
            v.boolean = c == 't';
            return true;
        }
        if (literal("null")) {
            v.type = JsonValue::Type::Null;
            return true;
        }
        if (c == '-' || std::isdigit(static_cast<unsigned char>(c))) return number(v);
        return fail("unexpected character");
    }

    bool number(JsonValue &v) {
        const size_t start = pos_;
        if (at('-')) ++pos_;
        while (pos_ < s_.size() && (std::isdigit(static_cast<unsigned char>(s_[pos_])) || std::strchr(".eE+-", s_[pos_]))) {
            ++pos_;
        }
        if (pos_ == start || (pos_ == start + 1 && s_[start] == '-')) return fail("bad number");
        v.type = JsonValue::Type::Number;
        v.text = s_.substr(start, pos_ - start);
        return true;
    }

    bool string(std::string &out) {
        ++pos_; // opening quote
        while (pos_ < s_.size()) {
            const char c = s_[pos_++];
            if (c == '"') return true;
            if (c != '\\') {
                out += c;
                continue;
            }
            if (pos_ >= s_.size()) break;
            const char e = s_[pos_++];
            switch (e) {
            case '"':
            case '\\':
            case '/': out += e; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case 'u': {
                if (pos_ + 4 > s_.size()) return fail("bad escape");
                char hex[5] = {s_[pos_], s_[pos_ + 1], s_[pos_ + 2], s_[pos_ + 3], 0};
                char *end = nullptr;
                const unsigned long code = std::strtoul(hex, &end, 16);
                if (end != hex + 4) return fail("bad escape");
                pos_ += 4;
                if (code < 0x100) {
                    // JsonWriter writes every byte >= 0x80 as \u00XX; give the byte back.
                    out += static_cast<char>(code);
                } else if (code < 0x800) {
                    out += static_cast<char>(0xC0 | (code >> 6));
                    out += static_cast<char>(0x80 | (code & 0x3F));
                } else {
                    out += static_cast<char>(0xE0 | (code >> 12));
                    out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
                    out += static_cast<char>(0x80 | (code & 0x3F));
                }
                break;
            }
            default: return fail("bad escape");
            }
        }
        return fail("unterminated string");
    }

    bool array(JsonValue &v, int depth) {
        v.type = JsonValue::Type::Array;
        ++pos_;
        skipSpace();
        if (at(']')) {
            ++pos_;
            return true;
        }
        for (;;) {
            v.items.emplace_back();
            if (!value(v.items.back(), depth + 1)) return false;
            skipSpace();
            if (at(',')) {
                ++pos_;
            } else if (at(']')) {
                ++pos_;
                return true;
            } else {
                return fail("expected , or ]");
            }
        }
    }

    bool object(JsonValue &v, int depth) {
        v.type = JsonValue::Type::Object;
        ++pos_;
        skipSpace();
        if (at('}')) {
            ++pos_;
            return true;
        }
        for (;;) {
            skipSpace();
            if (!at('"')) return fail("expected a member name");
            v.keys.emplace_back();
            if (!string(v.keys.back())) return false;
            skipSpace();
            if (!at(':')) return fail("expected :");
            ++pos_;
            v.values.emplace_back();
            if (!value(v.values.back(), depth + 1)) return false;
            skipSpace();
            if (at(',')) {
                ++pos_;
            } else if (at('}')) {
                ++pos_;
                return true;
            } else {
                return fail("expected , or }");
            }
        }
    }

    const std::string &s_;
    size_t pos_ = 0;
    std::string error_;
};

} // namespace

const JsonValue *JsonValue::get(const std::string &key) const {
    if (type != Type::Object) return nullptr;
    for (size_t i = 0; i < keys.size(); ++i) {
        if (keys[i] == key) return &values[i];
    }
    return nullptr;
}

std::string JsonValue::str(const std::string &key, const std::string &fallback) const {
    const JsonValue *v = get(key);
    return v != nullptr && v->type == Type::String ? v->text : fallback;
}

uint64_t JsonValue::u64(const std::string &key, uint64_t fallback) const {
    const JsonValue *v = get(key);
    if (v == nullptr || v->type != Type::Number || v->text.empty() || v->text[0] == '-') return fallback;
    errno = 0;
    char *end = nullptr;
    const unsigned long long n = std::strtoull(v->text.c_str(), &end, 10);
    return errno == 0 && end != nullptr && *end == '\0' ? n : fallback;
}

bool JsonValue::flag(const std::string &key, bool fallback) const {
    const JsonValue *v = get(key);
    return v != nullptr && v->type == Type::Bool ? v->boolean : fallback;
}

bool parseJson(const std::string &input, JsonValue &out, std::string &error) {
    out = JsonValue{};
    return Parser(input).parse(out, error);
}

} // namespace util
