// SPDX-License-Identifier: MPL-2.0
#include "odh/room_lighting_codec.hpp"
#include "odh/formats.hpp"
#include <iostream>
#include <limits>
#include <stdexcept>
namespace {
void require(bool v,const char* why){if(!v)throw std::runtime_error(why);}
template<class F>void rejects(F f,const char* why){try{f();}catch(const odh::FormatError&){return;}throw std::runtime_error(why);}
}
int main(){try{
    odh::RoomLightingSnapshot state;odh::RoomLighting room;room.base_room=1;
    room.ambient_light=40;room.outside_light=10;room.room_score=-42;room.bounds={0,0,4,4};
    odh::RoomLightSource light;light.room=1;light.x=8.5f;light.y=24.25f;light.weight=.375f;
    light.component=odh::ObjectPositionIdentity{1,9};light.window_room=2;light.bounds={-8,0,32,32};
    room.lights.push_back(light);room.components.push_back({1,9});room.object_footprints.push_back({0,0,16,16});
    state.by_base_room.emplace(1,room);state.visual_rooms.push_back({{1,9},65535});
    const auto bytes=odh::encode_room_lighting_snapshot(state);
    const auto restored=odh::decode_room_lighting_snapshot(bytes);
    require(odh::encode_room_lighting_snapshot(restored)==bytes,"all lighting fields and float bits round trip");
    require(restored.by_base_room.at(1).room_score==-42&&restored.visual_rooms.at(0).owner.generation==9,
        "signed score and visual generation survive");
    for(std::size_t n=0;n<bytes.size();++n){auto truncated=bytes;truncated.resize(n);
        rejects([&]{odh::decode_room_lighting_snapshot(truncated);},"every truncated prefix rejects");}
    auto invalid=bytes;invalid.push_back(0);rejects([&]{odh::decode_room_lighting_snapshot(invalid);},"trailing data rejected");
    invalid=bytes;invalid[0]=2;rejects([&]{odh::decode_room_lighting_snapshot(invalid);},"unknown version rejected");
    invalid=bytes;for(unsigned i=4;i<8;++i)invalid[i]=255;
    rejects([&]{odh::decode_room_lighting_snapshot(invalid);},"forged counts reject before allocation");
    odh::RoomLightingCodecLimits small;small.values.lights=0;
    rejects([&]{odh::decode_room_lighting_snapshot(bytes,small);},"aggregate light count respects smaller profile");
    small={};small.bytes=bytes.size()-1;
    rejects([&]{odh::encode_room_lighting_snapshot(state,small);},"writer respects smaller byte profile");
    rejects([&]{odh::decode_room_lighting_snapshot(bytes,small);},"reader respects smaller byte profile");
    auto bad=state;bad.by_base_room.at(1).lights[0].intensity=std::numeric_limits<float>::quiet_NaN();
    rejects([&]{odh::encode_room_lighting_snapshot(bad);},"nonfinite light cannot enter a save");
    bad=state;bad.visual_rooms.push_back(bad.visual_rooms.front());
    rejects([&]{odh::encode_room_lighting_snapshot(bad);},"duplicate visual identity rejected");
    std::cout<<"lighting codec matrix passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
