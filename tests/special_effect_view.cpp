#include "../tools/special_effect_view.hpp"
#include "../tools/lot_camera.hpp"
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {void require(bool value,const char* why){if(!value)throw std::runtime_error(why);}}
int main(){try{
    using Kind=odh_host::SpecialEffectVisualKind;
    odh_host::SpecialEffectRoomState room;room.kind=Kind::avatar;
    require(odh_host::special_effect_component_room(room)==std::optional<std::uint16_t>{65532},"avatar OOW getter produces65532, not the skipped65531 sentinel");
    room.positioned=true;room.object_room=-1;
    require(odh_host::special_effect_component_room(room)==std::optional<std::uint16_t>{0},"avatar positioned stored Room plus1");
    room.object_room=32767;
    require(odh_host::special_effect_component_room(room)==std::optional<std::uint16_t>{32768},"component room conversion preserves unsigned value");
    room.kind=Kind::object;room.positioned=false;room.object_room=4;room.flag_field2=0;room.disabled=0;
    require(odh_host::special_effect_component_room(room)==std::optional<std::uint16_t>{5},"ordinary object OOW still uses raw Room backing");
    room.flag_field2=0x400;room.lighting=20;
    require(odh_host::special_effect_component_room(room)==std::optional<std::uint16_t>{65535},"active light uses renderer light-room override");
    for(const auto architecture:{0x4000,0x8000}){
        room.flag_field2=static_cast<std::int16_t>(0x400|architecture);
        require(odh_host::special_effect_component_room(room)==std::optional<std::uint16_t>{5},"architectural window and door suppress light-room override");
    }
    room.disabled=4;room.object_room.reset();room.lighting.reset();
    require(odh_host::special_effect_component_room(room)==std::optional<std::uint16_t>{65533},"disabled component overrides room/light storage after flags are known");
    room.disabled.reset();
    require(!odh_host::special_effect_component_room(room),"unknown disabled policy is not assumed active");
    room.disabled=0;room.flag_field2=0x400;
    require(!odh_host::special_effect_component_room(room),"unknown required light contribution is explicit unsupported");
    odh_host::SpecialEffectView view;odh::WorldProjection camera;
    const odh::WorldEntityReference target{17,42};
    std::string reason;
    require(view.center_to(target,{target,0,0,{}},&reason)&&!view.focused(),"outside room no-op precedes visual backend access");
    require(view.center_to(target,{target,65531,0,{}},&reason)&&!view.focused(),"source excluded room sentinel no-op");
    require(!view.center_to(target,{target,65532,0,{}},&reason)&&!view.focused(),"avatar OOW component room is not skipped when visual pelvis is unavailable");
    require(!view.center_to(target,{{17,43},2,1,{{1,2,3}}},&reason),"reused numeric ID cannot supply old visual identity");
    require(!view.center_to(target,{target,2,1,{}},&reason)&&!reason.empty(),"missing animated pelvis/backend is explicit unsupported");
    require(!view.center_to(target,{target,2,1,{{1,std::numeric_limits<float>::infinity(),3}}},&reason),"nonfinite visual fact rejected");
    require(view.center_to(target,{target,2,2,{{10.25f,7.5f,3.2f}}},&reason)&&view.focused(),"actual visual center accepted without logical tile rounding");
    require(view.apply(camera)==std::optional<std::uint8_t>{2}&&camera.center_tile==std::array<float,3>{10.25f,7.5f,3.2f},"instant visual focus changes camera and view level");
    camera.center_tile={90,80,70};
    require(!view.apply(camera)&&camera.center_tile==std::array<float,3>{90,80,70},"manual camera movement is not replaced by target tracking");
    require(view.center_to(target,{target,0,0,{}},&reason)&&view.focused()&&!view.apply(camera),"later no-op leaves prior camera intent intact");
    require(view.center_to(target,{target,2,1,{{4,5,0.8f}}},&reason)&&view.apply(camera)==std::optional<std::uint8_t>{1}&&camera.center_tile[0]==4,"later source focus supersedes prior instant center");
    // Scroll commands change only presentation. At every zoom and rotation
    // a feature previously centered moves left when the player pans right.
    for(std::uint8_t rotation=0;rotation<4;++rotation)for(std::uint8_t zoom=1;zoom<=3;++zoom){
        camera.rotation=rotation;camera.zoom=zoom;camera.center_tile={20,20,3};
        const auto feature=camera.center_tile;
        require(odh_host::scroll_lot_camera(camera,64,64,1,0),"pan right within lot");
        const auto screen=odh::project_tile(camera,feature);
        require(screen[0]<camera.width/2&&std::abs(screen[1]-camera.height/2)<.001f&&
                camera.center_tile[2]==3,"pan obeys screen axes, rotation and floor height");
        auto split=camera,single=camera;
        (void)odh_host::scroll_lot_camera(split,64,64,0,.25f);
        (void)odh_host::scroll_lot_camera(split,64,64,0,.25f);
        (void)odh_host::scroll_lot_camera(single,64,64,0,.5f);
        require(std::abs(split.center_tile[0]-single.center_tile[0])<.0001f&&
                std::abs(split.center_tile[1]-single.center_tile[1])<.0001f,"scroll speed is independent of polling frequency");
    }
    camera.rotation=0;camera.center_tile={0,0,2};
    require(!odh_host::scroll_lot_camera(camera,32,32,0,-100)&&camera.center_tile[0]==0,"lot bound");
    require(odh_host::lot_camera_edge(1,10,320,200)==std::array<int,2>{-1,-1}&&
            odh_host::lot_camera_edge(150,90,320,200)==std::array<int,2>{}&&
            odh_host::lot_camera_edge(50,210,320,200)==std::array<int,2>{},"edge corner, interior and outside/HUD");
    require(view.center_to(target,{target,2,1,{{4,5,.8f}}},&reason)&&view.apply(camera).has_value()&&
            camera.center_tile[0]==4,"scripted center still wins after manual scroll");
    require(odh_host::scroll_lot_camera(camera,32,32,1,0)&&!view.apply(camera),"next manual movement keeps its own center");
    std::cout<<"special_effect_view: passed\n";return 0;
}catch(const std::exception& error){std::cerr<<"special_effect_view: "<<error.what()<<'\n';return 1;}}
