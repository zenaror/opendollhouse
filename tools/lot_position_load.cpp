// SPDX-License-Identifier: MPL-2.0
// Pinned FreeSO VM.Load/VMGameObject.PositionChange/VMEntity.SetWallUse,
// revision 7e76c1960252ddd033d8937d5edb47d326f5b69c.
#include "lot_position_load.hpp"
#include "odh/formats.hpp"
#include <map>
#include <tuple>
#include <memory>

namespace odh_host {
namespace {
using namespace odh;
using FaceKey=std::tuple<std::uint8_t,std::uint32_t,std::uint32_t,std::uint8_t>;
void need(bool value,const char* message){if(!value)throw FormatError(message);}
struct Replay {
    LotPositionLoadContext ctx;
    LotPositionLoadState& state;
    std::map<FaceKey,WallAppearanceFace> faces;
    Replay(LotPositionLoadContext context,LotPositionLoadState& live):ctx(std::move(context)),state(live){read_faces();}
    void read_faces(){
        faces.clear();
        for(const auto& face:state.wall_appearance.faces)
            need(faces.emplace(FaceKey{face.level,face.y,face.x,face.segment},face).second,"live wall appearance contains duplicate face authority");
    }
    void publish_faces(){
        state.wall_appearance.faces.clear();state.wall_appearance.faces.reserve(faces.size());
        for(const auto& item:faces)state.wall_appearance.faces.push_back(item.second);
    }
    bool current()const{return ctx.source_still_matches&&ctx.source_still_matches();}
    const Entity* entity(ObjectPositionIdentity id)const{
        if(!current())return nullptr;
        const auto* e=ctx.world->find(id.id);return e&&e->generation==id.generation?e:nullptr;
    }
    std::optional<std::int16_t> word(ObjectPositionIdentity id,std::int16_t index)const{
        const auto* e=entity(id);if(!e)return {};
        const auto found=e->object_words.find(index);return found==e->object_words.end()?std::nullopt:std::optional<std::int16_t>{found->second};
    }
    std::optional<std::size_t> tile(ObjectPositionIdentity id)const{
        const auto* e=entity(id);if(!e||!e->position)return {};
        const auto& p=*e->position;
        if(p.x<0||p.y<0||p.level<1||p.level>2||p.direction>7||
           p.x/16>=std::int32_t(ctx.prepared->width)||p.y/16>=std::int32_t(ctx.prepared->height))return {};
        return std::size_t(p.y/16)*ctx.prepared->width+std::size_t(p.x/16);
    }
    bool cohort_matches()const{
        if(!current())return false;std::size_t at=0;
        for(std::int16_t cursor=0;;){const auto next=ctx.world->next_entity(cursor);if(!next)break;cursor=*next;
            if(at==ctx.source_order.size()||ctx.source_order[at].id!=cursor||!entity(ctx.source_order[at]))return false;++at;}
        return at==ctx.source_order.size();
    }
    std::optional<bool> admit(ObjectPositionIdentity id)const{
        const auto* e=entity(id);if(!e||!e->definition||!e->entry_points||e->entry_points->size()<=9||!word(id,29))return {};
        if(e->position&&!tile(id))return false;
        if(e->kind==EntityKind::object){
            for(const std::int16_t field:{13,40,59,63})if(!word(id,field))return {};
            if(e->position&&!e->attachment&&!e->ui_ghost){
                if((std::uint16_t(*word(id,40))&0xc000U)&&(!ctx.resolved_wall_style||!ctx.resolved_wall_style(id)))return false;
                if(e->entry_points->at(6).action&&!ctx.execute_entry)return false;
                if(e->entry_points->at(8).action&&!ctx.dynamic_multitile)return false;
            }
        }else if(e->kind!=EntityKind::avatar)return false;
        return ctx.admit_loaded_entity(id);
    }
    bool architectural(ObjectPositionIdentity id){
        if(state.wall_appearance.applications.size()>=4096)return false;
        const auto* e=entity(id);const auto index=tile(id);const auto flags=word(id,40),placement=word(id,13);
        if(!e||!index||!flags||!placement||!e->definition||!ctx.resolved_wall_style)return false;
        const auto style=ctx.resolved_wall_style(id);if(!style||!entity(id)||state.wall_appearance.applications.size()>=4096)return false;
        // A provider callback can enter the same live lifecycle. Refresh from
        // its published authority rather than overwrite it with a stale cache.
        read_faces();
        // Preserve the provider/resource identity of sprite-backed styles.
        if(style->resource_guid&&style->resource_guid!=e->definition->guid)return false;
        const auto pos=*e->position;const bool door=(std::uint16_t(*flags)&0x8000U)!=0;
        const unsigned quarter=(pos.direction&1U)?0U:unsigned(pos.direction)/2U;
        WallAppearanceApplication application{id.id};
        for(unsigned relative=0;relative<4;++relative){if(!(std::uint16_t(*placement)&(1U<<relative)))continue;
            const auto side=(quarter+relative)%4U;
            if(side==1||side==2){application.ignored_sides|=std::uint8_t(1U<<side);continue;}
            const auto segment=std::uint8_t(side==0?wall_top_right:wall_top_left);
            const auto x=std::uint32_t(pos.x/16),y=std::uint32_t(pos.y/16);const auto level=std::uint8_t(pos.level);
            const FaceKey key{level,y,x,segment};const auto previous=faces.find(key);const bool exists=previous!=faces.end();
            const bool door_state=door?style->raw_style!=0:exists&&previous->second.door;
            const auto door_owner=door?id.id:exists?previous->second.door_owner:std::int16_t{0};
            const auto door_generation=door?id.generation:exists?previous->second.door_generation:std::uint64_t{0};
            const auto present=(ctx.prepared->levels[std::size_t(level-1)].walls[*index].segments&segment)!=0;
            WallAppearanceFace face{x,y,level,segment,*style,id.id,door_owner,id.generation,door_generation,door_state,present};
            if(exists){++state.wall_appearance.overwritten_faces;previous->second=face;}else faces.emplace(key,face);
            application.applied_sides|=std::uint8_t(1U<<side);
        }
        state.wall_appearance.applications.push_back(application);publish_faces();return current();
    }
    bool wall_use(ObjectPositionIdentity id,bool set,bool exclusive){
        const auto* e=entity(id);const auto index=tile(id);const auto placement=word(id,13);
        if(!e||!index||!placement||!e->entry_points||e->entry_points->size()<=6)return false;
        const auto pos=*e->position;const auto quarter=(pos.direction&1U)?0U:unsigned(pos.direction)/2U;
        const auto rotation=(8U-(quarter+1U))%4U;
        const auto rotate=[](std::uint32_t value,unsigned by){return ((value<<(4U-by))&15U)|((value&15U)>>by);};
        // Source caches placement bits BEFORE actual EP6 can change them.
        const auto used=std::uint8_t(rotate(std::uint16_t(*placement)%15U,rotation));
        if(e->entry_points->at(6).action){
            if(!ctx.execute_entry)return false;
            const auto segments=ctx.prepared->levels[std::size_t(pos.level-1)].walls[*index].segments;
            const auto main=rotate(segments,(4U-rotation)%4U);std::uint16_t adjacency=0;
            if(main&1U)adjacency|=4U;if(main&2U)adjacency|=2U;if(main&4U)adjacency|=8U;if(main&8U)adjacency|=1U;
            if(!ctx.world->declare_object_word(id.id,5,static_cast<std::int16_t>(adjacency))||
               !ctx.execute_entry(id,6,id.id,{{0,0,0,0}})||!entity(id))return false;
            // A script that relocates itself requires the full nested live
            // position lifecycle. Never publish this old-tile overlay as if it
            // represented that unhandled transition.
            const auto* after=entity(id);if(!after->position||!(*after->position==pos))return false;
        }
        if(used&&exclusive){auto& mask=state.occupied_walls[std::size_t(pos.level-1)][*index];
            if(set)mask|=used;else mask&=std::uint8_t(~used);}
        return current();
    }
};
}
LotPositionWallConsumers bind_lot_position_wall_consumers(LotPositionLoadContext context,LotPositionLoadState& state){
    need(context.world&&context.prepared&&context.source_still_matches&&context.source_still_matches(),
        "live wall consumers lack actual architecture/source authority");
    const auto& prepared=*context.prepared;const auto cells=std::size_t(prepared.width)*prepared.height;
    need(prepared.width&&prepared.height,"live wall consumers lack a nonempty architecture grid");
    for(std::size_t level=0;level<2;++level)
        need(prepared.levels[level].walls.size()==cells&&state.occupied_walls[level].size()==cells,
            "live wall consumer grid extent mismatch");
    auto replay=std::make_shared<Replay>(std::move(context),state);
    return {[replay](auto id,bool set,bool exclusive){return replay->wall_use(id,set,exclusive);},
        [replay](auto id){return replay->architectural(id);}};
}
LotPositionLoadResult replay_imported_lot_positions(LotPositionLoadContext context){
    LotPositionLoadResult result;
    try{
        need(context.world&&context.prepared&&context.source_still_matches&&context.admit_loaded_entity&&
             context.room_at&&context.register_position&&context.unregister_position,"lot load lacks actual lifecycle consumers");
        need(context.source_order.size()<=4096,"lot load entity budget exceeded");
        LotPositionLoadState fresh_state;
        auto replay=std::make_shared<Replay>(std::move(context),fresh_state);const auto& p=*replay->ctx.prepared;
        replay->state.object_support=make_architecture_object_support(p.width,p.height);
        const auto cells=std::size_t(p.width)*p.height;
        for(std::size_t level=0;level<2;++level){
            need(p.levels[level].walls.size()==cells,"lot load wall grid extent mismatch");
            replay->state.occupied_walls[level].assign(cells,0);
        }
        need(replay->cohort_matches(),"lot load sequence differs from complete source cohort");
        // No lifecycle effects until every imported entity is source-admitted.
        for(const auto id:replay->ctx.source_order){const auto yes=replay->admit(id);
            need(yes&&*yes,"lot load entity has an unsupported lifecycle consumer");}
        ObjectPositionChangeServices services;
        services.admit_loaded_entity=[replay](auto id){return replay->admit(id);};
        services.room_at=replay->ctx.room_at;services.unregister_position=replay->ctx.unregister_position;
        services.register_position=replay->ctx.register_position;
        services.architectural_wall_style=[replay](auto id){return replay->architectural(id);};
        services.wall_use=[replay](auto id,bool set,bool exclusive){return replay->wall_use(id,set,exclusive);};
        services.object_supported=[replay](auto id,const auto& pos,bool set){return set_architecture_object_supported(
            replay->state.object_support,*replay->ctx.world,id,pos,set,replay->ctx.source_still_matches);};
        services.dynamic_multitile=[replay](auto id){return replay->ctx.dynamic_multitile&&replay->ctx.dynamic_multitile(id)&&replay->current();};
        ObjectPositionValid unused_placement(*replay->ctx.world,{});
        ObjectPositionChange load(*replay->ctx.world,unused_placement,std::move(services));
        for(const auto id:replay->ctx.source_order){
            need(replay->cohort_matches(),"lot load source cohort changed during replay");
            const auto* e=replay->entity(id);need(e!=nullptr,"lot load source entity disappeared");
            if(e->attachment)continue; // Source rechecks live Container, not a frozen root list.
            result.effects_started=true;
            need(load.loaded_position_change(id,replay->ctx.source_still_matches),"lot load position lifecycle failed");
            ++result.roots_replayed;
        }
        need(replay->cohort_matches(),"lot load source cohort changed after replay");
        result.state=std::move(replay->state);
    }catch(const std::exception& error){result.diagnostic=error.what();}
    return result;
}
}
