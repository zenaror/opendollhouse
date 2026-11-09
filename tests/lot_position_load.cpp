// SPDX-License-Identifier: MPL-2.0
#include "lot_position_load.hpp"
#include "odh/formats.hpp"
#include <iostream>
#include <stdexcept>
using namespace odh;using namespace odh_host;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
struct Fixture {
    World world;PreparedArchitecture prepared;bool authorized=true,admitted=true;
    std::vector<std::string> events;LotPositionLoadContext ctx;
    explicit Fixture(bool wall_actions=true){
        prepared.width=prepared.height=4;prepared.buildable.assign(16,1);
        for(auto& level:prepared.levels){level.floors.assign(16,0);level.walls.resize(16);}
        prepared.levels[0].walls[5].segments=wall_top_right;
        for(std::int16_t id:{1,2,3}){
            require(world.create(id,EntityKind::object,{0,0,2})&&world.define_type(id,0x1000+id),"create source entity");
            for(std::int16_t index:{1,5,13,29,40,59,63})require(world.declare_object_word(id,index,0),"source object word");
            std::vector<EntryPoint> ep(10);if(id==1&&wall_actions){ep[6].action=600;ep[8].action=800;}ep[9].action=900;
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
    {
        Fixture matrix(false);matrix.ctx.execute_entry={};
        require(matrix.world.find(1)->entry_points->at(6).action==0&&matrix.world.find(1)->entry_points->at(8).action==0,
            "wall matrix absent EP6/EP8 is authored at immutable source installation");
        auto source=replay_imported_lot_positions(matrix.ctx);require(source.state.has_value(),"wall matrix initial actual state");
        auto& owner=*source.state;auto c=bind_lot_position_wall_consumers(matrix.ctx,owner);
        const ObjectPositionIdentity id{1,matrix.world.find(1)->generation};
        for(std::uint8_t direction=0;direction<8;++direction)for(std::uint16_t flag:{1,2,4,8,15,128,129,256}){
            require(matrix.world.set_position(1,{24,24,1,direction})&&matrix.world.declare_object_word(1,13,static_cast<std::int16_t>(flag)),"wall matrix actual words/heading");
            owner.occupied_walls[0][5]=0;owner.occupied_walls[0][10]=8;
            require(c.wall_use(id,true,true),"source wall set for every mask/direction");
            const auto applied=owner.occupied_walls[0][5];
            if(direction==0&&flag==1)require(applied==2,"source north required-front sets concrete TR bit");
            if(direction==2&&flag==1)require(applied==4,"source east required-front sets concrete BR bit");
            if(flag==15)require(applied==0,"preserve pinned modulo15 behavior rather than intuitively replacing it with bitand");
            require(c.wall_use(id,false,true)&&owner.occupied_walls[0][5]==0&&owner.occupied_walls[0][10]==8,"source clear only owns actual target tile/mask");
            owner.occupied_walls[0][5]=9;require(c.wall_use(id,true,false)&&owner.occupied_walls[0][5]==9,"nonexclusive source still executes but does not claim walls");
        }
        require(matrix.world.set_position(1,{24,24,1,0})&&matrix.world.declare_object_word(1,13,1),"phase style actual door pose");
        require(c.architectural_wall_style_change(id,false),"source Pre clears actual required door style");
        require(owner.wall_appearance.faces.size()==1&&is_canonical_cleared_wall_face(owner.wall_appearance.faces.front())&&
            !owner.wall_appearance.faces.front().door&&owner.wall_appearance.faces.front().door_owner==0,"cleared door retains explicit style0 without stale ownership");
        require(c.architectural_wall_style_change(id,true)&&owner.wall_appearance.faces.front().style.raw_style==22&&
            owner.wall_appearance.faces.front().door_owner==1,"source Post reinstates exact dynamic sprite-backed door identity");
        auto invalid=owner;invalid.occupied_walls[0][0]=16;bool rejected=false;
        try{(void)bind_lot_position_wall_consumers(matrix.ctx,invalid);}catch(const FormatError&){rejected=true;}
        require(rejected,"invalid occupancy is not an empty-wall fallback");
        Fixture dead;auto dead_source=replay_imported_lot_positions(dead.ctx);
        require(dead_source.state.has_value(),"dead cleanup uses its own actually initialized EP6-present source");
        auto& dead_owner=*dead_source.state;const ObjectPositionIdentity dead_id{1,dead.world.find(1)->generation};
        require(dead.world.find(1)->entry_points->at(6).action==600&&dead.world.declare_object_word(1,13,1)&&
            dead.world.mark_dead({dead_id.id,dead_id.generation}),"actual immutable source EP6 and World.Dead before deletion cleanup");
        unsigned dead_calls=0;auto dead_context=dead.ctx;
        dead_context.execute_entry=[&](auto,auto,auto,const auto&){++dead_calls;return true;};
        auto dead_consumer=bind_lot_position_wall_consumers(dead_context,dead_owner);dead_owner.occupied_walls[0][5]=2;
        require(dead_consumer.wall_use(dead_id,false,true)&&dead_calls==0&&dead_owner.occupied_walls[0][5]==0,
            "dead source cleanup clears walls without executing a dead object Thread");
        matrix.authorized=false;require(!c.wall_use(id,true,true)&&!c.architectural_wall_style_change(id,false),"expired source blocks both real wall event consumers");
    }
    {
        // One matrix crosses the old historical cap through both repeated
        // movement and distinct retired bodies. Codec/rebind points are cold
        // architecture recovery, with no fresh-load constructor or EP replay.
        Fixture repeated(false);auto loaded=replay_imported_lot_positions(repeated.ctx);
        require(loaded.state.has_value(),"bounded diagnostics actual imported state");
        auto& owner=*loaded.state;const auto support=owner.object_support.levels;
        const auto occupied=owner.occupied_walls;
        owner.wall_appearance.applications.assign(4096,WallAppearanceApplication{1,1,0});
        owner.wall_appearance.applications.front()={2,0,6};
        const auto legacy=encode_architecture_object_state(owner);
        owner=decode_architecture_object_state(legacy);
        require(encode_architecture_object_state(owner)==legacy&&owner.wall_appearance.applications.size()==4096,
            "legacy full repeated diagnostic history cold roundtrips without migration");
        auto c=bind_lot_position_wall_consumers(repeated.ctx,owner);
        const ObjectPositionIdentity id{1,repeated.world.find(1)->generation};
        auto unavailable=repeated.ctx;unavailable.resolved_wall_style=[](auto)->std::optional<WallAppearanceStyle>{return {};};
        const auto missing=bind_lot_position_wall_consumers(unavailable,owner);
        require(!missing.architectural_wall_style(id)&&encode_architecture_object_state(owner)==legacy,
            "failed provider does not compact or publish full legacy history");
        repeated.authorized=false;
        require(!c.architectural_wall_style_change(id,false)&&encode_architecture_object_state(owner)==legacy,
            "expired source does not compact full legacy history");
        repeated.authorized=true;
        for(unsigned iteration=0;iteration<5000;++iteration){
            require(c.architectural_wall_style_change(id,false),"repeated Pre at old pose stays available beyond old cap");
            const auto old=*repeated.world.find(1)->position;
            bool cleared=false;for(const auto& face:owner.wall_appearance.faces)
                if(face.x==std::uint32_t(old.x/16)&&face.y==std::uint32_t(old.y/16)&&face.segment==wall_top_right)
                    cleared=is_canonical_cleared_wall_face(face)&&!face.door&&!face.door_owner;
            require(cleared,"Pre retains explicit clear without retired style or door owner");
            const std::int32_t coordinate=(iteration&1U)?24:40;
            require(repeated.world.set_position(1,{coordinate,coordinate,1,0})&&c.architectural_wall_style_change(id,true),
                "repeated moved Post restores exact live wall style");
            require(owner.wall_appearance.applications.size()==2&&owner.wall_appearance.applications.back().id==1&&
                owner.wall_appearance.applications.back().applied_sides==1&&owner.wall_appearance.applications.back().ignored_sides==0,
                "diagnostics keep latest event per existing body, not historical event count");
            require(owner.object_support.levels==support&&owner.occupied_walls==occupied,
                "diagnostic compaction cannot alter occupancy/support ledgers");
            if(iteration%1000==999){const auto bytes=encode_architecture_object_state(owner);
                owner=decode_architecture_object_state(bytes);
                require(encode_architecture_object_state(owner)==bytes,"repeated cold checkpoint is byte exact");
                c=bind_lot_position_wall_consumers(repeated.ctx,owner);
            }
        }
        require(c.architectural_wall_style_change(id,false)&&repeated.world.remove(1),"retire original body after actual Pre clear");
        for(std::int16_t fresh=4;fresh<4206;++fresh){
            require(repeated.world.create(fresh,EntityKind::object,{0,0,2})&&repeated.world.define_type(fresh,0x1000+fresh)&&
                repeated.world.declare_object_word(fresh,40,std::int16_t(-32768))&&repeated.world.declare_object_word(fresh,13,1)&&
                repeated.world.set_position(fresh,{24,24,1,0}),"create distinct ordinary architectural body");
            const ObjectPositionIdentity body{fresh,repeated.world.find(fresh)->generation};
            require(c.architectural_wall_style_change(body,true)&&c.architectural_wall_style_change(body,false)&&
                owner.wall_appearance.applications.size()==2&&owner.wall_appearance.applications.back().id==fresh&&
                repeated.world.remove(fresh),"retired IDs do not exhaust current cohort diagnostics");
        }
        require(repeated.world.create(1,EntityKind::object,{0,0,2})&&repeated.world.define_type(1,0x1001)&&
            repeated.world.declare_object_word(1,40,std::int16_t(-32768))&&repeated.world.declare_object_word(1,13,2)&&
            repeated.world.set_position(1,{24,24,1,0}),"reuse original numeric ID with new actual identity");
        const ObjectPositionIdentity reused{1,repeated.world.find(1)->generation};
        require(reused.generation!=id.generation&&!c.architectural_wall_style(id)&&c.architectural_wall_style(reused)&&
            owner.wall_appearance.applications.back().applied_sides==0&&owner.wall_appearance.applications.back().ignored_sides==2,
            "new identity replaces latest masks while stale generation still rejects");
        const auto valid=encode_architecture_object_state(owner);
        for(unsigned bad=0;bad<4;++bad){
            owner=decode_architecture_object_state(valid);
            if(bad==0)owner.wall_appearance.applications.resize(4097,WallAppearanceApplication{1,1,0});
            if(bad==1)owner.wall_appearance.applications.front().id=0;
            if(bad==2)owner.wall_appearance.applications.front().applied_sides=2;
            if(bad==3)owner.wall_appearance.applications.front().ignored_sides=1;
            const auto count=owner.wall_appearance.applications.size();
            const auto first=owner.wall_appearance.applications.front();
            require(!c.architectural_wall_style(reused)&&owner.wall_appearance.applications.size()==count&&
                owner.wall_appearance.applications.front().id==first.id&&owner.wall_appearance.applications.front().applied_sides==first.applied_sides&&
                owner.wall_appearance.applications.front().ignored_sides==first.ignored_sides,
                "live compaction refuses invalid original records instead of laundering them");
            bool rejected=false;try{(void)encode_architecture_object_state(owner);}catch(const FormatError&){rejected=true;}
            require(rejected,"wire count/identity/mask guards remain enforced");
        }
    }
    {
        Fixture full(false);auto loaded=replay_imported_lot_positions(full.ctx);
        require(loaded.state.has_value(),"full current cohort actual initialized state");
        auto& owner=*loaded.state;owner.wall_appearance.applications.clear();
        for(std::int16_t numeric=1;numeric<=4096;++numeric){
            if(numeric>3)require(full.world.create(numeric,EntityKind::object,{0,0,2}),"fill actual bounded world cohort");
            owner.wall_appearance.applications.push_back({numeric,1,0});
        }
        auto c=bind_lot_position_wall_consumers(full.ctx,owner);
        require(c.architectural_wall_style({1,full.world.find(1)->generation})&&owner.wall_appearance.applications.size()==4096&&
            owner.wall_appearance.applications.back().id==1,
            "4096 current diagnostics can replace an existing record without weakening memory cap");
        (void)encode_architecture_object_state(owner);
    }
    for(bool provider_succeeds:{false,true}){
        Fixture nested(false);auto loaded=replay_imported_lot_positions(nested.ctx);
        require(loaded.state.has_value(),"nested diagnostic matrix actual initialized state");
        auto& owner=*loaded.state;owner.wall_appearance.applications.assign(4096,WallAppearanceApplication{1,1,0});
        require(nested.world.declare_object_word(2,40,0x4000)&&nested.world.declare_object_word(2,13,8),
            "nested provider actual window placement");
        auto inner=bind_lot_position_wall_consumers(nested.ctx,owner);auto context=nested.ctx;
        const auto resolve=context.resolved_wall_style;std::vector<std::uint8_t> inner_bytes;
        context.resolved_wall_style=[&](auto id)->std::optional<WallAppearanceStyle>{
            require(inner.architectural_wall_style({2,nested.world.find(2)->generation}),"provider executes legitimate nested style lifecycle");
            inner_bytes=encode_architecture_object_state(owner);
            return provider_succeeds?resolve(id):std::nullopt;
        };
        auto outer=bind_lot_position_wall_consumers(context,owner);
        require(outer.architectural_wall_style({1,nested.world.find(1)->generation})==provider_succeeds,
            "outer provider outcome remains authoritative after nested writes");
        if(provider_succeeds)require(owner.wall_appearance.applications.size()==2&&owner.wall_appearance.applications[0].id==2&&
            owner.wall_appearance.applications[1].id==1&&owner.wall_appearance.applications[0].applied_sides==8,
            "outer compaction refreshes nested diagnostics and keeps last application order");
        else require(encode_architecture_object_state(owner)==inner_bytes,"outer failure preserves nested successful effects without stale publication");
    }
    for(unsigned mutation=0;mutation<5;++mutation){
        Fixture provider;auto source=replay_imported_lot_positions(provider.ctx);require(source.state.has_value(),"provider mutation actual initialized source");
        auto& state=*source.state;const ObjectPositionIdentity target{1,provider.world.find(1)->generation};
        require(provider.world.declare_object_word(1,13,1),"provider mutation original placement flag");
        const auto before=encode_architecture_object_state(state);
        auto context=provider.ctx;context.resolved_wall_style=[&](auto)->std::optional<WallAppearanceStyle>{
            if(mutation==0)require(provider.world.remove(1),"provider removes actual body");
            if(mutation==1)require(provider.world.set_position(1,{8,8,1,0}),"provider moves actual body");
            if(mutation==2)require(provider.world.place_individual_on_floor(1,{},0),"provider publishes actual OOW");
            if(mutation==3)require(provider.world.declare_object_word(1,13,2),"provider changes actual placement bank");
            if(mutation==4)require(provider.world.remove(1)&&provider.world.create(1,EntityKind::object,{0,0,2})&&
                provider.world.define_type(1,0x1001)&&provider.world.set_position(1,{24,24,1,0})&&
                provider.world.find(1)->generation!=target.generation,"provider recycles actual numeric ID at same pose/GUID");
            return WallAppearanceStyle{22,0x1001,{{100,101,102,103,104,105}}};
        };
        const auto guarded=bind_lot_position_wall_consumers(context,state);
        require(!guarded.architectural_wall_style_change(target,true)&&encode_architecture_object_state(state)==before,
            "reentrant removal/movement/OOW/placement/recycled identity preserves provider effects but refuses stale wall publication");
    }
    std::cout<<"imported lot source position replay passed\n";
}
