#include "odh/graphics_state.hpp"

#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>

namespace {
int checks=0;
void require(bool value,const char* message) {++checks;if(!value)throw std::runtime_error(message);}
template<class F> void rejects(F call,const char* message) {
    bool rejected=false;try{call();}catch(const odh::FormatError&){rejected=true;}
    require(rejected,message);
}
std::uint32_t bits(float n) {std::uint32_t result;std::memcpy(&result,&n,sizeof(result));return result;}
bool same(std::array<float,3> a,std::array<float,3> b) {
    for(std::size_t i=0;i<3;++i)if(!std::isfinite(a[i]) || std::abs(a[i]-b[i])>0.00001f)return false;
    return true;
}
odh::SlotTable slots() {
    odh::SlotRecord route{};route.type=3;route.offset_bits={bits(1600),bits(1600),bits(1600)};
    odh::SlotRecord first{};first.type=0;first.offset_bits={bits(16),bits(-32),0x7FC00000};first.height=7;
    odh::SlotRecord other{};other.type=1;
    odh::SlotRecord second{};second.type=0;second.offset_bits={bits(-48),bits(64),bits(800)};
    return {0,10,{route,first,other,second},0};
}
struct Fixture {
    odh::World world;odh::SlotTable table=slots();
    Fixture() {
        require(world.create(1,odh::EntityKind::avatar,{0,0,1}),"create avatar");
        require(world.create(2,odh::EntityKind::object,{0,0,2}),"create container");
        require(world.set_position(1,{8,8,1,6}),"position avatar");
        require(world.set_position(2,{168,328,2,0}),"position container");
    }
    odh::GraphicsSlotResolver resolver() {
        return [this](const odh::Entity& entity)->const odh::SlotTable* {
            require(entity.id==2 && entity.kind==odh::EntityKind::object,"resolver receives actual parent");
            return &table;
        };
    }
    void place(std::size_t slot=0) {require(world.place_in_slot(2,slot,1),"position avatar in storage slot");}
};
}
int main() {
    try {
        {Fixture f;const auto avatar=odh::graphics_entity(f.world,1);
         require(avatar.tile_position==std::array<float,3>{0.5f,0.5f,0},"avatar keeps exact direct sixteenth tile center");
         require(avatar.id==1 && avatar.kind==odh::EntityKind::avatar && avatar.direction==6 &&
                 avatar.generation==f.world.find(1)->generation && avatar.container==0,"free avatar identity and direction");
         const auto object=odh::graphics_entity(f.world,2);
         require(object.tile_position==std::array<float,3>{10,20,2.95f},"object subtracts exact half tile and uses 2.95 level height");
         require(f.world.set_position(2,{0,0,1,0}),"position object at zero");
         require(same(odh::graphics_entity(f.world,2).tile_position,{-0.5f,-0.5f,0}),"negative graphics anchor is retained");}
        for(std::uint8_t direction=0;direction<8;++direction) {
            Fixture f;require(f.world.set_position(1,{20,36,2,direction}),"set each avatar direction");
            require(f.world.set_position(2,{20,36,2,direction}),"set each object direction");
            require(odh::graphics_entity(f.world,1).direction==direction &&
                    odh::graphics_entity(f.world,2).direction==direction,"all eight direction indices retained for flag mapping");
        }
        const std::array<std::array<float,2>,8> rotated{{{1,-2},{1,-2},{2,1},{1,-2},{-1,2},{1,-2},{-2,-1},{1,-2}}};
        for(std::uint8_t direction=0;direction<8;++direction) {
            Fixture f;require(f.world.set_position(2,{168,328,2,direction}),"set each container direction");f.place();
            const auto result=odh::graphics_entity(f.world,1,f.resolver());
            require(same(result.tile_position,{10.5f+rotated[direction][0],20.5f+rotated[direction][1],2.95f}),
                    "SLOT XY uses cardinal rotation and diagonal north fallback; slot Z ignored");
            require(result.container==2 && result.direction==6 && result.generation==f.world.find(1)->generation,
                    "contained result preserves child identity and direction");
        }
        {Fixture f;f.place(1);require(same(odh::graphics_entity(f.world,1,f.resolver()).tile_position,{7.5f,24.5f,2.95f}),
                    "storage slot index is ordinal among type zero records");}
        {Fixture f;f.place();auto& child=*const_cast<odh::Entity*>(f.world.find(1));
         child.position=odh::EntityPosition{3200,6400,7,6};
         require(same(odh::graphics_entity(f.world,1,f.resolver()).tile_position,{11.5f,18.5f,2.95f}),
                    "copied or inconsistent child logical location is never added to container graphics");}
        {Fixture f;f.place();rejects([&]{odh::graphics_entity(f.world,1);},"positioned containment requires explicit resolver");
         rejects([&]{odh::graphics_entity(f.world,1,[](const auto&)->const odh::SlotTable*{return nullptr;});},"null table rejected");
         f.table.records.erase(f.table.records.begin()+1);
         require(same(odh::graphics_entity(f.world,1,f.resolver()).tile_position,{7.5f,24.5f,2.95f}),"ordinal follows remaining storage order");
         f.table.records.clear();rejects([&]{odh::graphics_entity(f.world,1,f.resolver());},"missing storage slot rejected");}
        {Fixture f;require(f.world.attach(2,0,1),"attach without placement");
         rejects([&]{odh::graphics_entity(f.world,1,f.resolver());},"unpositioned attachment rejected");}
        {Fixture f;f.world.create(3,odh::EntityKind::object);f.world.set_position(3,{8,8,1,0});f.world.place_in_slot(2,0,3);
         rejects([&]{odh::graphics_entity(f.world,3,f.resolver());},"contained object requires unsupported SLOT height profile");}
        {Fixture f;f.world.create(3,odh::EntityKind::object,{0,0,1});f.world.set_position(3,{8,8,1,0});
         f.world.place_in_slot(3,0,2);f.place();
         rejects([&]{odh::graphics_entity(f.world,1,f.resolver());},"nested container rejected");}
        {Fixture f;f.world.create(3,odh::EntityKind::avatar,{0,0,1});f.world.set_position(3,{8,8,1,0});f.world.place_in_slot(3,0,1);
         rejects([&]{odh::graphics_entity(f.world,1,f.resolver());},"avatar bone slot profile rejected");}
        {Fixture f;rejects([&]{odh::graphics_entity(f.world,0);},"missing ID rejected");
         f.world.create(3,odh::EntityKind::object);rejects([&]{odh::graphics_entity(f.world,3);},"missing position rejected");
         auto& entity=*const_cast<odh::Entity*>(f.world.find(1));
         entity.position->level=0;rejects([&]{odh::graphics_entity(f.world,1);},"nonpositive level rejected");
         entity.position->level=1;entity.position->direction=8;
         rejects([&]{odh::graphics_entity(f.world,1);},"out of range direction rejected");}
        for(const auto invalid:{0x7FC00000U,0x7F800000U,0xFF800000U}) {
            Fixture f;f.place();f.table.records[1].offset_bits[0]=invalid;
            rejects([&]{odh::graphics_entity(f.world,1,f.resolver());},"nonfinite SLOT XY rejected");
        }
        {Fixture f;f.place();f.table.version=11;rejects([&]{odh::graphics_entity(f.world,1,f.resolver());},"unsupported SLOT version rejected");}
        {Fixture f;f.place();auto& parent=*const_cast<odh::Entity*>(f.world.find(2));parent.slots[0]=0;
         rejects([&]{odh::graphics_entity(f.world,1,f.resolver());},"inconsistent relation ends rejected");}
        {Fixture f;f.place();auto& parent=*const_cast<odh::Entity*>(f.world.find(2));parent.position.reset();
         rejects([&]{odh::graphics_entity(f.world,1,f.resolver());},"container missing position rejected");}
        {Fixture f;f.place();auto& parent=*const_cast<odh::Entity*>(f.world.find(2));parent.position->direction=8;
         rejects([&]{odh::graphics_entity(f.world,1,f.resolver());},"invalid container direction rejected");}
        {Fixture f;const auto old=odh::graphics_entity(f.world,1);f.world.remove(1);f.world.create(1,odh::EntityKind::avatar);
         f.world.set_position(1,{8,8,1,0});const auto replacement=odh::graphics_entity(f.world,1);
         require(replacement.id==old.id && replacement.generation!=old.generation,"snapshot distinguishes reused entity ID");}
        {Fixture f;f.place();rejects([&]{odh::graphics_entity(f.world,1,[&](const auto&)->const odh::SlotTable* {
             f.world.remove(1);f.world.create(1,odh::EntityKind::avatar);f.world.set_position(1,{8,8,1,6});f.world.place_in_slot(2,0,1);
             return &f.table;
         });},"resolver cannot replace actor with reused ID");}
        {Fixture f;f.place();rejects([&]{odh::graphics_entity(f.world,1,[&](const auto&)->const odh::SlotTable* {
             f.world.remove(2);f.world.create(2,odh::EntityKind::object,{0,0,2});f.world.set_position(2,{168,328,2,0});f.world.place_in_slot(2,0,1);
             return &f.table;
         });},"resolver cannot replace container with reused ID");}
        {Fixture f;f.place();rejects([&]{odh::graphics_entity(f.world,1,[&](const auto&)->const odh::SlotTable* {
             f.world.set_position(2,{184,328,2,0});return &f.table;
         });},"resolver cannot change copied parent position");}
        std::cout<<checks<<" graphics state checks passed (constructed reference profile only)\n";return 0;
    }catch(const std::exception& e){std::cerr<<"Graphics state test failed: "<<e.what()<<'\n';return 1;}
}
