#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace crucible {

class Sha256 {
public:
    Sha256();

    void update(const void* data, std::size_t size);
    void update(std::string_view text);
    std::array<std::uint8_t, 32> digest();
    std::string hexDigest();

private:
    void transform(const std::uint8_t* block);
    void pad();

    std::array<std::uint32_t, 8> state_;
    std::array<std::uint8_t, 64> buffer_{};
    std::size_t bufferSize_ = 0;
    std::uint64_t totalBytes_ = 0;
    bool finalized_ = false;
};

std::string sha256Hex(std::string_view text);

} // namespace crucible
