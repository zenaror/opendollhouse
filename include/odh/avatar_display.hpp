// SPDX-License-Identifier: MPL-2.0
// FreeSO7e AvatarComponent DrawAvatarMesh tint/alpha profile.
#pragma once
#include <array>
#include <cstdint>
namespace odh {
inline std::array<std::uint8_t,4> avatar_display_color(std::uint16_t flags,bool ghost){
    if(flags&1)return {21,168,63,168};if(ghost)return {255,255,255,64};return {255,255,255,255};
}
}
