// SPDX-License-Identifier: MPL-2.0
#include "lot_position_load.hpp"
#include <iostream>
#include <stdexcept>
using namespace odh;using namespace odh_host;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
struct Fixture {
    World world;PreparedArchitecture prepared;bool authorized=true,admitted=true;
    std::vector<std::string> events;LotPositionLoadContext ctx;
    Fixture(){
        prepared.width=prepared.height=4;prepared.buildable.assign(16,1);
        for(auto& level:prepared.levels){level.floors.assign(16,0);level.walls.resize(16);}
        prepared.levels[0].walls[5].segments=wall_top_right;
        for(std::int16_t id:{1,2,3}){
            require(world.create(id,EntityKind::object,{0,0,2})&&world.define_type(id,0x1000+id),"create source entity");
            for(std::int16_t index:{1,5,13,29,40,59,63})require(world.declare_object_word(id,index,0),"source object word");
            std::vector<EntryPoint> ep(10);if(id==1){ep[6].action=600;ep[8].action=800;}ep[9].action=900;
            require(world.define_entry_points(id,ep)&&world.set_position(id,{std::int32_t(8+id*16),std::int32_t(8+id*16),1,0}),"source position and entry points");
        }
        require(world.attach(2,0,3)&&world.refresh_contained_position(3),"source actual child");
        require(world.declare_object_word(1,40,std::int16_t(-32768))&&world.declare_object_word(1,13,1)&&world.declare_object_word(1,63,2),"source architectural door");
        ctx.world=&world;ctx.prepared=&prepared;
        for(std::int16_t id:{1,2,3})ctx.source_order.push_back({id,world.find(id)->generation});
        ctx.source_still_matches=[&]{return authorized;};
        ctx.admit_loaded_entity=[&](auto)->std::optional<bool>{return admitted;};
        ctx.room_at=[](const EntityPosition&)->std::optional<std::uint16_t>{return 2;};
        ctx.unregister_position=[&](auto id,bool change){require(change,"source import room change");events.push_back("u"+std::to_string(id.id));return true;};
        ctx.register_position=[&](auto id,std::uint16_t room,bool change){require(room==2&&change&&world.find(id.id)->object_words.at(29)==1,"source registration after real Room29 update");events.push_back("r"+std::to_string(id.id));return true;};
        ctx.execute_entry=[&](auto id,std::uint16_t ep,std::int16_t stack,const auto& args){
            require(ep==6&&id.id==1&&stack==1&&args==std::array<std::int16_t,4>{{0,0,0,0}},"actual EP6 source context, never base EP9");
            require(world.find(1)->object_words.at(5)==4,"source wall adjacency remap");events.push_back("ep6");
            return world.declare_object_word(1,59,8)&&world.declare_object_word(1,13,0);
        };
        ctx.resolved_wall_style=[](auto id)->std::optional<WallAppearanceStyle>{return WallAppearanceStyle{22,std::uint32_t(0x1000+id.id),{{100,101,102,103,104,105}}};};
        ctx.dynamic_multitile=[&](auto id){events.push_back("ep8");return world.declare_object_word(id.id,5,17);};
    }
};
}
int main(){
    Fixture f;auto result=replay_imported_lot_positions(f.ctx);
    require(result.state.has_value(),result.diagnostic.c_str());
    require(result.effects_started&&result.roots_replayed==2,"actual root source order excludes contained child as root");
    require(f.events==std::vector<std::string>{"r1","ep6","ep8","u3","r3","r2"},"source root and recursive children preserve lifecycle order, no EP9");
    const auto& state=*result.state;require(state.object_support.levels[0][5]==1&&state.object_support.levels[0][10]==0,"EP6 category mutation reaches actual support assignment");
    require(state.occupied_walls[0][5]==wall_top_right,"exclusive occupancy uses placement cached before EP6 changes");
    require(state.wall_appearance.faces.size()==1&&state.wall_appearance.faces[0].segment==wall_top_right&&state.wall_appearance.faces[0].door&&
        state.wall_appearance.faces[0].style.resource_guid==0x1001&&state.wall_appearance.faces[0].style.sprites[5]==105,"architectural load retains concrete dynamic resource identity and door state");
    // Recovery binds the already-published state, without rerunning fresh
    // position constructors or clearing any other live architecture ledger.
    auto& live=*result.state;const auto applications=live.wall_appearance.applications.size();
    const auto overwritten=live.wall_appearance.overwritten_faces;const auto support=live.object_support.levels;
    live.occupied_walls[0][10]=8;
    auto live_ctx=f.ctx;unsigned actual_ep6=0;const auto original_ep6=live_ctx.execute_entry;
    live_ctx.execute_entry=[&](auto id,auto ep,auto stack,const auto& args){
        require(&live==&*result.state&&live.wall_appearance.applications.size()==applications,
            "recovery EP6 sees the same existing ledger, without constructor replay");
        ++actual_ep6;return original_ep6(id,ep,stack,args);
    };
    const auto consumers=bind_lot_position_wall_consumers(std::move(live_ctx),live);
    require(f.world.declare_object_word(1,13,1)&&consumers.wall_use({1,f.world.find(1)->generation},false,true),
        "live recovery clears actual exclusive wall use through source EP6");
    require(actual_ep6==1&&live.occupied_walls[0][5]==0&&live.occupied_walls[0][10]==8&&
        live.object_support.levels==support&&live.wall_appearance.applications.size()==applications,
        "live wall use mutates only actual occupancy without resetting support/style history");
    require(f.world.set_position(2,{24,24,1,0})&&f.world.declare_object_word(2,40,0x4000)&&
        f.world.declare_object_word(2,13,1),"prepare actual live window write");
    require(consumers.architectural_wall_style({2,f.world.find(2)->generation}),"write architectural style into same live state");
    require(live.wall_appearance.faces.size()==1&&live.wall_appearance.faces[0].owner==2&&
        live.wall_appearance.faces[0].door_owner==1&&live.wall_appearance.faces[0].door&&
        live.wall_appearance.overwritten_faces==overwritten+1&&live.wall_appearance.applications.size()==applications+1&&
        live.occupied_walls[0][10]==8&&live.object_support.levels==support,
        "live style publishes faces/overwrite/application immediately and retains prior door/other ledgers");
    f.authorized=false;require(!consumers.wall_use({1,f.world.find(1)->generation},true,true)&&
        !consumers.architectural_wall_style({2,f.world.find(2)->generation}),"live consumers reject expired source authority");
    Fixture overlay;require(overlay.world.set_position(2,{24,24,1,0})&&overlay.world.refresh_contained_position(3)&&
        overlay.world.declare_object_word(2,40,0x4000)&&overlay.world.declare_object_word(2,13,1),"overlapping architectural source window");
    const auto resolve=overlay.ctx.resolved_wall_style;overlay.ctx.resolved_wall_style=[resolve](auto id)->std::optional<WallAppearanceStyle>{
        if(id.id==2)return WallAppearanceStyle{0,0,{}};return resolve(id);};
    const auto overlap=replay_imported_lot_positions(overlay.ctx);
    require(overlap.state&&overlap.state->wall_appearance.faces.size()==1&&overlap.state->wall_appearance.faces[0].owner==2&&
        overlap.state->wall_appearance.faces[0].style.raw_style==0&&overlap.state->wall_appearance.faces[0].door&&
        overlap.state->wall_appearance.faces[0].door_owner==1,"later window clear replaces style while retaining prior door state and owner");
    Fixture moved;moved.ctx.execute_entry=[&](auto,std::uint16_t,std::int16_t,const auto&){return moved.world.set_position(1,{8,8,1,0});};
    const auto relocation=replay_imported_lot_positions(moved.ctx);require(!relocation.state&&relocation.effects_started,"EP6 relocation without nested lifecycle cannot certify stale wall overlay");
    Fixture none;none.admitted=false;auto no=replay_imported_lot_positions(none.ctx);require(!no.state&&!no.effects_started&&none.events.empty(),"unsupported complete cohort rejects before any lifecycle effects");
    Fixture missing;missing.ctx.execute_entry={};no=replay_imported_lot_positions(missing.ctx);require(!no.state&&!no.effects_started&&missing.events.empty(),"missing known EP6 consumer rejects before effects");
    Fixture omitted;omitted.ctx.source_order.pop_back();no=replay_imported_lot_positions(omitted.ctx);require(!no.state&&!no.effects_started,"partial cohort cannot certify an empty support region");
    Fixture reversed;std::swap(reversed.ctx.source_order[0],reversed.ctx.source_order[1]);no=replay_imported_lot_positions(reversed.ctx);require(!no.state&&!no.effects_started,"wrong source ordering rejected");
    Fixture failed;failed.ctx.dynamic_multitile=[](auto){return false;};no=replay_imported_lot_positions(failed.ctx);require(!no.state&&no.effects_started&&no.roots_replayed==0,"post-event failure requires candidate discard, not fallback into a partial live session");
    Fixture changed;changed.ctx.execute_entry=[&](auto,std::uint16_t,std::int16_t,const auto&){changed.authorized=false;return true;};no=replay_imported_lot_positions(changed.ctx);require(!no.state&&no.effects_started,"expired capability during script cannot publish initialized support");
    std::cout<<"imported lot source position replay passed\n";
}
