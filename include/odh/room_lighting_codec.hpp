// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "odh/room_lighting.hpp"
namespace odh {
struct RoomLightingCodecLimits {
    std::size_t bytes=2U*1024U*1024U;
    RoomLightingLimits values{};
};
std::vector<std::uint8_t> encode_room_lighting_snapshot(const RoomLightingSnapshot&,
    const RoomLightingCodecLimits& = {});
RoomLightingSnapshot decode_room_lighting_snapshot(const std::vector<std::uint8_t>&,
    const RoomLightingCodecLimits& = {});
}
