// SPDX-License-Identifier: MPL-2.0
// FreeSO VMContext.CheckWallValid/CheckFloorValid and VMTS1GlobalLinkStub.
#include "architecture_edit_session_adapter.hpp"
#include "linux_session_archive_store.hpp"
#include "odh/floor_placement.hpp"
#include "odh/installed_lot_limits.hpp"
#include "odh/sha256.hpp"
#include <cstring>
#include <algorithm>
#include <memory>
#include <stdexcept>

namespace odh_host {
using namespace odh;
namespace {
using namespace odh;
std::optional<std::int16_t> word(const Entity&e,std::int16_t i){auto it=e.object_words.find(i);return it==e.object_words.end()?std::nullopt:std::optional<std::int16_t>{it->second};}
void need(bool ok,const char*why){if(!ok){throw std::runtime_error(why);}}
struct Validation {
    ArchitectureEditValidationContext context;
    std::optional<std::vector<std::uint8_t>> supported;
    std::vector<std::vector<const Entity*>> tiles;
    explicit Validation(ArchitectureEditValidationContext c):context(std::move(c)){
        need(context.world&&context.prepared&&context.terrain&&context.rooms&&context.definitions,"architecture validation context is incomplete");
        const auto&p=*context.prepared;need(p.width&&p.height&&p.width<=64&&p.height<=64,"architecture validation extent invalid");
        need(context.rooms->width==p.width&&context.rooms->height==p.height,"architecture validation room extent mismatch");
        need(context.terrain->width==p.width&&context.terrain->height==p.height,"architecture terrain must be the same prepared/cropped extent");
        tiles.resize(std::size_t(p.width)*p.height*2);
        for(const auto&e:context.world->entities){
            // Source RegisterObjectPos does not spatially register contained
            // children. UI-only Destination ghosts are not source geometry.
            if(!e.position||e.attachment||e.ui_ghost){continue;}const auto&pos=*e.position;
            if(pos.x<0||pos.y<0||pos.level<1||pos.level>2||pos.x/16>=std::int32_t(p.width)||pos.y/16>=std::int32_t(p.height))continue;
            tiles[(pos.level-1)*p.width*p.height+(pos.y/16)*p.width+pos.x/16].push_back(&e);
        }
        if(context.object_support)supported=regenerate_architecture_support(p,*context.rooms,*context.object_support);
    }
    const std::vector<const Entity*>&at(int x,int y,int l)const{return tiles[(l-1)*context.prepared->width*context.prepared->height+y*context.prepared->width+x];}
    std::optional<bool> sloped(int x,int y)const{
        const auto&r=*context.terrain;
        if(!r.altitude||r.altitude->size()!=std::size_t(r.width)*r.height||x<0||y<0||x>=int(r.width)||y>=int(r.height))return {};
        const auto h=[&](int xx,int yy){return (*r.altitude)[std::size_t(yy)*r.width+xx][1];};
        const auto base=h(x,y);return base!=h((x+1)%r.width,y)||base!=h(x,(y+1)%r.height)||base!=h((x+1)%r.width,(y+1)%r.height);
    }
};
}
ArchitectureEditServices make_architecture_edit_services(ArchitectureEditValidationContext c){
    auto state=std::make_shared<Validation>(std::move(c));ArchitectureEditServices s;
    s.material=state->context.material;s.rectangle_clip=state->context.rectangle_clip;
    s.in_clip=[state](int x,int y,std::int8_t l)->std::optional<bool>{
        const auto&p=*state->context.prepared;if(x<0||y<0||l<1||l>2||x>=int(p.width)||y>=int(p.height))return false;
        const auto at=std::size_t(y)*p.width+x;if(p.buildable.size()!=std::size_t(p.width)*p.height)return {};return p.buildable[at]!=0;
    };
    s.wall_valid=[state](const PreparedArchitecture&,int x,int y,std::int8_t l,const SavedWallTile&w)->std::optional<bool>{
        if(w.segments){auto slope=state->sloped(x,y);if(!slope){return {};}if(*slope){return false;}}
        for(const auto*e:state->at(x,y,l)){
            const auto flags=word(*e,13),direction=word(*e,1);if(!flags||!direction||*direction<0||*direction>7){return {};}
            const auto f=std::uint16_t(*flags);const bool diagonal=(w.segments&48)!=0;
            if((diagonal&&!(f&128))||(!diagonal&&(f&64)))return false;
            const auto quarter=(*direction&1)?0U:unsigned(*direction)/2U;
            const auto rotate=(quarter+1)%4;const auto sides=((std::uint32_t(w.segments)<<(4-rotate))&15U)|((w.segments&15U)>>rotate);
            if((f&sides)!=(f&15)||(f&(sides<<8)))return false;
            // Source CheckWallValid passes checkUnused=false, so this path
            // must not require or invent a live OccupiedWalls history.
        }return true;
    };
    s.floor_valid=[state](const PreparedArchitecture&,int x,int y,std::int8_t l,std::uint16_t floor)->std::optional<bool>{
        for(const auto*e:state->at(x,y,l)){
            const auto flags=word(*e,42);if(!flags){return {};}const auto f=std::uint16_t(*flags);
            std::uint16_t offset=0;
            const bool needs_offset=(floor==0&&l>1&&!(f&512))||(floor>0&&floor<65534&&e->kind==EntityKind::object&&!(f&1));
            if(needs_offset){if(!e->definition){return {};}const auto*def=state->context.definitions->unique(e->definition->guid);if(!def||!def->definition.level_offset){return {};}offset=*def->definition.level_offset;}
            if(floor_change_valid(floor,l,f,e->kind==EntityKind::object,offset)!=FloorPlacementResult::allowed)return false;
        }return true;
    };
    s.supported=[state](int x,int y,std::int8_t below)->std::optional<bool>{
        if(below!=1||!state->supported){return {};}return (*state->supported)[std::size_t(y)*state->context.prepared->width+x]!=0;
    };
    s.below_outside=[state](int x,int y,std::int8_t below)->std::optional<bool>{
        const auto&m=*state->context.rooms;if(below!=1||m.levels[0].size()!=std::size_t(m.width)*m.height)return {};
        const auto room=std::uint16_t(m.levels[0][std::size_t(y)*m.width+x]);if(room>=m.rooms.size()||m.rooms[room].id!=room)return {};return m.rooms[room].outside;
    };return s;
}
ArchitectureEditSessionAdapter::ArchitectureEditSessionAdapter(FamilySessionJournal&j,const odh::ArchitectureResourceCatalog&c,ArchitectureEditValidationContext v,ArchitectureEditSessionHooks h):journal_(j),catalog_(c),context_(std::move(v)),hooks_(std::move(h)){}
ArchitectureEditSessionResult ArchitectureEditSessionAdapter::execute(const std::vector<odh::ArchitectureEditCommand>&commands){
    ArchitectureEditSessionResult r;if(commands.empty()){r.state=ArchitectureEditSessionState::cancelled;return r;}
    if(executing_){r.diagnostic="architecture session command reentry";return r;}
    struct Guard{bool&v;explicit Guard(bool&f):v(f){v=true;}~Guard(){v=false;}}guard(executing_);bool published=false;
    try{
        need(bool(hooks_.capture_current)&&bool(hooks_.validate_candidate)&&bool(hooks_.validate_reopen),"architecture session lacks actual capture/geometry/reopen consumer");
        need(context_.prepared,"architecture session lacks current prepared grids");journal_.verify_current();
        auto source=hooks_.capture_current();
        need(!source.build_buy_permission||source.build_buy_permission->enabled,"Build/Buy is disabled by the current lot simulation");
        const auto before=odh::encode_session_bundle(source);
        need(source.host.family_budget.has_value(),"architecture command active family budget is unknown");
        // A captured ARCT is authoritative. A same-sized constructor context may
        // predate an earlier edit; never let it silently undo that checkpoint.
        auto current=source.architecture?odh::resolve_architecture_snapshot(*source.architecture,catalog_):*context_.prepared;
        auto current_rooms=odh::generate_prepared_room_map(current);
        auto context=context_;context.world=&source.world;context.prepared=&current;context.rooms=&current_rooms;
        context.object_support.reset(); // Captured absence overrides any stale constructor history.
        const auto* captured_support=source.architecture_object_state?&source.architecture_object_state->object_support:
            source.architecture_object_support?&*source.architecture_object_support:nullptr;
        if(captured_support){
            const auto& support=*captured_support;
            validate_architecture_object_support(support);
            need(support.width==current.width&&support.height==current.height,"captured support extent differs from architecture");
            context.object_support=support.levels[0];
        }
        auto services=make_architecture_edit_services(std::move(context));
        auto edit=odh::prepare_architecture_edit(current,commands,services);r.receipts=edit.receipts;r.cost=edit.cost;
        if(!edit.candidate||!edit.rooms){r.diagnostic=edit.diagnostic;return r;}
        // TS1GlobalLink simulates the WHOLE batch, then transacts its net cost.
        // Preserve unchecked signed32 addition and source negative rejection.
        need(edit.cost!=INT32_MIN,"architecture source Math.Abs(cost) overflow");
        const auto budget_bits=std::uint32_t(*source.host.family_budget)-std::uint32_t(edit.cost);std::int32_t budget;std::memcpy(&budget,&budget_bits,4);
        if(budget<0){r.state=ArchitectureEditSessionState::rejected;r.diagnostic="architecture batch exceeds family budget";return r;}
        source.host.family_budget=budget;source.architecture=odh::bind_architecture_snapshot(*edit.candidate,catalog_);
        if(std::any_of(edit.receipts.begin(),edit.receipts.end(),[](const auto&receipt){return receipt.geometry_written;})){
            const auto event=[](auto&runtime){
                if(runtime.route_stack)odh::invalidate_session_room_routes(*runtime.route_stack);
                else if(runtime.route)runtime.route->room_route_invalid=true;
            };
            for(auto&runtime:source.runtime_entities)event(runtime);
            for(auto&npc:source.npc_avatars)event(npc.runtime);
        }
        if(source.architecture_object_state){
            auto& objects=*source.architecture_object_state;
            for(auto& face:objects.wall_appearance.faces){
                const auto at=std::size_t(face.y)*edit.candidate->width+face.x;
                face.segment_present=(edit.candidate->levels.at(face.level-1).walls.at(at).segments&face.segment)!=0;
            }
            if(objects.position_registry){
                need(bool(context_.registry_services),"architecture edit lacks actual room registry providers");
                const auto prior=*objects.position_registry;
                World candidate(odh::installed_lot_world_limits());need(candidate.restore_snapshot(source.world),"architecture registry World restore failed");
                auto registry_services=context_.registry_services(candidate,prior);
                ObjectPositionRegistry fresh(candidate,*edit.candidate,*edit.rooms,registry_services);
                auto seed=fresh.snapshot();seed.motion=prior.motion;seed.tiles=prior.tiles;seed.retired_owners=prior.retired_owners;
                seed.next_footprint_instance=prior.next_footprint_instance;
                for(auto& binding:seed.footprints){const auto old=std::find_if(prior.footprints.begin(),prior.footprints.end(),[&](const auto& item){return item.owner.id==binding.owner.id&&item.owner.generation==binding.owner.generation;});
                    if(old!=prior.footprints.end())binding=*old;
                    if(binding.present){const auto* e=candidate.find(binding.owner.id);need(e,"architecture footprint owner identity missing");
                        binding.room=e->position?geometric_room_at_runtime(*edit.rooms,e->position->x,e->position->y,e->position->level):std::optional<std::uint16_t>{0};
                        need(binding.room.has_value(),"architecture footprint room unavailable");
                    }
                }
                ObjectPositionRegistry rebuilt(candidate,*edit.candidate,*edit.rooms,std::move(registry_services),std::move(seed));
                const auto authority=[&]{return executing_;}; // Private candidate during this guarded command.
                std::vector<ObjectPositionIdentity> order;
                for(const auto& e:source.world.entities){if(e.ui_ghost)continue;const ObjectPositionIdentity id{e.id,e.generation};order.push_back(id);
                    const auto room=e.position?geometric_room_at_runtime(*edit.rooms,e.position->x,e.position->y,e.position->level):std::optional<std::uint16_t>{0};
                    need(room&&rebuilt.register_position(id,*room,true,authority),"architecture room registry regeneration failed");
                }
                if(prior.complete)need(rebuilt.finish_initial_load(order,authority),"architecture room registry cohort incomplete");
                objects.position_registry=rebuilt.snapshot();
                // A known-incomplete prior event ledger cannot become proven
                // merely because geometry regeneration supplied memberships.
                if(!prior.complete)objects.position_registry->complete=false;
                source.world=candidate.snapshot();
            }
        }
        hooks_.validate_candidate(source,*edit.candidate,*edit.rooms);
        const auto archive=odh::encode_session_bundle(source);hooks_.validate_reopen(source,archive);
        need(odh::encode_session_bundle(hooks_.capture_current())==before,"architecture live session changed during candidate preparation");
        journal_.verify_current();r.archive=archive;r.checkpoint=journal_.publish(std::move(source));published=true;
        r.state=ArchitectureEditSessionState::published_reload_required;r.durability_uncertain=journal_.durability_uncertain();
        const auto reread=odh::encode_session_archive(read_session_archive(r.checkpoint));need(reread==archive,"published architecture checkpoint differs");journal_.validate_restore(reread);
        return r;
    }catch(const std::exception&e){r.diagnostic=e.what();if(published){r.state=ArchitectureEditSessionState::published_reload_required;}return r;}
}
}
