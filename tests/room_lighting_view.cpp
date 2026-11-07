#include "room_lighting_view.hpp"
#include <cmath>
#include <iostream>
#include <limits>
namespace {
int checks=0;
void require(bool ok,const char* why){++checks;if(!ok)throw std::runtime_error(why);}
template<class F>void rejects(F fn){bool rejected=false;try{fn();}catch(const odh::FormatError&){rejected=true;}require(rejected,"invalid lighting view accepted");}
odh::GeometricRoomMap map(){
    odh::GeometricRoomMap m;m.width=m.height=2;m.floor_resources_resolved=m.light_base_rooms_resolved=true;
    m.rooms.resize(3);for(unsigned i=0;i<3;++i){m.rooms[i].id=static_cast<std::uint16_t>(i);m.rooms[i].light_base_room=static_cast<std::uint16_t>(i);m.rooms[i].area=4;}
    m.rooms[2].level=1;m.levels[0].assign(4,1U|(1U<<16));m.levels[1].assign(4,2U|(2U<<16));return m;
}
}
int main(){try{
    using namespace odh_host;
    const auto noon=room_lighting_daylight(12,0,0),night=room_lighting_daylight(0,0,0);
    require(noon.outside==std::array<std::uint8_t,3>{255,255,255},"source noon is white");
    require(noon.minimum==std::array<std::uint8_t,3>{32,32,40},"Blueprint source minimum quantization");
    require(night.outside[0]<night.outside[1]&&night.outside[1]<night.outside[2]&&night.outside!=noon.outside,
        "night uses source blue exterior rather than fixed brightness");
    for(unsigned hour=0;hour<24;++hour)for(unsigned minute:{0U,29U,59U}){
        const auto day=room_lighting_daylight(hour,minute,59);
        for(auto c:day.minimum)require(c>0,"source minimum prevents completely black channels");
    }
    auto m=map();odh::RoomLightingSnapshot state;
    odh::RoomLighting first;first.base_room=1;first.outside_light=100;
    odh::RoomLighting second;second.base_room=2;second.ambient_light=50;
    state.by_base_room={{1,first},{2,second}};
    const auto view=make_room_lighting_view(state,m,12,0,0);
    require(room_lighting_view_color(view,1)==std::array<float,3>{1,1,1},"outside room full daytime");
    require(room_lighting_view_color(view,2)==std::array<float,3>{159/255.0f,159/255.0f,167/255.0f},
        "separate interior room uses source ambient plus per-channel minimum");
    require(room_lighting_view_color(view,65535)==std::array<float,3>{1,1,1},"emitter room is white");
    require(room_lighting_view_color(view,32770)==std::array<float,3>{0,0,0},"source diagonal cast selects known unused black Color slot");
    require(room_lighting_tile_room(m,0,0,1)==1&&room_lighting_tile_room(m,0,0,2)==2,"floor room uses actual story map");
    // WallComponent explicit diagonal half selection for every rotation.
    m.levels[0][0]=1U|(2U<<16)|0x80000000U;
    for(std::uint8_t r=0;r<4;++r){
        require(room_lighting_wall_room(m,0,0,1,2,r)==std::array<unsigned,4>{2,1,1,2}[r],"orientation2 source room half");
        // orientation3 source casts ushort without the orientation2 mask.
        require(room_lighting_wall_room(m,0,0,1,3,r)==std::array<unsigned,4>{1,1,32770,32770}[r],"orientation3 source room cast");
    }
    require(room_lighting_gamma_multiplier({0.25f,0.5f,1})[0]>0.25f,"gamma conversion is not raw RGB multiplication");
    rejects([&]{make_room_lighting_view(odh::RoomLightingSnapshot{},map(),12,0,0);});
    rejects([&]{room_lighting_view_color(view,100);});
    rejects([&]{room_lighting_daylight(24,0,0);});
    rejects([&]{room_lighting_daylight(0,60,0);});
    auto invalid=RoomLightingViewSettings{};invalid.darkness=std::numeric_limits<float>::quiet_NaN();
    rejects([&]{room_lighting_daylight(12,0,0,invalid);});
    std::cout<<checks<<" basic source lighting view checks passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
