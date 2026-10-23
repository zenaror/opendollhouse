#pragma once

#include "odh/formats.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace odh {

class Sha256 {
public:
    Sha256();
    void update(const void* data, std::size_t size);
    std::array<std::uint8_t,32> finish();

private:
    void transform(const std::uint8_t* block);
    std::array<std::uint32_t,8> state_{};
    std::array<std::uint8_t,64> buffer_{};
    std::uint64_t total_bytes_ = 0;
    std::size_t buffered_bytes_ = 0;
    bool finished_ = false;
};

std::array<std::uint8_t,32> sha256(const void* data, std::size_t size);

} // namespace odh
