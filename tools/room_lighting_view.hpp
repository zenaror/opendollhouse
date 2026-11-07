#pragma once
// FreeSO7e76 basic (non-AdvancedLighting) Blueprint/2DWorldBatch lighting.
#include "odh/room_lighting.hpp"
#include "odh/bitmap.hpp"
namespace odh_host {
struct RoomLightingViewSettings {
    float darkness=0.8f; // FinaleUtils normal, non-event profile.
    float minimum_outside_multiplier=175.0f/400.0f;
    std::array<float,3> minimum_color_multiplier{0.8f,0.8f,1};
    std::array<std::uint8_t,3> weather_color{255,255,255};
    float weather_strength=0;
};
struct RoomLightingDaylight {
    std::array<std::uint8_t,3> outside{},minimum{};
};
struct RoomLightingView {
    std::vector<std::array<float,3>> room_colors; // Linear RGB, one per geometric room.
};
RoomLightingDaylight room_lighting_daylight(unsigned hour,unsigned minute,unsigned second,
    const RoomLightingViewSettings& = {});
RoomLightingView make_room_lighting_view(const odh::RoomLightingSnapshot&,const odh::GeometricRoomMap&,
    unsigned hour,unsigned minute,unsigned second,const RoomLightingViewSettings& = {});
std::array<float,3> room_lighting_view_color(const RoomLightingView&,std::uint16_t room);
std::array<float,3> room_lighting_gamma_multiplier(const std::array<float,3>&);
std::uint16_t room_lighting_tile_room(const odh::GeometricRoomMap&,std::uint32_t x,std::uint32_t y,std::uint8_t level);
std::uint16_t room_lighting_wall_room(const odh::GeometricRoomMap&,std::uint32_t x,std::uint32_t y,
    std::uint8_t level,std::uint8_t orientation,std::uint8_t rotation);
}
