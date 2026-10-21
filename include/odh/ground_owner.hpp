#pragma once

#include <cstdint>
#include <optional>

namespace odh {

struct GroundCellOwner {
    std::uint32_t x = 0, y = 0;
    std::uint8_t level = 0;
};

// Zero means no painted ground. The compact token covers the supported
// 64x64 lot on both stories and fits the raster's uint16 owner channel.
constexpr std::uint16_t ground_owner_id(std::uint32_t x, std::uint32_t y,
                                        std::uint8_t level) noexcept {
    return x < 64 && y < 64 && level >= 1 && level <= 2
        ? static_cast<std::uint16_t>(1 + (level - 1U) * 4096U + y * 64U + x)
        : 0;
}

constexpr std::optional<GroundCellOwner> decode_ground_owner(
    std::uint16_t token) noexcept {
    if (!token || token > 8192) return std::nullopt;
    const auto value = static_cast<std::uint32_t>(token - 1U);
    return GroundCellOwner{value % 64U, (value / 64U) % 64U,
                           static_cast<std::uint8_t>(value / 4096U + 1U)};
}

} // namespace odh
