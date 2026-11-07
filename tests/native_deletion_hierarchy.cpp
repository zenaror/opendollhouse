// SPDX-License-Identifier: MPL-2.0
#include "native_deletion_hierarchy.hpp"
#include "source_container_drop.hpp"
#include "odh/object_position_registry.hpp"
#include "odh/npc_avatar.hpp"
#include <iostream>
#include <map>
#include <stdexcept>
using namespace odh;using namespace odh_host;
namespace {
using Ref=WorldEntityReference;
void require(bool value,const char* why){if(!value)throw std::runtime_error(why);}
struct Fixture {
    World world;PreparedArchitecture prepared;bool active=true;
    unsigned pres=0,unregisters=0,registers=0,walls=0;
    std::function<void()> during_wall;
    std::function<bool()> escaped;
    Fixture(){prepared.width=prepared.height=8;}
    Ref ref(std::int16_t id)const{return {id,world.find(id)->generation};}
    Ref add(std::int16_t id,int x=56,int y=56,int level=1){
        require(world.create_runtime_object(id,100,{0,0,2},std::vector<EntryPoint>(27))&&world.declare_object_word(id,14,-1)&&world.set_position(id,{x,y,std::int16_t(level),0}),"actual object constructor/scalar");return ref(id);
    }
    void zero(std::int16_t child,std::int16_t parent){require(world.set_position(child,*world.find(parent)->position)&&world.declare_object_word(child,14,0)&&world.declare_object_word(child,26,parent)&&world.publish_native_slot0(ref(parent),ref(child)),"actual reciprocal zero chain");}
    void physical(std::int16_t child,std::int16_t parent,std::size_t slot=0){require(world.attach(parent,slot,child)&&world.refresh_contained_position(child)&&world.declare_object_word(child,14,std::int16_t(slot+1))&&world.declare_object_word(child,26,parent),"actual reciprocal physical slot");}
    SessionObjectPositionContext context(){SessionObjectPositionContext c;c.world=&world;c.prepared=&prepared;c.source_still_matches=[this]{return active;};
        c.position_phase=[this](auto who,const auto&,auto,const auto&,const auto& operation){return active&&world.is_live_reference({who.id,who.generation})&&operation();};
        c.slot_phase=[this](auto child,auto parent,std::size_t,auto,const auto& operation){return active&&world.is_live_reference({child.id,child.generation})&&world.is_live_reference({parent.id,parent.generation})&&operation();};return c;}
    SessionObjectPositionServices services(){SessionObjectPositionServices s;
        s.change.admit_loaded_entity=[this](auto id)->std::optional<bool>{return active&&world.is_live_reference({id.id,id.generation});};
        s.change.room_at=[](const auto& p)->std::optional<std::uint16_t>{return p.level;};
        s.change.unregister_position=[this](auto id,bool){++unregisters;return active&&world.is_live_reference({id.id,id.generation});};
        s.change.register_position=[this](auto id,std::uint16_t,bool){++registers;return active&&world.is_live_reference({id.id,id.generation});};
        s.change.wall_use=[this](auto,bool,bool){++walls;if(during_wall)during_wall();return active;};
        s.change.object_supported=[](auto,const auto&,bool){return true;};
        s.change.lifecycle_phase=[this](auto,auto,const std::function<bool()>& operation){++pres;escaped=operation;return operation();};return s;}
    std::shared_ptr<NativeFloorHeads> original_owner(std::string& error){
        std::vector<Ref> whole;for(auto id=world.next_entity(0,{},true);id;id=world.next_entity(*id,{},true))whole.push_back(ref(*id));
        auto owner=std::make_shared<NativeFloorHeads>(world,8,8,[this]{return active;});if(!owner->seed(whole,error))return {};return owner;
    }
    std::unique_ptr<NativeDeletionHierarchy> lease(Ref r,std::string& error){auto owner=original_owner(error);if(!owner)return {};return NativeDeletionHierarchy::prepare(context(),services(),r,error,{},owner);}
};
ObjectDefinition runtime_definition(bool npc=false){ObjectDefinition d{};d.version=138;d.guid=npc?200:100;d.object_type=npc?2:4;d.num_attributes=0;d.level_offset=0;d.tile_width=16;d.footprint_mask=0;return d;}
struct RuntimeFixture:Fixture {
    GeometricRoomMap rooms;DefinitionIndex definitions{{{"fixture","",128,runtime_definition()},{"npc","",128,runtime_definition(true)}}};
    ObjectPositionRegistrySnapshot registry;bool initialized=false;std::optional<World::RetiredContainer> active_retired;
    RuntimeFixture(){for(auto& level:prepared.levels){level.floors.assign(64,1);level.walls.resize(64);}
        rooms.width=rooms.height=8;rooms.floor_resources_resolved=rooms.light_base_rooms_resolved=true;
        rooms.rooms={{},{1,0,64,{0,0,8,8},false,false,{},1},{2,1,64,{0,0,8,8},false,false,{},2}};
        rooms.levels[0].assign(64,0x00010001U);rooms.levels[1].assign(64,0x00020002U);
    }
    ObjectPositionRegistryServices providers(){ObjectPositionRegistryServices s;s.definitions=&definitions;
        s.group=[](auto id)->std::optional<std::vector<ObjectPositionIdentity>>{return std::vector<ObjectPositionIdentity>{id};};
        s.routing_slot_count=[](auto)->std::optional<std::size_t>{return 0;};s.moved_self=[](auto)->std::optional<bool>{return false;};
        s.retained_moves_often=[this](auto id)->std::optional<bool>{const Ref ref{id.id,id.generation};if(!active_retired||active_retired->parent()!=ref||!world.retired_container_active(*active_retired))return {};const auto* parent=world.resolve_reference(ref);if(!parent)return {};if(parent->kind==EntityKind::avatar)return true;return known_object_moved_self(registry,id);};return s;}
    std::uint16_t room(Ref id)const{const auto* e=world.resolve_reference(id);return e&&e->position?*geometric_room_at_runtime(rooms,e->position->x,e->position->y,e->position->level):0;}
    void initialize(){if(initialized)return;ObjectPositionRegistry source(world,prepared,rooms,providers());std::vector<ObjectPositionIdentity> whole;
        for(auto id=world.next_entity(0,{},true);id;id=world.next_entity(*id,{},true)){const auto r=ref(*id);whole.push_back({r.id,r.generation});require(source.register_position({r.id,r.generation},room(r),true,[this]{return active;}),"actual flat initial position event");}
        require(source.finish_initial_load(whole,[this]{return active;}),"actual complete-cohort flat registry admission");registry=source.snapshot();initialized=true;
    }
    bool event(Ref ref,bool add){initialize();ObjectPositionRegistry source(world,prepared,rooms,providers(),registry);
        const bool ok=add?source.register_position({ref.id,ref.generation},room(ref),true,[this]{return active;}):source.unregister_position({ref.id,ref.generation},true,[this]{return active;});registry=source.snapshot();return ok;}
    void constructor_metadata(Ref r){require(record_object_footprint_absent(registry,world,{r.id,r.generation},[this]{return active;})&&record_object_moved_self(registry,world,{r.id,r.generation},false,[this]{return active;}),"real fresh-constructor registry metadata writers");}
    Ref add(std::int16_t id,int x=56,int y=56,int level=1){auto r=Fixture::add(id,x,y,level);if(initialized){constructor_metadata(r);require(event(r,true),"actual constructed registration event");}return r;}
    Ref add_saved(std::int16_t id,std::int16_t raw_slot=-1,std::int16_t raw_parent=0){
        std::array<std::int16_t,68> words{};words[3]=-1;words[14]=raw_slot;words[26]=raw_parent;words[41]=1234;words[67]=2;
        std::array<std::int16_t,5> extras{{11,22,33,44,55}};
        require(world.create_saved_entity(id,EntityKind::object,100,{}, {}, {},words,extras,2)&&world.define_entry_points(id,std::vector<EntryPoint>(27)),"actual saved object resource/bank import");
        const auto r=ref(id);require(world.publish_native_slot0(r,{})&&world.set_position(id,{56,56,1,0}),"authenticated imported empty native storage and scalar");if(initialized){constructor_metadata(r);require(event(r,true),"actual saved object registration");}return r;
    }
    Ref add_npc(std::int16_t id,int x=56,int y=56){EntryPointTable table;table.entries.resize(27);
        constexpr std::int16_t caller=30000;if(!world.find(caller))add(caller,104,104);
        const CreateObjectInstanceRequest creation{200,static_cast<std::uint8_t>(CreateObjectPosition::out_of_world),0,0,0};
        const VmFrame source{routine_key(4096),0,{caller,caller,0,0},{},{}};const Ts1Temps temps{};const NeighborContext neighbors({0,62,{},0});
        const auto request=resolve_npc_avatar_guid_request(creation,source,temps,neighbors,world);
        require(request.status==NpcAvatarRequestStatus::ready&&request.request&&materialize_npc_avatar(world,id,*request.request,runtime_definition(true),table)&&world.set_position(id,{x,y,1,0}),"actual resource-backed fresh NPC request/constructor and scalar position");
        const auto r=ref(id);if(initialized){constructor_metadata(r);require(event(r,true),"actual NPC registration event");}return r;
    }
    bool move(std::int16_t id,EntityPosition p){const auto r=ref(id);return event(r,false)&&world.set_position(id,p)&&event(r,true);}
    bool attach_move(std::int16_t child,std::int16_t parent,std::size_t slot){const auto r=ref(child);return event(r,false)&&world.attach(parent,slot,child)&&world.refresh_contained_position(child)&&event(r,true);}
    bool detach_move(std::int16_t child,std::int16_t parent,std::size_t slot,EntityPosition p){const auto r=ref(child);return event(r,false)&&world.detach(parent,slot)&&world.set_position(child,p)&&event(r,true);}
    SessionObjectPositionContext context(){initialize();auto c=Fixture::context();c.position_registry=&registry;c.rooms=&rooms;return c;}
    SessionObjectPositionServices services(){auto s=Fixture::services();
        s.change.admit_loaded_entity=[this](auto id)->std::optional<bool>{const auto* body=world.resolve_reference({id.id,id.generation});return active&&body&&body->definition&&body->entry_points&&definitions.unique(body->definition->guid);};
        s.change.room_at=[this](const auto& p){return geometric_room_at_runtime(rooms,p.x,p.y,p.level);};
        s.change.unregister_position=[this](auto id,bool){++unregisters;return event({id.id,id.generation},false);};
        s.change.register_position=[this](auto id,std::uint16_t actual,bool){++registers;const Ref ref{id.id,id.generation};return actual==room(ref)&&event(ref,true);};return s;
    }
    std::unique_ptr<NativeDeletionHierarchy> lease(Ref r,std::string& error){return NativeDeletionHierarchy::prepare(context(),services(),r,error);}
};
void deletion_matrix(){for(const int level:{1,2,3})for(const unsigned shape:{0U,1U,2U,3U,4U}){
    Fixture f;const auto root=f.add(1,56,56,level);std::optional<Ref> child,parent;
    if(shape==1||shape==3||shape==4){child=f.add(2,56,56,level);f.zero(2,1);}
    if(shape==2||shape==3){parent=f.add(3,56,56,level);f.zero(1,3);}
    if(shape==4){parent=f.add(3,56,56,level);f.physical(1,3);}
    std::string error;auto lease=f.lease(root,error);require(bool(lease),"read-only full index preparation");
    require(!lease->effects_started()&&f.pres==0&&f.unregisters==0,"prepare cannot run lifecycle or cleanup effects");
    require(lease->before_position_change(error)&&lease->before_position_change(error),"both actual Pre calls complete");
    require(lease->pre_calls()==2&&lease->unlink_calls()==1&&f.pres==2&&lease->effects_started(),"two real Pre, exactly one native unlink");
    require(f.escaped&&!f.escaped(),"escaped lifecycle closure expires before later invocation");
    if(child){require(f.world.find(child->id)->position->level==level,"promotion preserves old source level");
        if(parent){if(shape==4)require(f.world.attachment_parent(*child)==parent,"promoted zero child inherits actual physical parent");
            else require(f.world.native_slot0(*parent)==child,"promoted zero child replaces removed node in old raw parent");}
        else require(f.world.find(child->id)->object_words.at(14)==-1&&f.world.find(child->id)->object_words.at(26)==0,"head child becomes native floor root");}
    require(f.world.remove_retaining_body(root)&&lease->finish(error)&&f.world.resolve_reference(root)&&!f.world.is_live_reference(root),"actual retained retirement settles promoted index");
    require(!lease->finish(error)&&!lease->before_position_change(error),"finished lease cannot be reused");
}}
void retained_contents_matrix(){
    // The actual SourceContainerDrop algorithm owns order, copies the removed
    // body, invokes actual World detach/OOW/placement and handles ordinary false.
    for(const int level:{1,2})for(const unsigned children:{1U,2U})for(const unsigned outcome:{0U,1U,2U,3U}){
        Fixture f;const auto root=f.add(1,56,56,level);f.add(3,56,56,level);f.zero(3,1);
        for(unsigned n=0;n<children;++n){const auto id=std::int16_t(2+n*2);f.add(id,56,56,level);f.physical(id,1,n);require(f.world.declare_object_word(id,34,1),"source hidden child input");}
        unsigned copies=0,placements=0;std::string error;std::optional<World::RetiredContainer> expired_token;
        NativeDeletionHierarchy::DropOwner owner=[&](const World::RetiredContainer& token,NativeDeletionHierarchy& lease,std::string& why){
            expired_token=token;SourceContainerDropServices s;s.parent=token.parent();s.source_still_matches=[&,token]{return f.active&&f.world.retired_container_active(token);};
            s.total_slots=[&]{return f.world.retired_slot_count(token);};s.child=[&](std::size_t slot){return f.world.retired_slot(token,slot);};
            s.copy_position=[&](Ref child){return lease.copy_retired_child_position(token,child,[&]{++copies;return f.world.copy_retired_position(token,child);},why);};
            s.direction=[&](Ref child)->std::optional<std::uint8_t>{const auto p=f.world.native_position(child);return p?std::optional<std::uint8_t>{p->direction}:std::nullopt;};
            s.anchor_positioned=[&]()->std::optional<bool>{const auto* body=f.world.resolve_reference(root);return body?std::optional<bool>{body->position.has_value()}:std::nullopt;};
            s.out_of_world=[&](Ref child,std::uint8_t){return lease.place_retired_child(token,child,[&]{
                Ts1PlacementResult result;result.status=Ts1PlacementStatus::unsupported;
                const auto* body=f.world.resolve_reference(child);const auto slot=body&&body->attachment?std::optional<std::size_t>{body->attachment->slot}:std::nullopt;
                if(slot&&f.world.detach_retained_slot(token,*slot,child)&&f.world.publish_native_recovery_position(child,{},true))result.status=Ts1PlacementStatus::success;
                return result;
            },why);};
            s.hidden=[&](Ref child)->std::optional<std::int16_t>{const auto* body=f.world.resolve_reference(child);return body?std::optional<std::int16_t>{body->object_words.at(34)}:std::nullopt;};
            s.unhide=[&](Ref child){VmFrame address;address.context.caller=child.id;return f.world.bind_context_live(address.context)&&f.world.write(address,3,34,0);};
            s.find_location=[&](Ref child){return lease.place_retired_child(token,child,[&]{
                Ts1PlacementResult result;++placements;
                if(outcome==1){result.status=Ts1PlacementStatus::blocked;return result;}
                if(outcome==2){result.status=Ts1PlacementStatus::unsupported;result.diagnostic="actual source placement unavailable";return result;}
                const auto x=outcome==3?56:72+(child.id==4?16:0);
                result.status=f.world.place_individual_on_floor(child.id,EntityPosition{x,56,std::int16_t(level),0},0)?Ts1PlacementStatus::success:Ts1PlacementStatus::unsupported;
                return result;
            },why);};
            const auto dropped=source_container_drop(s);if(!dropped.completed&&why.empty())why=dropped.diagnostic;return dropped.completed;
        };
        auto floors=f.original_owner(error);require(bool(floors),"strict original floor fixture owner");
        auto lease=NativeDeletionHierarchy::prepare(f.context(),f.services(),root,error,std::move(owner),floors);
        require(lease&&lease->target()==root&&!lease->effects_started(),"actual owned drop capability admits physical contents read-only");
        require(lease->before_position_change(error)&&lease->before_position_change(error),"two real Pre calls preserve physical contents until indexed retirement");
        const bool retired=f.world.remove_retaining_contents(root,[&](const auto& token){return lease->begin_indexed_retirement(token,error)&&lease->drop_retired_contents(token,error);});
        if(outcome>=2){
            require(!retired&&!error.empty()&&lease->membership_forgotten()&&!lease->finish(error)&&f.world.resolve_reference(root),"failed/coincident source drop preserves partial effects and retained body, never guesses floor order");
            require(copies==1&&placements==1,"source stops at first unsupported placement");continue;
        }
        require(retired&&lease->membership_forgotten()&&lease->contents_completed()&&lease->finish(error),"actual child-drop and native floor ownership settle without a second forget");
        require(expired_token&&!f.world.retired_container_active(*expired_token)&&!lease->floor_heads()->begin_retired_container(*expired_token,error),"expired World token cannot reopen a floor retirement scope");
        require(copies==children&&placements==children&&lease->pre_calls()==2&&lease->unlink_calls()==1,"source slot loop and native root unlink are independent");
        require(f.world.native_slot0(f.ref(3))==std::optional<Ref>{Ref{}},"promoted raw child remains the original floor head");
        for(unsigned n=0;n<children;++n){const auto id=std::int16_t(2+n*2);const auto* body=f.world.find(id);
            require(body&&!body->attachment&&body->object_words.at(14)==-1&&body->object_words.at(26)==0&&body->object_words.at(34)==0,"actual physical detach and Hidden publication preserve known native metadata");
            if(outcome==1)require(!body->position,"ordinary FindLocation false retains real OOW child");
            else require(lease->floor_heads()->head(*body->position)==std::optional<Ref>{f.ref(id)},"known-empty actual placement publishes its own native head");
        }
        require(f.world.resolve_reference(root)->position==std::optional<EntityPosition>{EntityPosition{56,56,std::int16_t(level),0}},"retained body keeps last actual copied child position");
    }
}
void queued_phase_matrix(){
    {RuntimeFixture f;const auto root=f.add(1);f.add(2);f.zero(2,1);f.add(7,88,56);bool entry=false;
        std::unique_ptr<NativeDeletionHierarchy> first,second;
        const auto pending=[&]{std::vector<Ref> refs;if(first&&first->certified_detached())refs.push_back(first->target());if(second&&second->certified_detached())refs.push_back(second->target());return refs;};
        auto context=f.context();context.source_still_matches=[&]{return f.active&&entry;};entry=true;std::string error;
        first=NativeDeletionHierarchy::prepare(context,f.services(),root,error,{}, {},pending);
        require(first&&first->before_position_change(error)&&first->unlink_calls()==1,"immediate source unlink yields a queued operation receipt");
        entry=false;require(first->certified_detached()&&!first->before_position_change(error)&&first->certified_detached(),"queued receipt stays visible with execution guard closed; effects remain forbidden");
        require(f.move(7,{104,56,1,0}),"unrelated live movement while Delete is queued");
        second=NativeDeletionHierarchy::prepare(f.context(),f.services(),f.ref(2),error,{}, {},pending);
        require(second&&second->before_position_change(error)&&second->certified_detached(),"second deletion elects its actual head while excluding only certified pending first target");
        require(f.world.remove_retaining_body(f.ref(2))&&second->finish(error),"another exact target retires before the first operation final Pre");
        f.add(9,88,72);require(first->certified_detached(),"cohort construction/removal and other movement do not invalidate target receipt");
        entry=true;require(first->before_position_change(error)&&first->before_position_change(error)&&first->pre_calls()==3&&first->unlink_calls()==1,
            "source final two Pre calls reuse the queued target receipt, not stale whole-World/old-parent snapshots");
        require(f.world.remove_retaining_body(root)&&first->finish(error),"deferred target retirement completes after independent same-frame mutations");
    }
    {RuntimeFixture f;const auto root=f.add(1);f.add(2);f.zero(2,1);const auto parent=f.add(3);f.physical(1,3);
        std::string error;auto lease=f.lease(root,error);require(lease&&lease->before_position_change(error),"real physical Pre detaches root while compatibility cleanup releases legacy raw child independently");
        require(f.attach_move(1,3,1)&&!lease->certified_detached(),"source really reattaches the same queued body");
        require(lease->before_position_change(error)&&lease->unlink_calls()==2,"new physical attachment executes a second actual native unlink");
        require(!f.world.find(2)->attachment&&f.world.find(3)->slots[0]==0&&f.move(2,{88,56,1,0})&&f.move(3,{72,56,1,0}),"independent legacy child and former physical parent continue their own current lifecycles");
        require(lease->before_position_change(error)&&lease->before_position_change(error)&&lease->unlink_calls()==2&&lease->pre_calls()==4,
            "final Pre calls do not repeat unlink or freeze old promoted-survivor proofs");
        require(f.world.remove_retaining_body(root)&&lease->finish(error),"reattached then deferred Delete completes with exact original retained body");
        (void)parent;
    }
    {Fixture f;const auto root=f.add(1);f.add(2);f.zero(2,1);std::string error;
        auto owner=std::make_shared<NativeFloorHeads>(f.world,8,8,[&]{return f.active;});require(owner->seed({root,f.ref(2)},error),"actual authenticated recovery floor cohort");
        auto lease=NativeDeletionHierarchy::prepare(f.context(),f.services(),root,error,{},owner);
        require(lease&&lease->floor_heads().get()==owner.get()&&lease->before_position_change(error)&&lease->before_position_change(error),"recovery reuses its existing native index rather than creating a parallel owner");
        require(f.world.remove_retaining_body(root)&&lease->finish(error)&&!owner->forget_unlinked(root,error),"actual body retirement forgets the shared cohort exactly once");
    }
}
NativeDeletionHierarchy::DropOwner runtime_drop(RuntimeFixture& f,bool block_find=false){
    return [&f,block_find](const World::RetiredContainer& token,NativeDeletionHierarchy& lease,std::string& error){
        f.active_retired=token;struct End{RuntimeFixture& f;~End(){f.active_retired.reset();}}end{f};
        SourceContainerDropServices s;s.parent=token.parent();s.source_still_matches=[&]{return f.active&&f.world.retired_container_active(token);};
        s.total_slots=[&]{return f.world.retired_slot_count(token);};s.child=[&](std::size_t slot){return f.world.retired_slot(token,slot);};
        s.copy_position=[&](Ref child){return lease.copy_retired_child_position(token,child,[&]{return f.world.copy_retired_position(token,child);},error);};
        s.direction=[&](Ref child)->std::optional<std::uint8_t>{const auto p=f.world.native_position(child);return p?std::optional<std::uint8_t>{p->direction}:std::nullopt;};
        s.anchor_positioned=[&]()->std::optional<bool>{const auto* parent=f.world.resolve_reference(token.parent());return parent?std::optional<bool>{parent->position.has_value()}:std::nullopt;};
        s.out_of_world=[&](Ref child,std::uint8_t){return lease.place_retired_child(token,child,[&]{
            Ts1PlacementResult result;const auto* body=f.world.resolve_reference(child);const auto slot=body&&body->attachment?std::optional<std::size_t>{body->attachment->slot}:std::nullopt;
            result.status=slot&&f.event(child,false)&&f.world.detach_retained_slot(token,*slot,child)&&f.world.publish_native_recovery_position(child,{},true)&&f.event(child,true)?Ts1PlacementStatus::success:Ts1PlacementStatus::unsupported;return result;
        },error);};
        s.hidden=[&](Ref child)->std::optional<std::int16_t>{const auto* body=f.world.resolve_reference(child);return body?std::optional<std::int16_t>{body->object_words.at(34)}:std::nullopt;};
        s.unhide=[&](Ref child){VmFrame address;address.context.caller=child.id;return f.world.bind_context_live(address.context)&&f.world.write(address,3,34,0);};
        s.find_location=[&](Ref child){return lease.place_retired_child(token,child,[&]{Ts1PlacementResult result;
            if(block_find){result.status=Ts1PlacementStatus::blocked;return result;}
            result.status=f.move(child.id,{56,56,1,0})?Ts1PlacementStatus::success:Ts1PlacementStatus::unsupported;return result;
        },error);};
        const auto dropped=source_container_drop(s);if(!dropped.completed&&error.empty())error=dropped.diagnostic;return dropped.completed;
    };
}
void runtime_flat_matrix(){
    {RuntimeFixture f;const auto root=f.add_npc(1);f.add_npc(2,58,62);std::string error;
        auto lease=NativeDeletionHierarchy::prepare(f.context(),f.services(),root,error,runtime_drop(f));
        if(!lease)throw std::runtime_error("actual NPC Delete preflight: "+error);
        require(lease->spatial_backend()==NativeDeletionSpatialBackend::runtime_flat_registry&&!lease->floor_heads(),"two real NPC avatars can share a flat query cell without a fabricated head owner");
        require(lease->before_position_change(error)&&lease->before_position_change(error)&&lease->pre_calls()==2,"actual avatar Pre calls unregister the exact NPC only");
        require(f.world.remove_retaining_contents(root,[&](const auto& token){return lease->begin_indexed_retirement(token,error)&&lease->drop_retired_contents(token,error);})&&lease->finish(error),"real NPC retained retirement finishes with shared-cell survivor preserved");
        const auto other=f.ref(2);require(std::any_of(f.registry.tiles.begin(),f.registry.tiles.end(),[&](const auto& tile){return std::any_of(tile.entities.begin(),tile.entities.end(),[&](auto id){return id.id==other.id&&id.generation==other.generation;});}),"NPC survivor remains in the actual tile list");
    }
    for(const bool distant:{false,true})for(const bool held:{false,true}){
        RuntimeFixture f;const auto root=f.add_saved(1);const auto child=f.add_saved(2,0,1);f.zero(2,1);
        if(distant)require(f.world.set_position(2,{88,72,2,4}),"independent legacy child position is already different from its old virtual root");
        f.add_npc(7,72,56);if(held){f.physical(1,7);if(!distant)require(f.world.set_position(2,*f.world.find(1)->position),"legacy child shares the held root cell while remaining an independent managed object");}
        else {f.add(3);f.zero(1,3);}
        auto context=f.context();const auto position=f.world.native_position(child);const auto bank=*f.world.find(2)->saved_object_banks;const auto words=f.world.find(2)->object_words;const auto physical=f.world.find(2)->attachment;
        std::string error;auto lease=NativeDeletionHierarchy::prepare(context,f.services(),root,error);
        require(lease&&lease->before_position_change(error)&&lease->before_position_change(error),"flat virtual cleanup follows the actual physical Pre path");
        const auto* after=f.world.find(2);require(f.world.native_position(child)==position&&!after->attachment&&!physical&&after->saved_object_banks&&
            after->saved_object_banks->object_data==bank.object_data&&after->saved_object_banks->extras==bank.extras,"flat legacy cleanup preserves child XYZ/level/physical state and immutable original banks");
        for(const auto& [index,value]:words)if(index!=14&&index!=26)require(after->object_words.at(index)==value,"compatibility cleanup changes no other live child word");
        if(held)require(f.world.find(7)->slots[0]==0&&after->object_words.at(14)==-1&&after->object_words.at(26)==0,"legacy child never moves into an old managed physical slot");
        else require(f.world.native_slot0(f.ref(3))==std::optional<Ref>{child}&&after->object_words.at(14)==0&&after->object_words.at(26)==3,"only the old virtual parent receives its legacy-chain splice");
        require(f.world.remove_retaining_body(root)&&lease->finish(error),"flat legacy root retires without changing surviving managed child state");
    }
    for(const bool blocked:{false,true}){RuntimeFixture f;const auto root=f.add(1);f.add(2);f.physical(2,1);f.add_npc(7);std::string error;
        auto lease=NativeDeletionHierarchy::prepare(f.context(),f.services(),root,error,runtime_drop(f,blocked));require(lease&&lease->before_position_change(error)&&lease->before_position_change(error),"real flat contained-child preparation");
        require(f.world.remove_retaining_contents(root,[&](const auto& token){return lease->begin_indexed_retirement(token,error)&&lease->drop_retired_contents(token,error);})&&lease->finish(error),"actual SourceContainerDrop retains ordinary false and permits a proven overlapping flat destination");
        require(blocked?!f.world.find(2)->position:f.world.find(2)->position==std::optional<EntityPosition>{EntityPosition{56,56,1,0}},"actual child placement result remains observable");
    }
    {RuntimeFixture f;const auto root=f.add(1);auto context=f.context();f.registry.complete=false;std::string error;require(!NativeDeletionHierarchy::prepare(context,f.services(),root,error)&&f.pres==0,"incomplete flat registry is unknown, never empty or a successful no-op");}
    {RuntimeFixture f;const auto root=f.add(1);auto context=f.context();auto services=f.services();services.change.unregister_position=[](auto,bool){return true;};std::string error;
        auto lease=NativeDeletionHierarchy::prepare(context,services,root,error);require(lease&&!lease->before_position_change(error)&&lease->effects_started()&&!lease->certified_detached(),"claimed Pre completion cannot replace actual registry removal");}
}
void runtime_unregistered_oow_matrix(){
    // The original reset runs real Pre but skips virtual+58/Post for an OLD
    // OOW snapshot. Retain the actual source metadata, not a fabricated tile.
    for(const int level:{1,2}){
        RuntimeFixture f;std::vector<Ref> roots;for(int id=1;id<=4;++id)roots.push_back(f.add(std::int16_t(id)));
        f.initialize();
        for(const auto root:roots){require(f.event(root,false)&&f.world.publish_native_recovery_position(root,{},true)&&
            f.world.set_native_out_of_world_level(root,std::int16_t(level)),"real original-reset unregister and known OOW publication");}
        const auto before=f.registry;std::vector<std::unique_ptr<NativeDeletionHierarchy>> leases;std::string error;
        for(const auto root:roots){auto lease=NativeDeletionHierarchy::prepare(f.context(),f.services(),root,error,runtime_drop(f));
            if(!lease)throw std::runtime_error("known unregistered OOW source admission: "+error);leases.push_back(std::move(lease));}
        require(f.unregisters==0&&f.registers==0,"unregistered OOW preflight does not invent registration or lifecycle effects");
        for(std::size_t i=0;i<roots.size();++i){auto& lease=leases[i];const auto root=roots[i];
            require(lease->before_position_change(error)&&lease->before_position_change(error)&&lease->pre_calls()==2,
                "unregistered OOW still executes both source Pre operations");
            require(f.world.native_position(root)->level==level&&f.registers==0,"unregistered OOW preserves its known native level without a fake Post");
            require(f.world.remove_retaining_contents(root,[&](const auto& token){return lease->begin_indexed_retirement(token,error)&&lease->drop_retired_contents(token,error);})&&lease->finish(error),
                "multiple original-unregistered OOW bodies retire through real token and empty source drop");}
        require(before.motion.size()==4&&before.footprints.size()==4&&f.unregisters==8,"OOW source metadata belongs to all exact cohort identities and both actual Pres");
    }
    for(const unsigned invalid:{0U,1U,2U,3U,4U,5U,6U,7U,8U,9U,10U,11U,12U,13U,14U,15U,16U}){
        RuntimeFixture f;const auto root=f.add(1);f.initialize();require(f.event(root,false)&&f.world.publish_native_recovery_position(root,{},true),"negative OOW genuine source preparation");
        if(invalid==0)f.registry.complete=false;
        if(invalid==1)f.registry.footprints.clear();
        if(invalid==2)f.registry.motion.clear();
        if(invalid==3)++f.registry.footprints.front().owner.generation;
        if(invalid==4)++f.registry.motion.front().owner.generation;
        if(invalid==5)f.registry.rooms[0].entities.push_back({root.id,root.generation});
        if(invalid==6)f.registry.tiles.push_back({-1,{{root.id,root.generation}}});
        if(invalid==7)f.registry.footprints.front().room=0;
        if(invalid==8)f.registry.rooms[0].portals.push_back({{root.id,root.generation},1});
        if(invalid==9)require(f.world.set_position(root.id,{56,56,1,0})&&f.world.place_out_of_world(root.id,0),"generic transition from actual in-world position deliberately lacks native OOW scalar proof");
        if(invalid==10)require(f.world.set_position(root.id,{56,56,1,0}),"in-world absence must not inherit an OOW receipt");
        const ObjectPositionIdentity stale{root.id,root.generation+1};
        if(invalid==11)f.registry.rooms[0].entities.push_back(stale);
        if(invalid==12)f.registry.tiles.push_back({-1,{stale}});
        if(invalid==13)f.registry.rooms[0].portals.push_back({stale,1});
        if(invalid==14)f.registry.rooms[0].window_portals.push_back({stale,1});
        if(invalid==15||invalid==16){const auto& shape=f.registry.footprints.front();
            (invalid==15?f.registry.rooms[0].static_obstacles:f.registry.rooms[0].dynamic_obstacles).push_back({stale,shape.box,shape.instance});}
        std::string error;const auto rejected=NativeDeletionHierarchy::prepare(f.context(),f.services(),root,error);
        if(rejected||f.pres!=0||f.unregisters!=0||error.empty())throw std::runtime_error(
            "unregistered OOW negative case="+std::to_string(invalid)+" admitted="+std::to_string(bool(rejected))+
            " pres="+std::to_string(f.pres)+" unregisters="+std::to_string(f.unregisters)+" diagnostic="+error);
    }
    {RuntimeFixture f;const auto root=f.add(1);f.initialize();require(f.event(root,false)&&f.world.publish_native_recovery_position(root,{},true)&&f.event(root,true),"normal registered OOW source Post");
        std::string error;auto lease=f.lease(root,error);require(lease&&lease->before_position_change(error)&&lease->before_position_change(error)&&f.world.remove_retaining_body(root)&&lease->finish(error),
            "normal room0/tile-minus1 OOW registration retains the existing source deletion path");}
}
void unindexed_constructor_matrix(){
    // A producer-owned construction scope, not a fake complete registry. The
    // exact origin survives actual retirement just as created/retired origins do.
    Fixture f;const auto root=f.add(1);require(f.world.publish_native_recovery_position(root,{},true),"actual constructor OOW source publication");
    bool bootstrap=true;std::map<Ref,std::uint32_t> created{{root,100}},retired;
    const auto owner=[&](Ref ref){const auto* body=f.world.resolve_reference(ref);const auto live=created.find(ref),old=retired.find(ref);
        const auto guid=live!=created.end()?std::optional<std::uint32_t>{live->second}:old!=retired.end()?std::optional<std::uint32_t>{old->second}:std::nullopt;
        return bootstrap&&ref==root&&body&&body->definition&&guid&&body->definition->guid==*guid;};
    unsigned child_calls=0;
    const NativeDeletionHierarchy::DropOwner drop=[&](const auto& token,NativeDeletionHierarchy&,std::string& error){
        SourceContainerDropServices s;s.parent=token.parent();s.source_still_matches=[&]{return bootstrap&&f.world.retired_container_active(token);};
        s.total_slots=[&]{return f.world.retired_slot_count(token);};s.child=[&](std::size_t slot){return f.world.retired_slot(token,slot);};
        s.direction=[&](Ref)->std::optional<std::uint8_t>{++child_calls;return {};};s.copy_position=[&](Ref){++child_calls;return false;};
        s.anchor_positioned=[&]()->std::optional<bool>{const auto* body=f.world.resolve_reference(token.parent());return body?std::optional<bool>{body->position.has_value()}:std::nullopt;};
        s.out_of_world=[&](Ref,std::uint8_t){++child_calls;Ts1PlacementResult r;r.status=Ts1PlacementStatus::unsupported;return r;};
        s.hidden=[&](Ref)->std::optional<std::int16_t>{++child_calls;return {};};s.unhide=[&](Ref){++child_calls;return false;};
        s.find_location=[&](Ref){++child_calls;Ts1PlacementResult r;r.status=Ts1PlacementStatus::unsupported;return r;};
        const auto result=source_container_drop(s);if(!result.completed)error=result.diagnostic;return result.completed;
    };
    std::string error;auto context=f.context();auto services=f.services();const auto no_index=services.change.unregister_position;
    services.change.unregister_position=[&,no_index](auto id,bool change){return !context.position_registry&&owner({id.id,id.generation})&&no_index(id,change);};
    auto lease=NativeDeletionHierarchy::prepare(context,services,root,error,drop,{}, {},owner);
    require(lease&&lease->spatial_backend()==NativeDeletionSpatialBackend::unindexed_constructor&&!lease->floor_heads(),"producer receipt selects explicit unindexed constructor backend without a fake registry");
    require(lease->before_position_change(error)&&lease->before_position_change(error)&&lease->pre_calls()==2&&f.pres==2&&f.unregisters==2,"both actual lifecycle Pre calls run through the lawful absent-index service");
    require(f.world.remove_retaining_contents(root,[&](const auto& token){retired.emplace(root,created.at(root));created.erase(root);return lease->begin_indexed_retirement(token,error)&&lease->drop_retired_contents(token,error);})&&lease->finish(error)&&child_calls==0&&f.registers==0,
        "actual retained retirement/empty source drop completes under the same construction receipt after origin retirement");
    for(const unsigned invalid:{0U,1U,2U,3U,4U,5U,6U,7U}){
        Fixture q;const auto target=q.add(1);require(q.world.publish_native_recovery_position(target,{},true),"negative constructor source OOW");
        auto ctx=q.context();ObjectPositionRegistrySnapshot incomplete;incomplete.width=incomplete.height=8;
        bool scope=true;Ref origin=target;
        if(invalid==0)scope=false;
        if(invalid==1)origin.generation++;
        if(invalid==2)ctx.position_registry=&incomplete;
        if(invalid==3)require(q.world.set_position(1,{56,56,1,0}),"constructor has a real in-world position");
        if(invalid==4){q.add(2);q.physical(2,1);}
        if(invalid==5){q.add(2);require(q.world.declare_object_word(2,14,0)&&q.world.declare_object_word(2,26,1)&&q.world.publish_native_slot0(target,q.ref(2)),"constructor acquired a virtual child");}
        if(invalid==6){q.add(2);require(q.world.declare_object_word(1,14,0)&&q.world.declare_object_word(1,26,2)&&q.world.publish_native_slot0(q.ref(2),target),"constructor acquired an incoming virtual parent");}
        const NativeDeletionHierarchy::UnindexedConstructionOwner proof=invalid==7?NativeDeletionHierarchy::UnindexedConstructionOwner{}:NativeDeletionHierarchy::UnindexedConstructionOwner{[&](Ref ref){const auto* body=q.world.resolve_reference(ref);return scope&&ref==origin&&body&&body->definition&&body->definition->guid==100;}};
        auto rejected=NativeDeletionHierarchy::prepare(ctx,q.services(),target,error,drop,{}, {},proof);
        require(!rejected&&q.pres==0&&!error.empty(),"missing/stale/expired construction ownership, existing incomplete registry, position, contents or links never acquire no-index permission");
    }
    {Fixture q;const auto target=q.add(1);require(q.world.publish_native_recovery_position(target,{},true),"expiry constructor OOW");bool scope=true;
        auto proof=[&](Ref ref){return scope&&ref==target&&q.world.resolve_reference(ref);};auto pending=NativeDeletionHierarchy::prepare(q.context(),q.services(),target,error,drop,{}, {},proof);
        require(pending&&pending->before_position_change(error),"valid initial construction Pre");scope=false;
        require(!pending->before_position_change(error)&&pending->pre_calls()==1,"construction phase expiration cannot authorize a later Pre through absent registry");}
}
void negative_matrix(){
    {Fixture f;const auto root=f.add(1);require(f.world.place_out_of_world(1,0),"generic OOW path leaves native sentinel level unknown");
        std::string error;auto lease=f.lease(root,error);
        require(!lease&&error.find("subject=1:")!=std::string::npos&&error.find("role=root")!=std::string::npos&&error.find("native_oow_level=unknown")!=std::string::npos&&f.pres==0,
            "missing root native scalar reports exact subject and sentinel backing before effects");}
    {Fixture f;const auto root=f.add(1);f.add(3);f.physical(1,3);require(f.world.place_individual_on_floor(3,{},0),"generic OOW parent path with actual held-child hierarchy");
        std::string error;auto lease=f.lease(root,error);
        require(!lease&&error.find("subject=3:")!=std::string::npos&&error.find("role=physical_parent")!=std::string::npos&&error.find("target=1:")!=std::string::npos&&f.pres==0,
            "missing closure scalar names the physical parent rather than blaming the deleted root");}

    {Fixture f;const auto r=f.add(1);f.add(2);f.physical(2,1);std::string error;auto lease=f.lease(r,error);require(!lease&&!error.empty()&&!f.pres,"unbound retired-child drop refuses before VM cleanup or Pre");}

    {RuntimeFixture f;const auto r=f.add(1);f.add(2,72,56);f.add(3,72,56);std::string error;auto lease=f.lease(r,error);require(lease&&lease->before_position_change(error)&&f.world.remove_retaining_body(r)&&lease->finish(error),"real flat overlap in another cell does not invent a native head gate");}
    {Fixture f;const auto r=f.add(1);f.add(2);std::string error;auto lease=f.lease(r,error);require(!lease&&error.find("marked unknown")!=std::string::npos&&error.find("query_tile=[3,3,1]")!=std::string::npos&&
        error.find("ref=1:")!=std::string::npos&&error.find("ref=2:")!=std::string::npos&&error.find("raw14=-1 raw26=0")!=std::string::npos&&
        error.find("native0=0:0")!=std::string::npos&&error.find("physical_parent=none")!=std::string::npos&&error.find("ui_ghost=false")!=std::string::npos&&!f.pres,
        "ambiguous consulted head reports its cause and all actual same-cell candidates before effects");
        auto owner=NativeFloorHeads::for_current_world(f.world,8,8,[&]{return f.active;});const auto revision=f.world.spatial_revision();
        require(!owner->checked_head({56,56,1,0},error)&&error.find("ambiguous unparented owners")!=std::string::npos&&f.world.spatial_revision()==revision,
            "checked head diagnostics do not mutate actual World state");
        require(!owner->checked_head({56,56,1,0},error)&&error.find("ambiguous unparented owners")!=std::string::npos,
            "repeated unknown reads retain the original resolver reason rather than hiding it behind a generic cache failure");}
    {Fixture f;const auto r=f.add(1);auto c=f.context();c.slot_phase={};std::string error;require(!NativeDeletionHierarchy::prepare(c,f.services(),r,error)&&!f.pres,"missing physical owner rejects before cleanup");}
    {Fixture f;const auto r=f.add(1);std::string error;auto lease=f.lease(r,error);require(bool(lease),"generation case preparation");require(f.world.remove(1)&&f.add(1,72,56).generation!=r.generation&&!lease->before_position_change(error)&&!lease->effects_started(),"replacement generation never receives original lifecycle");}
    {RuntimeFixture f;const auto r=f.add(1);std::string error;auto lease=f.lease(r,error);require(lease&&lease->before_position_change(error),"repeat case first real Pre");require(f.move(1,{72,56,1,0})&&!lease->certified_detached()&&lease->before_position_change(error)&&lease->pre_calls()==2&&lease->unlink_calls()==2,"actual new source position rearms unlink rather than reusing an old receipt");}
    {Fixture f;const auto r=f.add(1);std::string error;auto lease=f.lease(r,error);f.during_wall=[&]{f.active=false;};require(lease&&!lease->before_position_change(error)&&lease->effects_started()&&f.unregisters==1,"expired callback preserves partial effects without pretending rollback");}
    {Fixture f;const auto r=f.add(1);std::string error;auto lease=f.lease(r,error);f.during_wall=[&]{require(!lease->before_position_change(error),"same lease cannot recursively reenter Pre");};require(lease&&lease->before_position_change(error)&&lease->pre_calls()==1,"reentrant callback cannot add a nested unlink");}
    {Fixture f;const auto r=f.add(1,0,0);std::string error;auto lease=f.lease(r,error);require(lease&&lease->before_position_change(error)&&f.world.remove_retaining_body(r)&&lease->finish(error),"native boundary outside floor index remains a valid deletion");}
}
}
int main(){try{deletion_matrix();retained_contents_matrix();queued_phase_matrix();runtime_flat_matrix();runtime_unregistered_oow_matrix();unindexed_constructor_matrix();negative_matrix();std::cout<<"native deletion hierarchy lease matrix passed\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
