// SPDX-License-Identifier: MIT
//
// Desktop tool that changes a raw NVS partition dump through ESP-IDF's own
// nvs_flash (the same sources as the native UI build). It makes test fixtures:
// a test namespace inside a real dump, or a dump filled up to LOW, CRITICAL or
// FULL. NVS Manager itself never creates namespaces; this tool is not part of
// the firmware.
//
//   nvs-image <in.bin> <out.bin> [command...]
//
//   set <namespace> <key> <u8|i8|u16|i16|u32|i32|u64|i64|str> <value>
//   blob <namespace> <key> <hex bytes, e.g. 00a1ff>
//   erase-key <namespace> <key>
//   fill <namespace> <key-prefix> <max-count> <string-length>
//        adds str keys (u8 keys for length 0) until max-count or NVS is full

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <idf_host_shim.h>
#include <nvs.h>
#include <nvs_flash.h>

namespace {

constexpr const char *kLabel = "nvs";

int usage() {
    fprintf(stderr, "usage: nvs-image <in.bin> <out.bin> [set NS KEY TYPE VALUE | blob NS KEY HEX |\n"
                    "                                      erase-key NS KEY | fill NS PREFIX COUNT LENGTH]...\n");
    return 2;
}

bool check(const char *what, esp_err_t err) {
    if (err == ESP_OK) return true;
    fprintf(stderr, "%s: %s (0x%x)\n", what, esp_err_to_name(err), static_cast<unsigned>(err));
    return false;
}

class Handle {
public:
    explicit Handle(const char *ns) { err_ = nvs_open_from_partition(kLabel, ns, NVS_READWRITE, &handle_); }
    ~Handle() {
        if (err_ == ESP_OK) nvs_close(handle_);
    }
    esp_err_t error() const { return err_; }
    nvs_handle_t get() const { return handle_; }

private:
    nvs_handle_t handle_ = 0;
    esp_err_t err_ = ESP_FAIL;
};

bool parseSigned(const char *s, long long lo, long long hi, long long &out) {
    char *end = nullptr;
    errno = 0;
    out = std::strtoll(s, &end, 0);
    return errno == 0 && end != s && *end == '\0' && out >= lo && out <= hi;
}

bool parseUnsigned(const char *s, unsigned long long hi, unsigned long long &out) {
    if (*s == '-') return false;
    char *end = nullptr;
    errno = 0;
    out = std::strtoull(s, &end, 0);
    return errno == 0 && end != s && *end == '\0' && out <= hi;
}

// Returns ESP_ERR_INVALID_ARG for an unknown type or a value out of range.
esp_err_t setTyped(nvs_handle_t h, const char *key, const std::string &type, const char *value) {
    long long s = 0;
    unsigned long long u = 0;
    if (type == "u8") return parseUnsigned(value, 0xFF, u) ? nvs_set_u8(h, key, static_cast<uint8_t>(u)) : ESP_ERR_INVALID_ARG;
    if (type == "u16") return parseUnsigned(value, 0xFFFF, u) ? nvs_set_u16(h, key, static_cast<uint16_t>(u)) : ESP_ERR_INVALID_ARG;
    if (type == "u32") return parseUnsigned(value, 0xFFFFFFFFull, u) ? nvs_set_u32(h, key, static_cast<uint32_t>(u)) : ESP_ERR_INVALID_ARG;
    if (type == "u64") return parseUnsigned(value, ~0ull, u) ? nvs_set_u64(h, key, u) : ESP_ERR_INVALID_ARG;
    if (type == "i8") return parseSigned(value, -128, 127, s) ? nvs_set_i8(h, key, static_cast<int8_t>(s)) : ESP_ERR_INVALID_ARG;
    if (type == "i16") return parseSigned(value, -32768, 32767, s) ? nvs_set_i16(h, key, static_cast<int16_t>(s)) : ESP_ERR_INVALID_ARG;
    if (type == "i32") {
        return parseSigned(value, -2147483648LL, 2147483647LL, s) ? nvs_set_i32(h, key, static_cast<int32_t>(s))
                                                                 : ESP_ERR_INVALID_ARG;
    }
    if (type == "i64") {
        return parseSigned(value, (-9223372036854775807LL - 1), 9223372036854775807LL, s) ? nvs_set_i64(h, key, s)
                                                                                          : ESP_ERR_INVALID_ARG;
    }
    if (type == "str") return nvs_set_str(h, key, value);
    return ESP_ERR_INVALID_ARG;
}

bool parseHex(const char *hex, std::vector<uint8_t> &out) {
    const size_t n = std::strlen(hex);
    if (n % 2 != 0) return false;
    for (size_t i = 0; i < n; i += 2) {
        char byte[3] = {hex[i], hex[i + 1], '\0'};
        char *end = nullptr;
        const long v = std::strtol(byte, &end, 16);
        if (*end != '\0') return false;
        out.push_back(static_cast<uint8_t>(v));
    }
    return true;
}

void printStats(const char *when) {
    nvs_stats_t s{};
    if (!check("nvs_get_stats", nvs_get_stats(kLabel, &s))) return;
    printf("%s used=%zu free=%zu available=%zu total=%zu namespaces=%zu\n", when, s.used_entries, s.free_entries,
           s.available_entries, s.total_entries, s.namespace_count);
}

bool commit(Handle &h, const char *what) { return check(what, nvs_commit(h.get())); }

} // namespace

int main(int argc, char **argv) {
    if (argc < 3) return usage();

    std::ifstream in(argv[1], std::ios::binary);
    if (!in) {
        fprintf(stderr, "cannot read %s\n", argv[1]);
        return 1;
    }
    std::vector<uint8_t> image((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (!check("register image", idf_host_register_nvs_image(kLabel, image.data(), image.size()))) return 1;
    if (!check("nvs_flash_init_partition", nvs_flash_init_partition(kLabel))) return 1;
    printStats("before:");

    for (int i = 3; i < argc;) {
        const std::string cmd = argv[i];
        if (cmd == "set" && i + 4 < argc) {
            Handle h(argv[i + 1]);
            if (!check("open", h.error())) return 1;
            if (!check("set", setTyped(h.get(), argv[i + 2], argv[i + 3], argv[i + 4])) || !commit(h, "commit")) return 1;
            printf("set %s/%s (%s)\n", argv[i + 1], argv[i + 2], argv[i + 3]);
            i += 5;
        } else if (cmd == "blob" && i + 3 < argc) {
            std::vector<uint8_t> bytes;
            if (!parseHex(argv[i + 3], bytes)) return usage();
            Handle h(argv[i + 1]);
            if (!check("open", h.error())) return 1;
            if (!check("set blob", nvs_set_blob(h.get(), argv[i + 2], bytes.data(), bytes.size())) || !commit(h, "commit")) {
                return 1;
            }
            printf("blob %s/%s (%zu B)\n", argv[i + 1], argv[i + 2], bytes.size());
            i += 4;
        } else if (cmd == "erase-key" && i + 2 < argc) {
            Handle h(argv[i + 1]);
            if (!check("open", h.error())) return 1;
            if (!check("erase", nvs_erase_key(h.get(), argv[i + 2])) || !commit(h, "commit")) return 1;
            printf("erased %s/%s\n", argv[i + 1], argv[i + 2]);
            i += 3;
        } else if (cmd == "fill" && i + 4 < argc) {
            const std::string prefix = argv[i + 2];
            const long count = std::strtol(argv[i + 3], nullptr, 10);
            const long length = std::strtol(argv[i + 4], nullptr, 10);
            if (prefix.size() > 10 || count <= 0 || length < 0) return usage();
            Handle h(argv[i + 1]);
            if (!check("open", h.error())) return 1;
            const std::string value(static_cast<size_t>(length), 'x');
            long written = 0;
            esp_err_t err = ESP_OK;
            for (; written < count; ++written) {
                const std::string key = prefix + std::to_string(written);
                err = length == 0 ? nvs_set_u8(h.get(), key.c_str(), 1) : nvs_set_str(h.get(), key.c_str(), value.c_str());
                if (err != ESP_OK) break;
            }
            if (!commit(h, "commit")) return 1;
            printf("fill %s/%s*: %ld keys written%s%s\n", argv[i + 1], prefix.c_str(), written,
                   err == ESP_OK ? "" : ", stopped: ", err == ESP_OK ? "" : esp_err_to_name(err));
            if (err != ESP_OK && err != ESP_ERR_NVS_NOT_ENOUGH_SPACE) return 1;
            i += 5;
        } else {
            return usage();
        }
    }

    printStats("after: ");
    const uint8_t *data = nullptr;
    size_t size = 0;
    if (!check("read image", idf_host_get_image(kLabel, &data, &size))) return 1;
    std::ofstream out(argv[2], std::ios::binary);
    out.write(reinterpret_cast<const char *>(data), static_cast<std::streamsize>(size));
    if (!out) {
        fprintf(stderr, "cannot write %s\n", argv[2]);
        return 1;
    }
    printf("wrote %s (%zu B)\n", argv[2], size);
    return 0;
}
