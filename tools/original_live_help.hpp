// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <array>
#include <cstdint>
#include <optional>
#include <string_view>
namespace odh_host {
// Live-panel layout belongs to the offered help kind, even when its authored
// illustration bank is empty (Social). Resource requests retain prior support.
inline bool original_live_panel_help(std::string_view context,bool portrait,
        const std::array<std::uint32_t,2>& objects){
    return context=="needs"||context=="personality"||portrait||objects[0]||objects[1];
}
struct OriginalNeedsHelpSelection {
    std::uint16_t table;
    unsigned title,description;
    std::array<std::uint32_t,2> objects;
};
// The shared HUD orders needs by column. Live130/180 and the original
// object-pair table alternate columns: Hunger,Energy,Comfort,Fun,... .
inline std::optional<OriginalNeedsHelpSelection> original_needs_help(unsigned need,bool pet){
    constexpr unsigned order[8]={0,2,4,6,1,3,5,7};
    constexpr std::array<std::array<std::uint32_t,2>,8> human{{
        {0xb94755df,0xff9f18e0},{0x82e04c5b,0xbf137195},
        {0xd97681db,0x7cb11019},{0x5cdc712f,0x481a74ec},
        {0x85e00942,0x85e02a40},{0,0},{0x84e0774c,0},{0x7f907075,0xbc3ba088}}};
    constexpr std::array<std::array<std::uint32_t,2>,8> animal{{
        {0x8eec19f7,0xffc0ea58},{0xbc537c52,0x96cb67ae},
        {0xe23f9020,0x2b4502a2},{0x914e5a74,0xb26ab256},
        {0x2ecdb068,0},{0,0},{0x39ccf441,0xbedd7b26},{0x7f907075,0xbc3ba088}}};
    if(need>=8)return {};
    const auto slot=order[need];
    return OriginalNeedsHelpSelection{std::uint16_t(pet?180:130),1+2*slot,2+2*slot,(pet?animal:human)[slot]};
}
struct OriginalPersonalityHelpSelection {
    std::uint16_t table;
    unsigned name,title,description,bucket;
};
// Original cWinPersonality divides raw hundredths before choosing the
// descriptions. Preserve the boundaries (three and eight), including truncation.
inline std::optional<OriginalPersonalityHelpSelection> original_personality_help(
        unsigned trait,std::int16_t raw,bool pet){
    if(trait>=5)return {};
    const int points=int(raw)/100;
    const unsigned bucket=points<3?0:points<8?1:2;
    const unsigned base=1+7*trait;
    return OriginalPersonalityHelpSelection{std::uint16_t(pet?170:131),base,base+1+bucket,base+4+bucket,bucket};
}
}
