#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace odh {
inline constexpr std::size_t ts1_temp_count = 20;
inline constexpr std::size_t ts1_temp_xl_count = 2;
using Ts1Temps = std::array<std::int16_t, ts1_temp_count>;
using Ts1TempXL = std::array<std::int32_t, ts1_temp_xl_count>;
}
