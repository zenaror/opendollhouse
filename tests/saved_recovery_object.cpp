// SPDX-License-Identifier: MPL-2.0
#include "saved_recovery_object.hpp"
#include "native_recovery_placement.hpp"
#include "native_seating.hpp"
#include "odh/object_position_registry.hpp"
#include <iostream>
#include <memory>
#include <limits>
#include <stdexcept>

namespace {
using namespace odh;using namespace odh_host;using Phase=SavedRecoveryPhase;
void require(bool yes,const char* text){if(!yes)throw std::runtime_error(text);}
ObjectDefinition definition(std::uint16_t master=0,std::uint32_t guid=100,std::uint16_t type=4){ObjectDefinition d{};d.version=138;d.guid=guid;d.object_type=type;d.tile_width=16;d.footprint_mask=0;d.master_id=master;return d;}
struct Fixture {
    World world;PreparedArchitecture prepared;SavedArchitecture architecture;GeometricRoomMap rooms;
    DefinitionIndex definitions;ArchitectureObjectSupport support;
    ObjectPositionRegistrySnapshot registry;
    std::shared_ptr<const VmProgram> program;
    std::vector<std::unique_ptr<Vm>> threads;
    std::vector<SavedRecoveryEntity> profiles;
    std::map<std::int16_t,unsigned> remove_calls,placement_calls;
    std::map<std::int16_t,SavedRecoveryNativeOffset> offsets{{1,{0,0,0}},{2,{16,0,0}}};
    bool authorized=true;bool portal_group=false,triple_group=false,regrouped=false;
    Fixture(bool portals=false,bool triple=false,std::uint16_t master=0,std::optional<bool> seat_fixture={},std::uint32_t guid=100,std::uint16_t type=4):definitions({{"fixture","",128,definition(master,guid,type)}}),support(make_architecture_object_support(4,4)),portal_group(portals),triple_group(triple){
        prepared.width=prepared.height=architecture.width=architecture.height=4;prepared.buildable.assign(16,1);
        for(auto& level:prepared.levels){level.floors.assign(16,1);level.walls.resize(16);}
        rooms.width=rooms.height=4;rooms.floor_resources_resolved=rooms.light_base_rooms_resolved=true;
        rooms.rooms={{},{1,0,8,{0,0,2,4},false,false,{},1},{2,0,8,{2,0,2,4},false,false,{},2},{3,1,16,{0,0,4,4},false,false,{},3}};
        rooms.levels[0].resize(16);rooms.levels[1].assign(16,0x00030003U);
        for(std::size_t i=0;i<16;++i)rooms.levels[0][i]=i%4<2?0x00010001U:0x00020002U;
        Bhav routine{0x8002,{{100,255,255,{}}},{},Bhav::Metadata{0,4,0,0}};
        program=std::make_shared<const VmProgram>(RoutineSet{{routine_key(4096),routine},{routine_key(4097),routine}});
        for(std::int16_t n=1;n<=(triple?3:2);++n){
            auto table=entries(portals);
            if(seat_fixture){if(n==1)table[26].action=*seat_fixture?4096:0;if(n==3)table[25].action=4096;}
            require(world.create_runtime_object(n,guid,{1,0,2},table),"real object constructor");
            require(world.declare_object_word(n,14,-1)&&world.declare_object_word(n,41,314)&&world.set_position(n,{triple?(n==3?40:24):(n==1?24:40),triple&&n==2?40:24,1,0}),"source banks and actual pose");
            threads.push_back(std::make_unique<Vm>(program,ShareVmProgramTag{},world.vm_memory()));
            for(std::size_t parameter=0;parameter<4;++parameter)
                threads.back()->temps()[parameter]=static_cast<std::int16_t>(n*100+parameter+1);
            profiles.push_back({reference(n),portals?SavedRecoveryClass::portal:SavedRecoveryClass::object,true,false,portals,23,threads.back().get(),{}});
        }
        if(triple){offsets[2]={0,16,0};offsets[3]={16,0,0};}
        ObjectPositionRegistry fresh(world,prepared,rooms,registry_services());
        std::vector<ObjectPositionIdentity> source;for(std::int16_t n=1;n<=(triple?3:2);++n){source.push_back(identity(n));require(fresh.register_position(identity(n),room(n),true,authority()),"real initial registry event");}
        require(fresh.finish_initial_load(source,authority()),"real registry source-load completion");registry=fresh.snapshot();
    }
    std::vector<EntryPoint> entries(bool portal)const{std::vector<EntryPoint> ep(27);ep[9].action=4097;ep[10].action=4096;if(portal)ep[15].action=500;return ep;}
    WorldEntityReference reference(std::int16_t n)const{const auto* e=world.find(n);return {n,e?e->generation:0};}
    ObjectPositionIdentity identity(std::int16_t n)const{return {n,world.find(n)->generation};}
    std::function<bool()> authority(){return [this]{return authorized;};}
    std::uint16_t room(std::int16_t n)const{const auto* e=world.find(n);return e->position?*geometric_room_at_runtime(rooms,e->position->x,e->position->y,e->position->level):0;}
    ObjectPositionRegistryServices registry_services(){ObjectPositionRegistryServices s;s.definitions=&definitions;
        s.group=[this](auto i)->std::optional<std::vector<ObjectPositionIdentity>>{if(triple_group){if(regrouped)return i.id==1?std::vector<ObjectPositionIdentity>{identity(1)}:std::vector<ObjectPositionIdentity>{identity(2),identity(3)};return std::vector<ObjectPositionIdentity>{identity(2),identity(1),identity(3)};}return portal_group?std::vector<ObjectPositionIdentity>{identity(1),identity(2)}:std::vector<ObjectPositionIdentity>{i};};
        s.routing_slot_count=[](auto)->std::optional<std::size_t>{return 0;};s.moved_self=[](auto)->std::optional<bool>{return false;};return s;}
    bool registry_event(ObjectPositionIdentity i,bool add,bool change,std::uint16_t actual=0){
        ObjectPositionRegistry live(world,prepared,rooms,registry_services(),registry);
        const bool ok=add?live.register_position(i,actual,change,authority()):live.unregister_position(i,change,authority());
        registry=live.snapshot();return ok;
    }
    SavedRecoveryObjectContext context(){SavedRecoveryObjectContext c;
        c.position.world=&world;c.position.prepared=&prepared;c.position.architecture=&architecture;c.position.rooms=&rooms;
        c.position.object_support=&support;c.position.position_registry=&registry;c.position.source_still_matches=authority();c.definitions=&definitions;
        c.position.registration_group=registry_services().group;
        auto& services=c.services.change;services.room_at=[this](const auto& p){return geometric_room_at_runtime(rooms,p.x,p.y,p.level);};
        services.unregister_position=[this](auto i,bool change){return registry_event(i,false,change);};
        services.register_position=[this](auto i,std::uint16_t r,bool change){return registry_event(i,true,change,r);};
        services.wall_use=[this](auto i,bool,bool exclusive){const auto* e=world.find(i.id);return e&&e->generation==i.generation&&(!exclusive||!(e->object_words.at(13)%15));};
        services.object_supported=[this](auto i,const auto& p,bool set){return set_architecture_object_supported(support,world,i,p,set,authority());};
        services.dynamic_multitile=[this](auto i){return world.find(i.id)&&world.find(i.id)->generation==i.generation&&world.find(i.id)->entry_points->at(8).action==0;};
        services.publish_position=[this](auto i,const auto& p,std::uint8_t d){const auto* e=world.find(i.id);return e&&e->generation==i.generation&&world.place_individual_on_floor(i.id,p,d);};
        services.restore_container=[this](auto child,auto parent,std::size_t slot){
            const auto* c=world.find(child.id);const auto* p=world.find(parent.id);
            // Same physical source operation as PositionBinding::publish_slot:
            // reset leaves the child OOW, which place_in_slot intentionally
            // rejects. Attach then copy parent XYZ retains the actual identity.
            return c&&p&&c->generation==child.generation&&p->generation==parent.generation&&
                world.attach(parent.id,slot,child.id)&&world.refresh_contained_position(child.id);
        };
        c.services.execute_entry=[this](auto i,std::uint16_t ep,std::int16_t stack,const std::vector<std::int16_t>& args)->std::optional<bool>{
            const auto* e=world.find(i.id);if(!authorized||!e||e->generation!=i.generation)return {};
            const auto action=e->entry_points->at(ep).action;if(action<=255)return false;
            VmPrimitive primitive=[this,ep](const VmFrame& frame,const auto&,const auto&,const auto&){
                const auto& owner=*threads.at(std::size_t(frame.context.caller-1));
                const std::vector<std::int16_t> defaults(owner.temps().begin(),owner.temps().begin()+4);
                require(frame.arguments==defaults&&frame.context.stack_object==0,
                    "native recovery entry receives its real receiver Thread TEMP defaults and NULL StackObject");
                if(ep==10){++remove_calls[frame.context.caller];require(world.declare_object_word(frame.context.caller,41,415),"actual EP10 effect");}
                if(ep==9){++placement_calls[frame.context.caller];require(world.write(frame,0,0,std::int16_t(placement_calls[frame.context.caller])),"actual EP9 register/World effect");}
                return PrimitiveOutcome{PrimitiveDisposition::goto_true,{}};
            };
            Vm check(program,ShareVmProgramTag{},world.vm_memory(),{},primitive);
            if(check.start(routine_key(action),{i.id,i.id,stack},args)!=VmStatus::running)return {};
            const auto result=check.run_synchronous_check_with_registers(*threads.at(std::size_t(i.id-1)),false,100,primitive);
            if(!result.started||(result.status!=VmStatus::returned_true&&result.status!=VmStatus::returned_false))return {};
            return result.status==VmStatus::returned_true;
        };
        // Fixture BHAVs declare exactly four parameters. Production resolves
        // this count in the shared native recovery executor, tested separately.
        c.native_entry=[this,execute=c.services.execute_entry](auto r,std::uint16_t ep,std::string& error){
            const auto* e=world.find(r.id);if(!authorized||!e||e->generation!=r.generation)return false;
            auto& owner=*threads.at(std::size_t(r.id-1));
            const auto action=e->entry_points->at(ep).action;if(action<=255)return true;
            const auto found=program->routines().find(routine_key(action));
            if(found==program->routines().end()||!found->second.metadata){error="fixture parameter metadata unavailable";return false;}
            const auto count=found->second.metadata->arguments;
            if(count>owner.temps().size())return false;
            const std::vector<std::int16_t> args(owner.temps().begin(),owner.temps().begin()+count);
            return execute({r.id,r.generation},ep,0,args).has_value(); // false BHAV still completed.
        };
        services.entry_point9=[](auto)->bool{throw std::runtime_error("generic explicit-argument EP9 must not execute in native recovery");};
        // Fixture-owned physical callback tests the adapter's transport and old
        // parent receiver. The shared production4331d4 implementation, including
        // its controller gates, has real-resource fixtures in the session adapter.
        c.native_detach=[this,entry=c.native_entry](auto r,std::string& error){
            const auto* e=world.find(r.id);if(!authorized||!e||e->generation!=r.generation)return false;
            const auto parent=e->attachment?world.attachment_parent(r):std::nullopt;
            if(!entry(r,10,error)||!registry_event({r.id,r.generation},false,true))return false;
            e=world.find(r.id);if(!e)return false;
            if(e->attachment&&!world.detach(e->attachment->container,e->attachment->slot))return false;
            if(!world.declare_object_word(r.id,26,0)||!world.declare_object_word(r.id,14,-1)||
               !world.publish_native_recovery_position(r,std::nullopt,true))return false;
            VmFrame frame;frame.context.caller=r.id;
            const auto flags=world.find(r.id)->object_words.at(8);
            return world.write(frame,3,8,std::int16_t(std::uint16_t(flags)&0xfeffU))&&
                (!parent||entry(*parent,9,error));
        };
        c.clear_use_count=[this](auto r,std::string&){VmFrame f;f.context={r.id,r.id,0};return world.bind_context_live(f.context)&&world.write(f,3,62,0);};
        c.native_offset=[this](auto r)->std::optional<SavedRecoveryNativeOffset>{const auto e=world.find(r.id);const auto at=offsets.find(r.id);return e&&e->generation==r.generation&&at!=offsets.end()?std::optional<SavedRecoveryNativeOffset>{at->second}:std::nullopt;};
        c.restore_group=[this](auto leader,auto target,std::string&){if(!world.is_live_reference(leader)||!world.is_live_reference(target))return false;offsets={{1,{0,0,0}},{2,{16,0,0}}};return true;};
        return c;
    }
    bool run(SavedRecoveryObjectAdapter& adapter,Phase phase,std::int16_t n=1,std::int16_t target=1){std::string why;
        const bool ok=adapter.execute({phase,reference(n),reference(target),false,{}},why);
        if(!ok)std::cerr<<"phase "<<unsigned(phase)<<": "<<why<<'\n';return ok;}
};
void ordinary(){Fixture f;SavedRecoveryObjectAdapter adapter(f.context());std::string why;
    require(adapter.preflight(f.profiles,why),"actual ordinary services preflight");const auto original=f.reference(1);
    require(f.run(adapter,Phase::capture_placement)&&f.run(adapter,Phase::remove_entry)&&f.run(adapter,Phase::detach),"real EP10 and positioning detach");
    require(!f.world.find(1)->position&&f.world.find(1)->object_words.at(41)==415&&f.remove_calls[1]==2&&f.placement_calls[1]==0,"native detach transports its own EP10 and does not fabricate a removed-body EP9");
    require(f.run(adapter,Phase::before_clear)&&f.run(adapter,Phase::restore_placement)&&f.run(adapter,Phase::placement_entry),"real source restore and deferred EP9");
    require(f.world.resolve_reference(original)&&f.world.find(1)->position==std::optional<EntityPosition>(EntityPosition{24,24,1,0})&&
        f.placement_calls[1]==1&&f.world.find(1)->attributes[0]==1&&f.registry.complete,"ordinary restore uses real World/registry and EP9 exactly once");
    require(f.world.declare_object_word(1,8,0x20)&&f.world.declare_object_word(1,62,3)&&f.run(adapter,Phase::cleanup_flags)&&
        f.world.find(1)->object_words.at(8)==0&&f.world.find(1)->object_words.at(62)==0,"Cleanup publishes occupied flag and verified actual UseCount62 owner");
}
void always_reset_actual_class(){
    Fixture f(false,false,0,{},0x3c08965fU);
    for(auto& profile:f.profiles){profile.kind=SavedRecoveryClass::special_cursor;profile.code_changed=false;}
    SavedRecoveryObjectAdapter adapter(f.context());std::string why;const auto original=f.reference(1);
    require(adapter.preflight(f.profiles,why),"exact always-reset ordinary definition binds real shared class services");
    require(f.run(adapter,Phase::capture_placement)&&f.run(adapter,Phase::detach)&&!f.world.find(1)->position&&
        f.remove_calls[1]==1,"shared native detach executes actual EP10 for selected ordinary object");
    require(f.run(adapter,Phase::before_clear)&&f.run(adapter,Phase::restore_placement)&&f.run(adapter,Phase::placement_entry)&&
        f.world.is_live_reference(original)&&f.registry.complete&&f.placement_calls[1]==1&&f.world.find(1)->attributes[0]==1,
        "always-reset object uses real placement registry, original identity and authored EP9 effects");
    require(f.world.declare_object_word(1,8,32)&&f.world.declare_object_word(1,62,5)&&
        f.run(adapter,Phase::cleanup_flags)&&f.world.find(1)->object_words.at(8)==0&&f.world.find(1)->object_words.at(62)==0,
        "same selected object uses actual shared Cleanup flags and UseCount consumer");
    Fixture cursor_class(false,false,0,{},0x3c08965fU,9);
    for(auto& profile:cursor_class.profiles)profile.kind=SavedRecoveryClass::special_cursor;
    SavedRecoveryObjectAdapter unsupported(cursor_class.context());
    require(!unsupported.preflight(cursor_class.profiles,why)&&cursor_class.remove_calls.empty(),
        "same GUID with unproved Cursor type rejects before native callbacks instead of claiming ordinary capability");
}

void containment(){Fixture f;require(f.registry_event(f.identity(1),false,true)&&f.world.place_in_slot(2,0,1)&&
    f.world.declare_object_word(1,26,2)&&f.world.declare_object_word(1,14,1)&&f.registry_event(f.identity(1),true,true,f.room(1)),
    "real native reciprocal container hierarchy and actual moved registry");
    SavedRecoveryObjectAdapter adapter(f.context());std::string why;require(adapter.preflight(f.profiles,why),"contained native snapshot proof");
    require(f.run(adapter,Phase::capture_placement)&&f.run(adapter,Phase::remove_entry)&&f.run(adapter,Phase::detach)&&
        f.placement_calls[1]==0&&f.placement_calls[2]==1&&f.run(adapter,Phase::restore_placement)&&f.run(adapter,Phase::placement_entry),"contained native detach notifies its old parent before restoration");
    require(f.world.find(1)->attachment&&f.world.find(1)->attachment->container==2&&f.world.find(1)->attachment->slot==0&&
        f.world.find(1)->object_words.at(26)==2&&f.world.find(1)->object_words.at(14)==1&&f.placement_calls[1]==2&&f.placement_calls[2]==1,
        "exact original slot/parent restored; native detach's EP9 receiver is the old parent");
}
void portals(){Fixture f(true);SavedRecoveryObjectAdapter adapter(f.context());std::string why;
    require(adapter.preflight(f.profiles,why)&&f.registry.rooms[1].portals.size()==1&&f.registry.rooms[2].portals.size()==1,"real portal registry initially linked");
    require(f.run(adapter,Phase::capture_placement)&&f.run(adapter,Phase::portal_detach)&&f.registry.rooms[1].portals.empty()&&f.registry.rooms[2].portals.empty(),"portal links removed before EP10 without removing RoomInfo entities");
    for(const auto n:{1,2})require(f.run(adapter,Phase::remove_entry,n),"group EP10 all");
    for(const auto n:{1,2})require(f.run(adapter,Phase::detach,n),"group detach real registry all");
    require(f.run(adapter,Phase::restore_group),"real current offset owner restores native fields");
    for(const auto n:{1,2})require(f.run(adapter,Phase::restore_placement,n),"group scalar/post publication all");
    for(const auto n:{1,2})require(f.run(adapter,Phase::placement_entry,n),"group native EP9 pass all");
    require(f.run(adapter,Phase::portal_restore)&&f.run(adapter,Phase::portal_reconcile)&&f.registry.complete&&
        f.registry.rooms[1].portals.size()==1&&f.registry.rooms[2].portals.size()==1&&
        f.registry.rooms[1].boundaries_known[0]&&f.registry.rooms[2].boundaries_known[0]&&f.placement_calls[1]==2&&f.placement_calls[2]==2,
        "actual portal indices boundaries positions and callback counts restored without duplication");
}
void portal_order(){Fixture f(true,true);SavedRecoveryObjectAdapter adapter(f.context());std::string why;
    require(adapter.preflight(f.profiles,why)&&f.run(adapter,Phase::portal_detach)&&f.run(adapter,Phase::portal_restore),"actual reordered portal group publication");
    require(f.registry.rooms[1].portals.size()==2&&f.registry.rooms[1].portals[0].owner.id==2&&f.registry.rooms[1].portals[1].owner.id==1,
        "portal list append preserves authenticated native group order rather than numeric-ID set order");
    f.regrouped=true;require(f.run(adapter,Phase::portal_reconcile)&&f.registry.rooms[1].portals.size()==1&&
        f.registry.rooms[1].portals.front().owner.id==2&&f.registry.rooms[2].portals.front().owner.id==3,
        "finally reconciles regrouping by final Init AFTER successful portal_restore");
}
void failures(){Fixture f;auto context=f.context();context.services.change.register_position={};SavedRecoveryObjectAdapter missing(std::move(context));std::string why;
    require(!missing.preflight(f.profiles,why)&&f.remove_calls.empty(),"missing actual registration owner rejected before effects");
    auto original=f.context();original.clear_use_count={};SavedRecoveryObjectAdapter no_use(std::move(original));f.profiles[0].has_cleanup=true;
    require(!no_use.preflight(f.profiles,why),"missing actual use owner cannot be accepted by raw unrelated word writes");
    f.profiles[0].has_cleanup=false;SavedRecoveryObjectAdapter adapter(f.context());require(adapter.preflight(f.profiles,why),"valid before source invalidation");
    f.authorized=false;require(!adapter.execute({Phase::capture_placement,f.reference(1),f.reference(1),false,{}},why),"expired actual source authority rejects execution");
    Fixture q;SavedRecoveryObjectAdapter exact(q.context());require(exact.preflight(q.profiles,why),"generation source proof");const auto old=q.reference(1);
    require(q.world.remove(1)&&q.world.create_runtime_object(1,100,{1,0,2},q.entries(false))&&
        !exact.execute({Phase::remove_entry,old,old,false,{}},why)&&q.remove_calls.empty()&&q.world.find(1)->attributes[0]==0,
        "same numeric ID replacement cannot receive original recovery entry");
    Fixture architectural;require(architectural.world.declare_object_word(1,40,std::int16_t(0x8000)),"actual architectural flag input");
    SavedRecoveryObjectAdapter no_wall(architectural.context());require(!no_wall.preflight(architectural.profiles,why),
        "missing actual architectural wall-style owner is rejected instead of accepted no-op");
}
void native_entries(){
    {Fixture f;auto context=f.context();context.native_entry={};
        require(bool(context.services.execute_entry),"legacy executor exists for missing-native negative case");
        SavedRecoveryObjectAdapter adapter(std::move(context));std::string why;
        require(!adapter.preflight(f.profiles,why)&&f.remove_calls.empty()&&f.placement_calls.empty(),
            "generic explicit-argument executor cannot satisfy native NULL-argument preflight");}
    {Fixture f;auto context=f.context();context.services.execute_entry={};
        SavedRecoveryObjectAdapter adapter(std::move(context));std::string why;
        require(adapter.preflight(f.profiles,why)&&f.run(adapter,Phase::capture_placement)&&f.run(adapter,Phase::remove_entry)&&
            f.run(adapter,Phase::detach)&&f.run(adapter,Phase::restore_placement)&&f.run(adapter,Phase::placement_entry)&&
            f.remove_calls[1]==2&&f.placement_calls[1]==1,
            "native EP10/EP9 remain bound to receiver TEMP defaults with generic executor absent");}
    {Fixture f;auto context=f.context();context.native_detach={};SavedRecoveryObjectAdapter adapter(std::move(context));std::string why;
        require(!adapter.preflight(f.profiles,why)&&f.remove_calls.empty()&&f.placement_calls.empty(),
            "missing native RemoveFromWorld cannot fall back to generic Pre/null/Post");}
    for(const bool expire:{false,true}){Fixture f;auto context=f.context();
        context.native_entry=[&](auto r,std::uint16_t ep,std::string& why){
            require(r==f.reference(1)&&ep==10,"native callback receives exact original target and entry");
            require(f.world.declare_object_word(1,41,901),"native failed entry retains its real completed World effect");
            if(expire){f.authorized=false;return true;}
            why="native entry deliberately incomplete";return false;
        };
        SavedRecoveryObjectAdapter adapter(std::move(context));std::string why;
        require(adapter.preflight(f.profiles,why)&&!f.run(adapter,Phase::remove_entry)&&f.world.find(1)->object_words.at(41)==901&&
            f.remove_calls.empty()&&f.placement_calls.empty(),
            "incomplete or expired native entry retains earlier effects without falling back to explicit zero arguments");}
}
}
namespace {
void placement_entry_order(){
    for(const auto master:{std::uint16_t{0},std::uint16_t{7}})for(const bool group:{false,true})for(const bool contained:{false,true}){
        Fixture f(false,false,master);f.profiles[0].native_group=group;
        if(contained)require(f.registry_event(f.identity(1),false,true)&&f.world.place_in_slot(2,0,1)&&
            f.world.declare_object_word(1,26,2)&&f.world.declare_object_word(1,14,1)&&f.registry_event(f.identity(1),true,true,f.room(1)),"entry-order real containment");
        require(f.world.declare_object_word(1,40,0x400),"entry-order actual F2 control");
        auto context=f.context();std::vector<std::string> events;
        context.native_entry=[&,entry=context.native_entry](auto ref,std::uint16_t ep,std::string& error){if(ep==9&&ref==f.reference(1))events.push_back("ep9");return entry(ref,ep,error);};
        context.native_position_finished=[&](auto ref,std::string&){events.push_back("f2");return f.world.declare_object_word(ref.id,41,942);};
        SavedRecoveryObjectAdapter adapter(std::move(context));std::string why;
        require(adapter.preflight(f.profiles,why)&&f.run(adapter,Phase::capture_placement)&&f.run(adapter,Phase::detach),"entry-order native capture/detach");
        events.clear();require(f.run(adapter,Phase::restore_placement)&&f.run(adapter,Phase::placement_entry),"entry-order native restoration");
        std::vector<std::string> expected((master==0?1:0)+(contained?1:0),"ep9");expected.push_back("f2");if(group)expected.push_back("ep9");
        require(events==expected&&f.world.find(1)->object_words.at(41)==942,"3333ef conditional EP9 then F2 before separate MT placement-entry pass");
    }
    for(const bool expired:{false,true}){
        Fixture f;auto context=f.context();
        context.native_entry=[&,entry=context.native_entry](auto ref,std::uint16_t ep,std::string& error){
            if(!entry(ref,ep,error))return false;
            return ep!=9||f.world.declare_object_word(ref.id,40,0x400);
        };
        if(expired)context.native_position_finished=[&](auto,std::string&){f.authorized=false;return true;};
        SavedRecoveryObjectAdapter adapter(std::move(context));std::string why;
        require(adapter.preflight(f.profiles,why)&&f.run(adapter,Phase::capture_placement)&&f.run(adapter,Phase::detach)&&
            !f.run(adapter,Phase::restore_placement)&&f.world.find(1)->object_words.at(40)==0x400,
            "late F2 activation requires real owner and successful event must retain source authority");
    }
}
void native_group_offset_units(){
    // Three current pieces; each selected target can be a non-head member.
    // Translating all tile offsets by a signed origin must cancel exactly.
    for(const auto base:{-7,5})for(const std::int16_t target:{1,2,3}){
        Fixture f(false,true);for(auto& profile:f.profiles)profile.native_group=true;
        auto convert=[](NativeObjectGroupOffset tiles){std::string error;auto value=saved_recovery_offset_from_native_tiles(tiles,error);require(value.has_value(),"actual tile-to-World binding conversion");return *value;};
        f.offsets={{1,convert({base,base,0})},{2,convert({base,base+1,0})},{3,convert({base+1,base,0})}};
        std::map<std::int16_t,EntityPosition> original;
        for(const std::int16_t n:{1,2,3})original[n]=*f.world.find(n)->position;
        SavedRecoveryObjectAdapter adapter(f.context());std::string why;
        require(adapter.preflight(f.profiles,why)&&f.run(adapter,Phase::capture_placement,target,target),"capture selected native group target");
        for(const std::int16_t n:{1,2,3})require(f.run(adapter,Phase::detach,n,target),"detach actual offset cohort");
        for(const std::int16_t n:{1,2,3})require(f.run(adapter,Phase::restore_placement,n,target),"restore current tile offsets in World units");
        for(const std::int16_t n:{1,2,3})require(f.world.find(n)->position==std::optional<EntityPosition>{original[n]},
            "signed XY tile deltas scale by16 independently of selected head/member or offset origin");
    }
    {Fixture f;for(auto& profile:f.profiles)profile.native_group=true;
        require(f.registry_event(f.identity(2),false,true)&&f.world.set_position(2,{40,24,2,0})&&f.registry_event(f.identity(2),true,true,f.room(2)),"actual upper-floor group piece");
        std::string why;const auto upper=saved_recovery_offset_from_native_tiles({1,0,1},why);require(upper.has_value(),"upper-floor binding conversion");f.offsets[2]=*upper;SavedRecoveryObjectAdapter adapter(f.context());
        require(adapter.preflight(f.profiles,why)&&f.run(adapter,Phase::capture_placement),"capture level-offset source");
        for(const std::int16_t n:{1,2})require(f.run(adapter,Phase::detach,n),"detach level-offset source");
        for(const std::int16_t n:{1,2})require(f.run(adapter,Phase::restore_placement,n),"restore floor delta without XY scale");
        require(f.world.find(2)->position==std::optional<EntityPosition>{EntityPosition{40,24,2,0}},"level offset remains one floor, not sixteen");
    }
    for(const auto tiles:{NativeObjectGroupOffset{134217727,-134217728,3},NativeObjectGroupOffset{-134217728,134217727,-2}}){
        std::string why;const auto converted=saved_recovery_offset_from_native_tiles(tiles,why);
        require(converted&&converted->x==std::int64_t(tiles.x)*16&&converted->y==std::int64_t(tiles.y)*16&&converted->level==tiles.level,
            "binding preserves full signed32 World offset range and unscaled levels");
    }
    for(const auto tiles:{NativeObjectGroupOffset{134217728,0,0},NativeObjectGroupOffset{0,-134217729,0},
            NativeObjectGroupOffset{std::numeric_limits<std::int32_t>::max(),0,0},NativeObjectGroupOffset{0,std::numeric_limits<std::int32_t>::min(),0}}){
        std::string why;require(!saved_recovery_offset_from_native_tiles(tiles,why)&&!why.empty(),"unrepresentable tile conversion rejects without truncation");
    }
    // Adapter World-unit INT32 endpoints exercise wide subtraction. Unknown
    // owners and positions outside the existing bounded World remain errors.
    for(const unsigned failure:{0U,1U,2U,3U,4U}){
        Fixture f(true);SavedRecoveryObjectAdapter adapter(f.context());std::string why;
        require(adapter.preflight(f.profiles,why)&&f.run(adapter,Phase::capture_placement)&&f.run(adapter,Phase::detach,2),"capture overflow/unknown offset case");
        if(failure==0)f.offsets[2].x=std::numeric_limits<std::int32_t>::max();
        if(failure==1){f.offsets[1].x=std::numeric_limits<std::int32_t>::min();f.offsets[2].x=std::numeric_limits<std::int32_t>::max();}
        if(failure==2){f.offsets[1].y=std::numeric_limits<std::int32_t>::max();f.offsets[2].y=std::numeric_limits<std::int32_t>::min();}
        if(failure==3)f.offsets[2].level=std::numeric_limits<std::int32_t>::max();
        if(failure==4)f.offsets.erase(2);
        require(!adapter.execute({Phase::restore_placement,f.reference(2),f.reference(1),false,{}},why)&&!why.empty()&&
            !f.world.find(2)->position&&f.placement_calls[2]==0,"unsupported current offsets never wrap or publish a fabricated placement");
    }
}
void native_placement_binding(){for(const bool seat:{false,true}){
    Fixture f(false,true,0,seat);auto context=f.context();
    require(f.world.set_position(1,{24,24,1,4}),"seat fixture retains actual source facing");
    context.position.position_phase=[&](auto who,const auto&,auto,const auto&,const std::function<bool()>& operation){
        return f.authorized&&f.world.is_live_reference({who.id,who.generation})&&operation();
    };
    context.position.slot_phase=[&](auto child,auto parent,std::size_t,auto,const std::function<bool()>& operation){
        return f.authorized&&f.world.is_live_reference({child.id,child.generation})&&f.world.is_live_reference({parent.id,parent.generation})&&operation();
    };
    auto floors=std::make_shared<NativeFloorHeads>(f.world,4,4,f.authority());std::string why;
    require(floors->seed({f.reference(1),f.reference(2),f.reference(3)},why),"actual whole fixture floor-head ownership");
    auto kernel=std::make_shared<NativeRecoveryPlacement>(context.position,context.services,floors);
    context.native_unlink=[kernel](auto ref,std::string& error){return kernel->unlink(ref,error);};
    context.native_restore_position=[kernel](auto ref,const auto& pose,const auto& parent,std::size_t slot,const auto& saved,std::string& error){return kernel->restore(ref,pose,parent,slot,saved,error);};
    NativeSeatingContext seating;seating.world=&f.world;seating.width=seating.height=4;seating.source_still_matches=f.authority();
    seating.floor_head=[floors](const EntityPosition& point){return floors->head(point);};
    seating.native_entry=context.native_entry;
    seating.preflight_entry=[&](auto ref,std::uint16_t ep,std::string& error){
        const auto* body=f.world.resolve_reference(ref);
        if(!f.authorized||!body||!body->entry_points||ep>=body->entry_points->size()||ref.id<=0||std::size_t(ref.id)>f.threads.size()||!f.threads[std::size_t(ref.id-1)]){
            error="actual fixture receiver Thread/function table unavailable";return false;
        }
        const auto action=body->entry_points->at(ep).action;if(action<=255)return true;
        const auto routine=f.program->routines().find(routine_key(action));
        if(routine==f.program->routines().end()||!routine->second.metadata||routine->second.metadata->arguments>f.threads[std::size_t(ref.id-1)]->temps().size()){
            error="actual fixture receiver BHAV/parameter resources unavailable";return false;
        }
        return true;
    };
    unsigned centers=0,fronts=0;bool restoring=false,restored_wall=false;
    context.services.change.wall_use=[&,wall=context.services.change.wall_use](auto who,bool adding,bool exclusive){
        if(restoring&&adding&&who.id==1){
            require(centers==1&&fronts==(seat?2U:0U)&&f.placement_calls[3]==unsigned(seat)&&f.placement_calls[1]==0,
                "actual center/front updates and neighbor EP9 precede restored wall use and final root EP9");
            restored_wall=true;
        }
        return wall(who,adding,exclusive);
    };
    context.native_entry=[&,entry=context.native_entry](auto who,std::uint16_t ep,std::string& error){
        if(restoring&&who.id==1&&ep==9)require(restored_wall,"final receiver EP9 follows actual wall-use consumer");
        return entry(who,ep,error);
    };
    context.native_position_neighbors=[&,seating](auto ref,const EntityPosition& captured,bool after,std::string& error){
        auto update=[&](NativeSeatingTile point){const auto result=update_native_seating(seating,point);error=result.diagnostic;return result.completed;};
        //4333ef updates the restored center even when Sit26 is absent.
        if(after){++centers;if(!update(native_seating_center_tile(captured)))return false;}
        const auto* body=f.world.resolve_reference(ref);const auto live=f.world.native_position(ref);
        if(!f.authorized||!body||!body->entry_points||!live){error="neighbor receiver expired after center EP9";return false;}
        if(body->entry_points->size()<=26||!body->entry_points->at(26).action)return true;
        auto front_pose=captured;front_pose.level=live->level;
        const auto direction=body->object_words.find(1);if(direction==body->object_words.end()){error="actual neighbor direction unavailable";return false;}
        const auto front=native_seating_front_tile(front_pose,direction->second,error);if(!front)return false;
        ++fronts;return update(*front);
    };
    context.native_detach=[&,kernel,entry=context.native_entry](auto ref,std::string& error){
        if(!entry(ref,10,error)||!f.registry_event({ref.id,ref.generation},false,true)||!kernel->unlink(ref,error))return false;
        return f.world.publish_native_recovery_position(ref,std::nullopt,true);
    };
    SavedRecoveryObjectAdapter adapter(std::move(context));
    require(adapter.preflight(f.profiles,why)&&f.run(adapter,Phase::capture_placement)&&f.run(adapter,Phase::detach),"actual native binding captures and unlinks original floor head");
    const auto target=EntityPosition{24,24,1,4},displaced=EntityPosition{24,24,1,0};const auto prior=f.world.native_position(f.reference(2));
    require(f.registry_event(f.identity(2),false,true)&&kernel->unlink(f.reference(2),why)&&
        kernel->restore(f.reference(2),displaced,{},0,prior,why)&&f.registry_event(f.identity(2),true,true,f.room(2)),
        "actual Init-like placement occupies the saved destination using same native index");
    require(f.world.declare_object_word(1,8,0x100)&&f.world.declare_object_word(2,8,0x100),"stale center flags exercise actual whole-tree clearing");
    restoring=true;
    require(f.run(adapter,Phase::restore_placement)&&f.run(adapter,Phase::placement_entry)&&restored_wall,"adapter consumes actual native insertion, seating and scalar binding");
    require(f.world.find(1)->position==std::optional<EntityPosition>{target}&&f.world.find(2)->position==std::optional<EntityPosition>{displaced}&&
        !f.world.find(2)->attachment&&f.world.native_slot0(f.reference(1))==std::optional<WorldEntityReference>{f.reference(2)}&&
        floors->head(target)==std::optional<WorldEntityReference>{f.reference(1)}&&floors->settled(why)&&f.registry.complete,
        "destination occupant survives as native zero child with coherent actual index/registry, no fake physical attachment");
    require(centers==1&&fronts==(seat?2U:0U)&&f.world.find(1)->object_words.at(8)==0&&f.world.find(2)->object_words.at(8)==0,
        "unconditional restored-center traversal clears the entire native tree; front traversal requires Sit26");
    require(f.world.find(3)->object_words.at(8)==(seat?0x100:0)&&f.placement_calls[3]==unsigned(seat)&&f.world.find(3)->attributes[0]==std::int16_t(seat),
        "conditional front update publishes flags then runs actual receiver EP9 with Thread TEMP defaults and StackObject0");
}}

void originally_outside(){
    Fixture f;require(f.registry_event(f.identity(1),false,true)&&f.world.place_out_of_world(1,0)&&
        f.world.set_native_out_of_world_level(f.reference(1),1),"source-proven originally OOW body");
    SavedRecoveryObjectAdapter adapter(f.context());std::string why;
    require(adapter.preflight(f.profiles,why)&&f.run(adapter,Phase::capture_placement),"capture old source outside snapshot");
    const EntityPosition initialized{24,40,1,0};require(f.world.set_position(1,initialized)&&f.registry_event(f.identity(1),true,true,f.room(1)),"Init-like real placement after originally OOW capture");
    require(f.run(adapter,Phase::restore_placement)&&f.run(adapter,Phase::placement_entry)&&
        f.world.find(1)->position==std::optional<EntityPosition>{initialized}&&f.placement_calls[1]==0,
        "3206d skips old-OOW restoration and its EP9, preserving Init placement");
}
}
int main(){try{ordinary();always_reset_actual_class();containment();portals();portal_order();failures();native_entries();placement_entry_order();native_group_offset_units();native_placement_binding();originally_outside();std::cout<<"saved object recovery adapter checks passed (synthetic resources, actual native placement binding)\n";return 0;}
catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
