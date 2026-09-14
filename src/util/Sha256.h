#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

// Plain SHA-256 (FIPS 180-4), identical on every target so a backup made on
// the device can be checked with sha256sum anywhere.
namespace util {

class Sha256 {
public:
    Sha256();
    void update(const uint8_t *data, size_t size);
    std::array<uint8_t, 32> finish();

private:
    void block(const uint8_t *chunk);

    std::array<uint32_t, 8> state_;
    std::array<uint8_t, 64> buffer_{};
    size_t buffered_ = 0;
    uint64_t totalBytes_ = 0;
};

std::array<uint8_t, 32> sha256(const uint8_t *data, size_t size);
std::string toHex(const uint8_t *data, size_t size);

} // namespace util
