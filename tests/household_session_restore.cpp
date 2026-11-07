#include "odh/sha256.hpp"
#include "saved_avatar_containment_fixture.hpp"
#include "odh/avatar_person_data.hpp"
#include "odh/world_snapshot_codec.hpp"
#include "odh/household_session_restore.hpp"
#include "odh/single_resident_session_restore.hpp"
#include "odh/seated_session_continuation.hpp"
#include "saved_action_owner_cache.hpp"
#include <fstream>
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void require(bool value,const char* why){if(!value)throw std::runtime_error(why);}
template<class F>void rejects(F f,const char* why){try{f();}catch(const odh::FormatError&){return;}throw std::runtime_error(why);}
odh::RoutineSet program(){
    odh::Bhav main{0x8002,{{0,254,255,{}}},{},odh::Bhav::Metadata{0,4,4,0}};
    return {{odh::routine_key(4096),std::move(main)}};
}
odh::RoutineSet go_here_program(){
    auto result=program();
    result[odh::routine_key(4096)].instructions[0]={27,254,255,{10,0,254,255,10,0,5,0}};
    return result;
}
std::vector<odh::SessionGlobalValue> globals(const odh::LotContext& lot,const odh::Ts1BootstrapGlobals& bootstrap){
    std::vector<odh::SessionGlobalValue> result;
    for(std::int16_t i=0;i<38;++i){auto v=i==9?std::optional<std::int16_t>{21}:bootstrap.read(i);if(!v)v=lot.read(i);
        if(v)result.push_back({static_cast<std::uint16_t>(i),*v});}return result;
}
odh::RoutineOwners resident_owners(std::int16_t id,bool missing,const odh::RoutineOwners& extra){
    auto owners=missing?odh::RoutineOwners{}:odh::RoutineOwners{{static_cast<odh::CodeOwner>(id),program()}};
    owners.insert(extra.begin(),extra.end());return owners;
}
struct Execution {
    odh::EntityThreadControllers controllers;
    odh::Playback playback;
    odh::Vm vm;
    Execution(odh::World& world,std::int16_t id,odh::RandomState& rng,bool missing=false,
        const odh::RoutineOwners& extra={}):
        controllers(world,id,[&rng]{rng.next(100);}),playback(world,{}),
        vm(odh::RoutineSet{},world.vm_memory(),{},controllers.sleep_primitive(),
            resident_owners(id,missing,extra)){
        require(controllers.bind(vm)&&playback.bind(vm),"bind resident graph");}
    void schedule(odh::EntityVmScheduler& scheduler,std::int16_t id){require(scheduler.add(id,vm,
        [&](auto tick){return controllers.advance(tick);},[&]{return controllers.observe();},[&]{return controllers.notify();}),"schedule resident");}
    odh::SessionRuntimeEntity snapshot(std::int16_t id){odh::PlaybackSnapshot p;require(playback.capture_snapshot(p),"capture playback");
        return {id,vm.snapshot(),controllers.snapshot(),p};}
};
struct Fixture {
    odh::World world{odh::WorldLimits{},odh::AttributePolicy::ts1_bounded};
    odh::RandomState rng{0x12345678};
    odh::RelationshipState relationships;
    odh::EntityVmScheduler scheduler{4};
    odh::LotContext lot{7,std::vector<std::int16_t>(32)};
    odh::Ts1BootstrapGlobals bootstrap;
    odh::WorldSnapshot baseline;
    odh::SessionHostState selection;
    odh::SessionResourceIdentity resources;
    std::vector<odh::SessionHouseholdMember> roster;
    std::unique_ptr<Execution> first,second;
    std::uint64_t tick=0;
    explicit Fixture(std::size_t motive_words=8){
        for(std::int16_t id:{1,2}){require(world.create(id,odh::EntityKind::avatar,{1,88,1,motive_words})&&
            world.define_type(id,static_cast<std::uint32_t>(123450+id)),"create avatar");
            require(world.declare_definition_word(id,20,static_cast<std::int16_t>(10+id))&&
                world.define_entry_points(id,{{0,4096}}),"define avatar");
            odh::SessionHouseholdMember m;m.id=id;m.generation=world.find(id)->generation;
            m.guid=static_cast<std::uint32_t>(123450+id);m.neighbor_id=static_cast<std::int16_t>(40+id);
            m.object_definition_id=static_cast<std::uint16_t>(120+id);
            m.character_asset="UserData/Characters/User0000"+std::to_string(id)+".iff";
            m.character_fingerprint.fill(static_cast<std::uint8_t>(id));roster.push_back(m);}
        baseline=world.snapshot();resources.resource_fingerprint.fill(0x5a);
        selection.neighborhood_mode=1;selection.house_number=7;selection.family_chunk_id=21;selection.family_number=12;
        selection.resident_guid=123451;selection.resident_neighbor_id=41;selection.runtime_avatar_id=1;
        require(lot.write(12,321),"mutable global");
        require(relationships.import_object_relationships(world,{{1,{{2,{12,-9}}}},{2,{{1,{33,7}}}}}),"relationships");
        first=std::make_unique<Execution>(world,1,rng);second=std::make_unique<Execution>(world,2,rng);
        first->schedule(scheduler,1);second->schedule(scheduler,2);
    }
    void start(){for(const auto& e:std::vector<std::pair<std::int16_t,Execution*>>{{1,first.get()},{2,second.get()}})
        require(e.second->vm.start(odh::routine_key(4096),{e.first,e.first,e.first,static_cast<odh::CodeOwner>(e.first)},
            {static_cast<std::int16_t>(e.first+2)})==odh::VmStatus::running,"start resident Main");}
    void step(){require(scheduler.tick(++tick,16,8),"household tick");}
    odh::SessionBundle snapshot(){odh::SessionBundle b;b.resources=resources;b.host=selection;b.host.tick=tick;b.host.random_seed=rng.seed();
        b.host.global_values=globals(lot,bootstrap);b.host.family_budget=4321;
        b.household_roster=roster;b.world=world.snapshot();b.relationships=relationships.snapshot();
        b.runtime_entities={first->snapshot(1),second->snapshot(2)};b.scheduler=scheduler.snapshot();return b;}
};
auto prepare(const std::vector<std::uint8_t>& bytes,Fixture& f){return odh::HouseholdSessionRestore::prepare(bytes,f.resources,f.selection,
    f.roster,f.baseline,f.lot,f.bootstrap,21);}
odh::SessionBundle go_here_bundle(Fixture& f,std::int16_t id=1,std::int16_t marker=3){
    auto b=f.snapshot();const odh::CodeOwner owner=0x80000000U;
    odh::World world;require(world.restore_snapshot(b.world),"restore fixture World");
    require(world.create(marker,odh::EntityKind::object,{2,0,1,0}),"create Destination marker");
    require(world.define_type(marker,0x7c4)&&world.set_ui_ghost(marker,true)&&
        world.set_static_footprint(marker,false)&&world.set_position(marker,{8,88,1,0}),"define exact Destination marker");
    b.world=world.snapshot();auto& state=b.runtime_entities[static_cast<std::size_t>(id-1)];
    auto& vm=state.vm;vm.status=odh::VmStatus::waiting;vm.ticket_sequence=1;vm.steps=2;
    vm.stack={{odh::routine_key(4096),0,{id,id,marker,owner},std::vector<std::int16_t>(4),
        std::vector<std::int16_t>(4),odh::VmCallReturnMode::ordinary,0}};
    const odh::Instruction instruction{27,254,255,{10,0,254,255,10,0,5,0}};
    vm.pending=odh::VmPending{1,odh::VmTrace{1,vm.stack.size(),odh::routine_key(4096),0,instruction,owner},{id,id,marker,owner}};
    state.route.emplace();auto& route=*state.route;route.ticket=1;route.actor=id;route.target=marker;
    route.actor_generation=world.find(id)->generation;route.target_generation=world.find(marker)->generation;
    route.run_generation=vm.run_generation;route.expected=*world.find(id)->position;
    route.target_position=*world.find(marker)->position;route.expected_radian_direction=world.find(id)->radian_direction;
    route.frame.state=odh::RouteFrameState::initial;route.path={{route.expected.x,route.expected.y}};
    route.next=1;route.entry_flags=0;route.facing=2;route.same_room_verified=true;route.call_failure_trees=true;
    odh::SessionDynamicActionOwner binding;binding.owner=owner;binding.target_id=marker;
    binding.target_generation=world.find(marker)->generation;binding.target_guid=0x7c4;
    binding.source="Objects.far";binding.asset="Destination.iff";binding.definition_chunk_id=1;
    binding.tree_table_id=1;binding.root_routine=4096;
    const auto routines=go_here_program();binding.routine_fingerprint=odh::session_action_routine_fingerprint(routines);
    state.dynamic_action_owners={binding};
    state.controllers.interactions.actor_generation=world.find(id)->generation;
    state.controllers.interactions.run_generation=vm.run_generation;
    for(auto& runtime:b.runtime_entities){const auto* avatar=world.find(runtime.id);
        runtime.controllers.interactions.actor_generation=avatar->generation;
        runtime.controllers.interactions.run_generation=runtime.vm.run_generation;}
    auto scheduled=std::find_if(b.scheduler.entities.begin(),b.scheduler.entities.end(),[&](const auto& e){return e.id==id;});
    require(scheduled!=b.scheduler.entities.end(),"routed resident scheduler entry");scheduled->status=odh::VmStatus::waiting;
    scheduled->steps=vm.steps;scheduled->run_generation=vm.run_generation;
    require(vm.status==odh::VmStatus::waiting&&vm.pending,"synthetic VM wait exists");
    require(vm.pending->ticket==route.ticket&&route.ticket<=vm.ticket_sequence,"synthetic route ticket matches");
    require(route.actor==state.id&&route.target!=state.id,"synthetic route ids match");
    require(route.actor_generation==world.find(id)->generation&&route.run_generation==vm.run_generation,
        "synthetic route generations match");
    require(route.same_room_verified&&route.expected.level==route.target_position.level,
        "synthetic route room and level match");
    const auto* snapshot_actor=world.find(state.id);
    require(snapshot_actor&&snapshot_actor->generation==route.actor_generation,
        "synthetic Go Here actor generation matches World");
    return b;
}
odh::SessionBundle local_route_bundle(Fixture& f,std::int16_t id){
    auto b=f.snapshot();const auto target=static_cast<std::int16_t>(id==1?2:1);
    odh::World world;require(world.restore_snapshot(b.world),"restore local Route45 fixture World");
    require(world.set_position(id,{5,12,1,2})&&world.set_position(target,{28,12,1,0}),
        "place local Route45 participants");
    b.world=world.snapshot();auto& state=b.runtime_entities[static_cast<std::size_t>(id-1)];
    auto& vm=state.vm;vm.status=odh::VmStatus::waiting;vm.ticket_sequence=1;vm.steps=2;
    const odh::VmContext context{id,target,target,static_cast<odh::CodeOwner>(id)};
    vm.stack={{odh::routine_key(4096),0,context,std::vector<std::int16_t>(4),
        std::vector<std::int16_t>(4),odh::VmCallReturnMode::ordinary,0}};
    const odh::Instruction instruction{45,254,255,{1,0,2,0,0,0,0,0}};
    vm.pending=odh::VmPending{1,odh::VmTrace{1,1,odh::routine_key(4096),0,instruction,
        static_cast<odh::CodeOwner>(id)},context};
    state.route.emplace();auto& route=*state.route;route.ticket=1;route.actor=id;route.target=target;
    route.actor_generation=world.find(id)->generation;route.target_generation=world.find(target)->generation;
    route.run_generation=vm.run_generation;route.expected=*world.find(id)->position;
    route.target_position=*world.find(target)->position;route.expected_radian_direction=world.find(id)->radian_direction;
    route.frame.state=odh::RouteFrameState::initial;route.path={{5,12},{6,12}};
    route.next=1;route.entry_flags=4;route.facing=2;route.same_room_verified=true;
    for(auto& runtime:b.runtime_entities){
        runtime.controllers.interactions.actor_generation=world.find(runtime.id)->generation;
        runtime.controllers.interactions.run_generation=runtime.vm.run_generation;
    }
    auto scheduled=std::find_if(b.scheduler.entities.begin(),b.scheduler.entities.end(),
        [&](const auto& entity){return entity.id==id;});
    scheduled->status=vm.status;scheduled->steps=vm.steps;scheduled->run_generation=vm.run_generation;
    return b;
}
void move_to_route_stack(odh::SessionRuntimeEntity& state) {
    require(state.route&&state.vm.pending&&!state.vm.stack.empty(),"route stack fixture has a pending local route");
    odh::RouteJobSnapshot job;static_cast<odh::LocalRouteSnapshot&>(job)=*state.route;
    job.pending=*state.vm.pending;job.route_frame=state.vm.stack.back();
    state.route_stack.emplace();state.route_stack->jobs.push_back(std::move(job));state.route.reset();
}
void route_stack_profile_tests() {
    for(const auto id:{std::int16_t{1},std::int16_t{2}}) {
        Fixture f;f.start();auto nested=local_route_bundle(f,id);
        auto& state=nested.runtime_entities[static_cast<std::size_t>(id-1)];
        move_to_route_stack(state);
        auto& parent=state.route_stack->jobs.front();
        parent.external_frame_active=true;parent.external_frame_owner=static_cast<odh::CodeOwner>(id);
        parent.external_frame_call=odh::VmCall{parent.route_frame.key,parent.route_frame.context,
            parent.route_frame.arguments};parent.frame.state=odh::RouteFrameState::room_portal;
        state.vm.suspended_pending.push_back(*state.vm.pending);
        auto child=parent;child.ticket=2;child.external_frame_active=false;child.external_frame_owner=0;
        child.external_frame_call.reset();child.frame.state=odh::RouteFrameState::walking;child.walking_started=true;
        child.pending.ticket=2;child.pending.instruction.depth=2;child.pending.instruction.step=2;
        child.route_frame.return_mode=odh::VmCallReturnMode::external_frame_next_tick;
        child.route_frame.invocation_ticket=1;
        state.vm.stack.push_back(child.route_frame);state.vm.pending=child.pending;
        state.vm.steps=3;state.vm.ticket_sequence=2;
        state.route_stack->jobs.push_back(child);
        auto actor=std::find_if(nested.world.entities.begin(),nested.world.entities.end(),[&](const auto& value){return value.id==id;});
        actor->static_footprint=false;
        auto scheduled=std::find_if(nested.scheduler.entities.begin(),nested.scheduler.entities.end(),[&](const auto& value){return value.id==id;});
        scheduled->steps=state.vm.steps;
        const auto bytes=odh::encode_session_bundle(nested);const auto seed=f.rng.seed();
        const auto candidate=prepare(bytes,f);
        require(candidate->route_stack(id)&&candidate->route_stack(id)->jobs.size()==2&&
            !candidate->world().find(id)->static_footprint&&f.rng.seed()==seed,
            "household restore admits exact nested VM waits and dynamic footprint for either resident without effects");
        auto returned=nested;auto& runtime=returned.runtime_entities[static_cast<std::size_t>(id-1)];
        runtime.route_stack->jobs.pop_back();runtime.vm.stack.pop_back();runtime.vm.pending=runtime.vm.suspended_pending.back();
        runtime.vm.suspended_pending.clear();runtime.vm.status=odh::VmStatus::yielded;
        runtime.vm.external_frame_return=odh::VmExternalFrameReturn{1,true,static_cast<odh::CodeOwner>(id),runtime.vm.run_generation};
        returned.world.entities[static_cast<std::size_t>(id-1)].static_footprint=true;
        auto wake=std::find_if(returned.scheduler.entities.begin(),returned.scheduler.entities.end(),[&](const auto& value){return value.id==id;});
        wake->status=runtime.vm.status;
        const auto after=prepare(odh::encode_session_bundle(returned),f);
        require(after->route_stack(id)&&after->route_stack(id)->jobs.front().external_frame_active,
            "household restore admits the owned portal return after the nested child has popped");
        auto unowned=nested;unowned.runtime_entities[static_cast<std::size_t>(id-1)].route_stack.reset();
        rejects([&]{prepare(odh::encode_session_bundle(unowned),f);},
            "unowned external frame cannot inherit route-stack restore permissions");
    }
    Fixture cache;cache.start();cache.step();auto idle=cache.snapshot();idle.runtime_entities.front().route_stack.emplace();
    idle.runtime_entities.front().route_stack->portal_planned_calls=1;
    const auto restored=prepare(odh::encode_session_bundle(idle),cache);
    require(restored->route_stack(1)&&restored->route_stack(1)->jobs.empty(),
        "cache-only host continuation preserves ordinary resident restore policy");
}
void resume(odh::HouseholdSessionRestore& candidate,const odh::SessionBundle& expected,bool missing=false,bool partial=false,bool aliased=false){
    odh::RandomState rng{999};std::uint64_t tick=999;
    Execution a(candidate.world(),1,rng),b(candidate.world(),2,rng,missing);odh::EntityVmScheduler scheduler{4};
    a.schedule(scheduler,1);b.schedule(scheduler,2);
    std::vector<odh::SessionExecutionBinding> bindings{{1,&a.vm,&a.controllers,&a.playback,{}},{2,&b.vm,&b.controllers,&b.playback,{}}};
    if(partial)bindings.pop_back();if(aliased)bindings[1].vm=&a.vm;
    if(missing||partial||aliased){rejects([&]{candidate.restore_execution(bindings,scheduler,rng,tick);},"partial execution accepted");
        require(tick==999&&rng.seed()==999,"failure published tick/RNG");
        rejects([&]{candidate.restore_execution(bindings,scheduler,rng,tick);},"failed candidate retry accepted");return;}
    candidate.restore_execution(bindings,scheduler,rng,tick);require(tick==1,"restore saved tick");
    for(;tick<6;)require(scheduler.tick(++tick,16,8),"resume household tick");
    auto actual=expected;actual.world=candidate.world().snapshot();actual.relationships=candidate.relationships().snapshot();
    actual.runtime_entities={a.snapshot(1),b.snapshot(2)};actual.scheduler=scheduler.snapshot();actual.host.tick=tick;actual.host.random_seed=rng.seed();
    actual.host.global_values=globals(candidate.lot(),candidate.bootstrap());
    require(odh::encode_session_bundle(actual)==odh::encode_session_bundle(expected),"two residents differ from continuous execution");
}
void equivalent_action_owner_cache_tests(){
    constexpr odh::CodeOwner first_owner=0x80000000U,second_owner=first_owner+1;
    Fixture f;f.start();f.step();
    require(f.first->vm.install_owner(second_owner,program()),"install queued duplicate resource owner");
    require(f.first->controllers.interactions().enqueue_source_user(odh::routine_key(4096),
        {1,2,2,second_owner},std::nullopt,0),"queue action with independent owner identity");
    auto saved=f.snapshot();saved.generic_execution=true;saved.next_code_owner=second_owner+1;
    auto& actor=saved.runtime_entities.front();
    for(auto& frame:actor.vm.stack)frame.context.code_owner=first_owner;
    actor.vm.pending->context.code_owner=first_owner;
    actor.vm.pending->instruction.code_owner=first_owner;
    odh::SessionResourceCodeOwner first;
    first.owner=first_owner;first.resource_guid=98765;first.source="Objects.far";
    first.asset="Synthetic.iff";first.definition_chunk_id=1;first.tree_table_id=1;
    first.root_routine=4096;first.bank_kind=odh::SessionCodeBankKind::complete_private;
    first.routine_fingerprint=odh::session_loaded_bank_fingerprint(program());
    auto second=first;second.owner=second_owner;actor.resource_owners={first,second};
    auto candidate=prepare(odh::encode_session_bundle(saved),f);
    odh::RandomState rng{999};std::uint64_t tick=999;
    Execution a(candidate->world(),1,rng),b(candidate->world(),2,rng);
    odh::EntityVmScheduler scheduler{4};a.schedule(scheduler,1);b.schedule(scheduler,2);
    std::size_t loads=0;
    candidate->install_resource_owners(1,a.vm,[&](const auto&){++loads;
        return std::make_pair(program(),std::optional<odh::SessionLinkedSemiglobal>{});});
    candidate->restore_execution({{1,&a.vm,&a.controllers,&a.playback,{}},
        {2,&b.vm,&b.controllers,&b.playback,{}}},scheduler,rng,tick);
    const auto first_bank=a.vm.shared_program_for_owner(first_owner);
    const auto second_bank=a.vm.shared_program_for_owner(second_owner);
    require(loads==2&&first_bank&&second_bank&&first_bank!=second_bank&&
        odh_host::equivalent_installed_action_owners(first,second,a.vm),
        "separately restored resource owners prove equivalent from actual installed programs");
    const auto uid=a.controllers.interactions().snapshot().actions.front().uid;
    using Key=std::tuple<std::string,std::string,std::uint16_t,std::uint8_t>;
    std::map<Key,odh::CodeOwner> cache;
    const Key key{first.source,first.asset,first.root_routine,1};
    cache.emplace(key,first_owner);
    const auto inserted=cache.emplace(key,second_owner);
    require(!inserted.second&&odh_host::equivalent_installed_action_owners(first,second,a.vm)&&
        inserted.first->second==first_owner&&a.vm.shared_program_for_owner(first_owner)==first_bank&&
        a.vm.shared_program_for_owner(second_owner)==second_bank&&
        a.vm.stack().front().context.code_owner==first_owner&&
        a.vm.pending()->context.code_owner==first_owner&&
        a.controllers.interactions().snapshot().actions.front().call.context.code_owner==second_owner&&
        a.controllers.interactions().snapshot().actions.front().uid==uid&&
        candidate->resource_owners(1).size()==2,
        "cache canonicalization preserves both banks, manifests, queued UID and executing frame owner");
    const auto incompatible=[&](const auto& changed){
        require(!odh_host::equivalent_installed_action_owners(first,changed,a.vm),
            "incompatible or uninstalled cache bank must not be reused");};
    auto changed=second;changed.routine_fingerprint[0]^=1;incompatible(changed);
    changed=second;changed.bank_kind=odh::SessionCodeBankKind::private_closure;incompatible(changed);
    changed=second;changed.linked_semiglobal=odh::SessionLinkedSemiglobal{"x","Global.far","x.iff"};incompatible(changed);
    changed=second;changed.identity=odh::SessionCodeOwnerIdentity::resource_guid;incompatible(changed);
    changed=second;changed.resource_guid++;incompatible(changed);
    changed=second;changed.source="Other.far";incompatible(changed);
    changed=second;changed.definition_chunk_id++;incompatible(changed);
    changed=second;changed.tree_table_id++;incompatible(changed);
    changed=second;changed.root_routine++;incompatible(changed);
    changed=second;changed.owner=second_owner+1;incompatible(changed);
    require(a.vm.install_owner(second_owner+1,go_here_program()),"install actual changed bank");
    incompatible(changed); // Matching declared digest cannot conceal different actual bytecode.
    auto missing_root=program();missing_root.emplace(odh::routine_key(4097),missing_root.begin()->second);
    missing_root.erase(odh::routine_key(4096));
    require(a.vm.install_owner(second_owner+2,missing_root),"install bank with absent root");
    changed=second;changed.owner=second_owner+2;incompatible(changed);
}
void advertisement_execution_restore_tests(){
    Fixture f;f.start();f.step();auto saved=f.snapshot();saved.generic_execution=true;
    saved.runtime_entities[0].vm.advertisement_changes=std::map<std::uint32_t,std::int16_t>{{0x10007,0},{2,4}};
    saved.runtime_entities[1].vm.advertisement_changes.emplace();
    auto candidate=prepare(odh::encode_session_bundle(saved),f);
    odh::RandomState rng{999};std::uint64_t tick=999;
    Execution a(candidate->world(),1,rng),b(candidate->world(),2,rng);odh::EntityVmScheduler scheduler{4};
    a.schedule(scheduler,1);b.schedule(scheduler,2);
    candidate->restore_execution({{1,&a.vm,&a.controllers,&a.playback,{}},{2,&b.vm,&b.controllers,&b.playback,{}}},scheduler,rng,tick);
    require(a.vm.advertisement_changes()==saved.runtime_entities[0].vm.advertisement_changes&&
        b.vm.advertisement_changes()&&b.vm.advertisement_changes()->empty(),
        "fresh household execution restores each thread dictionary, including allocated-empty state");
    require(a.vm.effective_memory().read(a.vm.stack().back(),12,7)==std::optional<std::int16_t>{0}&&
        a.vm.effective_memory().read(a.vm.stack().back(),29,2)==std::optional<std::int16_t>{4}&&
        b.vm.effective_memory().read(b.vm.stack().back(),29,2)==std::optional<std::int16_t>{0},
        "restored operand memory reads the correct thread state without cross-resident leakage");
}
void generic_footprint_state_tests(){
    Fixture f;
    require(f.world.declare_object_word(1,8,8),"declare source Flags shadow");
    f.baseline=f.world.snapshot();f.start();f.step();
    auto saved=f.snapshot();saved.generic_execution=true;
    odh::World current;require(current.restore_snapshot(saved.world)&&
        current.set_static_footprint(1,false),"source mutable footprint class");
    saved.world=current.snapshot();
    const auto bytes=odh::encode_session_bundle(saved);
    auto candidate=prepare(bytes,f);
    require(!candidate->world().find(1)->static_footprint&&
        candidate->world().find(1)->object_words.at(8)==8&&
        !saved.runtime_entities.front().route&&!saved.runtime_entities.front().route_stack,
        "GEXP preserves dynamic actor state without inferring it from Flags or an active walk");
    auto legacy=saved;legacy.generic_execution=false;
    rejects([&]{prepare(odh::encode_session_bundle(legacy),f);},"legacy profile still rejects unrepresented dynamic actor state");
    auto bad=saved;++bad.runtime_entities.front().controllers.interactions.actor_generation;
    rejects([&]{prepare(odh::encode_session_bundle(bad),f);},"mutable class does not bypass actor generation");
    bad=saved;bad.world.entities.front().kind=odh::EntityKind::object;
    rejects([&]{prepare(odh::encode_session_bundle(bad),f);},"mutable class does not bypass actor kind");
    auto returned=f.snapshot();returned.generic_execution=true;
    f.baseline.entities.front().static_footprint=false;
    require(prepare(odh::encode_session_bundle(returned),f)->world().find(1)->static_footprint,
        "GEXP preserves static class after completion even when imported baseline was dynamic");
}
void generic_execution_tests(){
    advertisement_execution_restore_tests();
    generic_footprint_state_tests();
    Fixture f;f.start();f.step();auto b=f.snapshot();b.generic_execution=true;
    {
        auto lifetime=b;
        odh::World tracked;require(tracked.restore_snapshot(lifetime.world),"history current World");
        require(tracked.create_runtime_object(3,0x4567,{},{}),"create transient object before save");
        const auto generation=tracked.find(3)->generation;
        require(tracked.retire(3),"retire transient object before save");
        lifetime.world=tracked.snapshot();
        lifetime.object_history=odh::SessionObjectHistory{true,
            {{3,generation,0x4567,"GameData/Objects/Objects.far","Transient.iff",128}},tracked.reserved_entity_ids()};
        const auto encoded=odh::encode_session_bundle(lifetime);
        auto reopened=prepare(encoded,f);
        require(reopened->object_history() && reopened->object_history()->complete &&
            reopened->object_history()->retired_created_objects.size()==1 &&
            reopened->object_history()->retired_created_objects[0].generation==generation &&
            !reopened->world().entity_id_available(3) && !reopened->world().create(3,odh::EntityKind::object),
            "created-then-deleted origin and reserved ID survive normal session restore");
        require(reopened->world().create_runtime_object(4,0x4567,{}, {}) &&
            reopened->world().find(4)->generation>generation,"reopened creation uses a fresh ID and generation");
        lifetime.object_history->complete=false;
        auto incomplete=prepare(odh::encode_session_bundle(lifetime),f);
        require(incomplete->object_history() && !incomplete->object_history()->complete &&
            !incomplete->world().entity_id_available(3),"incomplete history remains incomplete but preserves known reservations");
        require(!prepare(odh::encode_session_bundle(b),f)->object_history(),"legacy restore does not invent complete history");
    }
    constexpr odh::CodeOwner owner=0x80000000U;b.next_code_owner=owner+9;
    auto& state=b.runtime_entities.front();
    for(auto& frame:state.vm.stack)frame.context.code_owner=owner;
    state.vm.pending->context.code_owner=owner;state.vm.pending->instruction.code_owner=owner;
    odh::SessionResourceCodeOwner provenance;provenance.owner=owner;provenance.resource_guid=98765;
    provenance.source="Objects.far";provenance.asset="Synthetic.iff";provenance.definition_chunk_id=1;
    provenance.tree_table_id=1;provenance.root_routine=4096;
    provenance.routine_fingerprint=odh::session_loaded_bank_fingerprint(program());
    state.resource_owners={provenance};const auto bytes=odh::encode_session_bundle(b);
    auto missing=prepare(bytes,f);odh::RandomState rng{999};std::uint64_t tick=999;
    Execution missing_a(missing->world(),1,rng),missing_b(missing->world(),2,rng);
    odh::EntityVmScheduler missing_scheduler{4};missing_a.schedule(missing_scheduler,1);missing_b.schedule(missing_scheduler,2);
    rejects([&]{missing->restore_execution({{1,&missing_a.vm,&missing_a.controllers,&missing_a.playback,{}},
        {2,&missing_b.vm,&missing_b.controllers,&missing_b.playback,{}}},missing_scheduler,rng,tick);},
        "generic restore accepted an owner bank that was never resource verified");
    require(tick==999&&rng.seed()==999,"missing resource owner published tick or RNG");
    auto candidate=prepare(bytes,f);Execution a(candidate->world(),1,rng),c(candidate->world(),2,rng);
    odh::EntityVmScheduler scheduler{4};a.schedule(scheduler,1);c.schedule(scheduler,2);
    rejects([&]{candidate->install_resource_owners(1,a.vm,[](const auto&){return std::make_pair(go_here_program(),
        std::optional<odh::SessionLinkedSemiglobal>{});});},"generic owner accepted changed bank fingerprint");
    std::size_t loads=0;
    candidate->install_resource_owners(1,a.vm,[&](const auto& record){++loads;
        require(record.resource_guid==98765,"resource owner incorrectly inferred from its current callee");
        return std::make_pair(program(),std::optional<odh::SessionLinkedSemiglobal>{});});
    candidate->restore_execution({{1,&a.vm,&a.controllers,&a.playback,{}},{2,&c.vm,&c.controllers,&c.playback,{}}},
        scheduler,rng,tick);
    require(loads==1&&tick==1&&rng.seed()==b.host.random_seed&&candidate->next_code_owner()==owner+9&&
        a.vm.stack().front().context.code_owner==owner&&a.vm.pending()->ticket==state.vm.pending->ticket,
        "generic resource owner and pending Sleep restore without rerunning Main or resetting allocator gaps");

    // Replace one original object with a fresh generation at the same ID.
    Fixture reuse;
    require(reuse.world.create_runtime_object(3,111,{2,0,1,0},{{0,4096}}),"create original object fixture");
    reuse.baseline=reuse.world.snapshot();reuse.start();reuse.step();
    const auto old=*reuse.world.find(3);require(reuse.world.remove(3),"remove original object fixture");
    require(reuse.world.create_runtime_object(3,222,{3,0,2,0},{{0,4097}}),"reuse original ID with new constructed object");
    const auto fresh=*reuse.world.find(3);auto with_object=reuse.snapshot();with_object.generic_execution=true;
    with_object.created_objects={{3,fresh.generation,222,"Objects.far","NewObject.iff",2}};
    with_object.removed_original_objects={{3,old.generation,111}};
    with_object.deferred_removals={{3,fresh.generation}};
    const auto object_bytes=odh::encode_session_bundle(with_object);
    rejects([&]{prepare(object_bytes,reuse);},"created object accepted without an exact resource metadata resolver");
    std::size_t constructors=0;
    const odh::SessionCreatedObjectResolver metadata=[&](const auto& origin){++constructors;
        require(origin.id==3&&origin.guid==222&&origin.definition_chunk_id==2,"created object provenance changed");
        odh::World world;require(world.create_runtime_object(origin.id,origin.guid,{3,0,2,0},{{0,4097}}),
            "resource-only metadata constructor");
        auto entity=*world.find(origin.id);entity.generation=origin.generation;return entity;};
    const auto restored=odh::HouseholdSessionRestore::prepare(object_bytes,reuse.resources,reuse.selection,reuse.roster,
        reuse.baseline,reuse.lot,reuse.bootstrap,21,{},metadata);
    require(constructors==1&&restored->world().find(3)->generation==fresh.generation&&
        restored->world().find(3)->definition->guid==222&&restored->deferred_removals().size()==1,
        "verified created object replaces removed original identity and retains stable removal intent");
    rejects([&]{odh::HouseholdSessionRestore::prepare(object_bytes,reuse.resources,reuse.selection,reuse.roster,
        reuse.baseline,reuse.lot,reuse.bootstrap,21,{},[&](const auto& origin){auto value=metadata(origin);
            value.entry_points=std::vector<odh::EntryPoint>{{0,999}};return value;});},
        "created object accepted metadata from another resource constructor");
    auto wrong_removed=with_object;wrong_removed.removed_original_objects.front().guid++;
    rejects([&]{odh::HouseholdSessionRestore::prepare(odh::encode_session_bundle(wrong_removed),reuse.resources,reuse.selection,
        reuse.roster,reuse.baseline,reuse.lot,reuse.bootstrap,21,{},metadata);},"removed baseline identity was not checked");
}

void npc_restore_tests(bool reuse_original=false) {
    Fixture f;constexpr odh::CodeOwner owner=0xfc72c0c0U;
    const std::int16_t neighbor_id=reuse_original?0:40;
    std::optional<odh::SessionRemovedOriginalObject> removed;
    if(reuse_original){
        require(f.world.create_runtime_object(3,0x12345678u,{},{}),"original object occupies numeric ID before NPC creation");
        f.baseline=f.world.snapshot();removed=odh::SessionRemovedOriginalObject{3,f.world.find(3)->generation,0x12345678u};
        require(f.world.remove(3),"remove authenticated original before recycling its ID for a resource-only NPC");
    }
    require(f.world.create(3,odh::EntityKind::avatar,{2,88,1,16})&&f.world.define_type(3,owner)&&
        f.world.set_person_data(3,31,neighbor_id)&&f.world.declare_definition_word(3,20,19)&&
        f.world.define_entry_points(3,{{0,4096}})&&f.world.set_position(3,{14,20,1,2}),"construct NPC outside original lot");
    Execution live_npc(f.world,3,f.rng,true,{{owner,program()}});live_npc.schedule(f.scheduler,3);
    f.start();require(live_npc.vm.start(odh::routine_key(4096),{3,3,3,owner},{7})==odh::VmStatus::running,
        "start independent NPC Main");f.step();
    odh::SessionNpcAvatar npc;npc.origin.id=3;npc.origin.generation=f.world.find(3)->generation;
    npc.origin.guid=owner;npc.origin.neighbor_id=neighbor_id;npc.origin.source="GameData/Objects/WorldNPC.far";
    npc.origin.asset="Pizza.iff";npc.origin.object_definition_id=128;npc.origin.character_fingerprint.fill(0x6a);
    npc.primary_owner=owner;npc.selected_name="Synthetic Pizza";npc.decay.last_two_minute_bucket=4;npc.decay.fractions[1]=28;
    npc.decay.fractions[2]=std::numeric_limits<std::int16_t>::min();
    npc.decay.fractions[3]=std::numeric_limits<std::int16_t>::max();
    odh::SessionResourceCodeOwner provenance;provenance.owner=owner;provenance.resource_guid=owner;
    provenance.source=npc.origin.source;provenance.asset=npc.origin.asset;provenance.definition_chunk_id=128;
    provenance.root_routine=4096;provenance.identity=odh::SessionCodeOwnerIdentity::resource_guid;
    provenance.bank_kind=odh::SessionCodeBankKind::complete_private;
    provenance.routine_fingerprint=odh::session_loaded_bank_fingerprint(program());
    const auto capture=[&]{auto b=f.snapshot();b.generic_execution=true;npc.runtime=live_npc.snapshot(3);
        npc.runtime.resource_owners={provenance};b.npc_avatars={npc};
        if(removed){b.removed_original_objects={*removed};b.object_history=odh::SessionObjectHistory{};
            b.object_history->reusable_entity_ids=true;}
        return b;};
    const auto saved=capture();const auto bytes=odh::encode_session_bundle(saved);
    std::size_t resolved=0;
    const odh::SessionNpcAvatarResolver metadata=[&](const auto& origin){++resolved;
        if(origin.guid!=owner||origin.neighbor_id!=neighbor_id||origin.source!=npc.origin.source||origin.asset!="Pizza.iff"||
           origin.object_definition_id!=128||origin.character_fingerprint!=npc.origin.character_fingerprint)
            throw odh::FormatError("NPC exact resource identity changed");
        odh::World source;require(source.create(origin.id,odh::EntityKind::avatar,{2,88,1,16})&&
            source.define_type(origin.id,origin.guid)&&source.set_person_data(origin.id,31,neighbor_id)&&
            source.declare_definition_word(origin.id,20,19)&&source.define_entry_points(origin.id,{{0,4096}}),
            "resource-only NPC metadata");
        auto result=*source.find(origin.id);result.generation=origin.generation;return result;};
    const auto prepare_npc=[&](const auto& data){return odh::HouseholdSessionRestore::prepare(data,f.resources,
        f.selection,f.roster,f.baseline,f.lot,f.bootstrap,21,{}, {},metadata);};
    auto absent_person=saved;odh::World constructor_world;
    require(constructor_world.restore_snapshot(saved.world),"restore absent-NBRS constructor World");
    auto person101=constructor_world.find(3)->person_data;person101.resize(101);
    require(constructor_world.replace_person_data(3,person101),"constructor101 without saved neighbor data");
    absent_person.world=constructor_world.snapshot();
    const auto constructor_metadata=[&](const auto& origin){auto result=metadata(origin);
        result.person_data.resize(101);return result;};
    const auto constructor_candidate=odh::HouseholdSessionRestore::prepare(odh::encode_session_bundle(absent_person),
        f.resources,f.selection,f.roster,f.baseline,f.lot,f.bootstrap,21,{}, {},constructor_metadata);
    require(constructor_candidate->world().find(3)->person_data.size()==101,
        "NPC absent saved person data retains exact constructor101 metadata profile");
    for(const std::size_t count:{69u,80u,100u})
        rejects([&]{odh::HouseholdSessionRestore::prepare(bytes,f.resources,f.selection,f.roster,f.baseline,
            f.lot,f.bootstrap,21,{}, {},[&](const auto& origin){auto result=metadata(origin);
                result.person_data.resize(count);return result;});},"unsupported NPC constructor69/80/100 metadata accepted");
    resolved=0;
    rejects([&]{prepare(bytes,f);},"NPC restored without exact metadata resource resolver");
    auto candidate=prepare_npc(bytes);
    require(resolved==1&&candidate->npc_avatars().size()==1&&candidate->npc_avatars()[0].selected_name=="Synthetic Pizza"&&
        candidate->npc_avatars()[0].decay.fractions[1]==28&&
        candidate->npc_avatars()[0].decay.fractions[2]==std::numeric_limits<std::int16_t>::min()&&
        candidate->npc_avatars()[0].decay.fractions[3]==std::numeric_limits<std::int16_t>::max()&&
        candidate->host().family_chunk_id==21&&
        candidate->family_budget()==4321&&candidate->world().find(3)->kind==odh::EntityKind::avatar&&
        *candidate->world().find(3)->position==*f.world.find(3)->position,
        "NPC metadata, body choice and motive state restored independently of family authority");
    if(removed){
        require(candidate->world().find(3)->generation>removed->generation&&
            candidate->world().find(3)->person_data[31]==0&&candidate->world().find(3)->definition->guid==owner,
            "restore authenticates removed original then installs newer resource-only NPC at recycled numeric ID");
        auto wrong=saved;wrong.removed_original_objects.front().guid++;
        rejects([&]{prepare_npc(odh::encode_session_bundle(wrong));},"recycled NPC accepts removed original with forged baseline GUID");
        wrong=saved;wrong.object_history->reusable_entity_ids=false;
        rejects([&]{prepare_npc(odh::encode_session_bundle(wrong));},"recycled NPC restore lacks reusable-history authority");
        wrong=saved;wrong.removed_original_objects.front().generation=npc.origin.generation;
        rejects([&]{prepare_npc(odh::encode_session_bundle(wrong));},"recycled NPC restore aliases its own generation as original");
        wrong=saved;wrong.removed_original_objects.front().generation=npc.origin.generation+1;
        wrong.world.generation_sequence=npc.origin.generation+1;
        rejects([&]{prepare_npc(odh::encode_session_bundle(wrong));},"recycled NPC restore accepts newer removed-original generation");
    }
    odh::RandomState rng{999};std::uint64_t tick=999;
    Execution a(candidate->world(),1,rng),b(candidate->world(),2,rng),c(candidate->world(),3,rng,true);
    odh::EntityVmScheduler scheduler{4};a.schedule(scheduler,1);b.schedule(scheduler,2);c.schedule(scheduler,3);
    std::size_t owner_loads=0;
    candidate->install_resource_owners(3,c.vm,[&](const auto& record){++owner_loads;
        require(record.owner==owner&&record.resource_guid==owner,"NPC code owner identity changed");
        return std::make_pair(program(),std::optional<odh::SessionLinkedSemiglobal>{});});
    candidate->restore_execution({{1,&a.vm,&a.controllers,&a.playback,{}},{2,&b.vm,&b.controllers,&b.playback,{}}},
        {{3,&c.vm,&c.controllers,&c.playback,{}}},scheduler,rng,tick);
    require(owner_loads==1&&tick==saved.host.tick&&rng.seed()==saved.host.random_seed&&
        c.vm.pending()&&c.vm.pending()->ticket==saved.npc_avatars[0].runtime.vm.pending->ticket&&
        c.vm.stack().front().context.code_owner==owner,"restore NPC Main without Init, Main restart or RNG");
    auto reopened=saved;reopened.world=candidate->world().snapshot();reopened.scheduler=scheduler.snapshot();
    reopened.runtime_entities={a.snapshot(1),b.snapshot(2)};reopened.npc_avatars[0].runtime=c.snapshot(3);
    reopened.npc_avatars[0].runtime.resource_owners={provenance};
    require(odh::encode_session_bundle(reopened)==bytes,"NPC reopen without advancement changes bytes");
    for(unsigned n=0;n<4;++n){f.step();require(scheduler.tick(++tick,16,8),"continue restored NPC scheduler");}
    reopened.host.tick=tick;reopened.host.random_seed=rng.seed();reopened.world=candidate->world().snapshot();
    reopened.scheduler=scheduler.snapshot();reopened.runtime_entities={a.snapshot(1),b.snapshot(2)};
    reopened.npc_avatars[0].runtime=c.snapshot(3);reopened.npc_avatars[0].runtime.resource_owners={provenance};
    require(odh::encode_session_bundle(reopened)==odh::encode_session_bundle(capture()),
        "resident and NPC continuation differs from continuous execution");
    // Every failure operates on fresh candidate graphs and leaves published time/RNG untouched.
    const auto reject_execution=[&](unsigned mode,const char* why){auto failed=prepare_npc(bytes);
        odh::RandomState fresh_rng{999};std::uint64_t fresh_tick=999;
        Execution ra(failed->world(),1,fresh_rng),rb(failed->world(),2,fresh_rng),rc(failed->world(),3,fresh_rng,true);
        odh::EntityVmScheduler fresh_scheduler{4};ra.schedule(fresh_scheduler,1);rb.schedule(fresh_scheduler,2);
        if(mode!=3)rc.schedule(fresh_scheduler,3);
        if(mode!=0)failed->install_resource_owners(3,rc.vm,[](const auto&){return std::make_pair(program(),std::optional<odh::SessionLinkedSemiglobal>{});});
        std::vector<odh::SessionExecutionBinding> residents{{1,&ra.vm,&ra.controllers,&ra.playback,{}},{2,&rb.vm,&rb.controllers,&rb.playback,{}}};
        std::vector<odh::SessionExecutionBinding> visitors{{3,&rc.vm,&rc.controllers,&rc.playback,{}}};
        if(mode==1)visitors.clear();if(mode==2)visitors[0].controllers=&ra.controllers;
        rejects([&]{failed->restore_execution(residents,visitors,fresh_scheduler,fresh_rng,fresh_tick);},why);
        require(fresh_tick==999&&fresh_rng.seed()==999,"failed NPC restore published time or RNG");
        rejects([&]{failed->restore_execution(residents,visitors,fresh_scheduler,fresh_rng,fresh_tick);},"failed candidate reused");};
    reject_execution(0,"NPC unverified resource owner accepted");
    reject_execution(1,"partial NPC bindings accepted");reject_execution(2,"NPC aliases resident controllers");
    reject_execution(3,"partial NPC scheduler accepted");
    rejects([&]{odh::SingleResidentSessionRestore::prepare(bytes,f.resources,f.selection,f.baseline,f.lot,f.bootstrap,21);},
        "legacy single resident loader accepted NPCs");
    auto changed=saved;changed.npc_avatars[0].origin.character_fingerprint[0]++;
    rejects([&]{prepare_npc(odh::encode_session_bundle(changed));},"changed NPC resource fingerprint accepted");
    changed=saved;changed.world.entities.back().definition_words[20]++;
    rejects([&]{prepare_npc(odh::encode_session_bundle(changed));},"NPC immutable metadata differs from exact resource");
    changed=saved;changed.world.entities.back().person_data[0]=1;
    auto unbound_posture=prepare_npc(odh::encode_session_bundle(changed));
    require(unbound_posture->world().find(3)->person_data[0]==1&&!unbound_posture->world().find(3)->attachment,
        "generic NPC preserves source unbound sitting posture until STAND_FUNC executes");
    // An NPC cannot reuse an entity that belonged to the immutable lot baseline.
    auto conflicting=f.baseline;auto old=*f.world.find(3);old.generation=npc.origin.generation;
    conflicting.entities.push_back(old);conflicting.generation_sequence=old.generation;
    rejects([&]{odh::HouseholdSessionRestore::prepare(bytes,f.resources,f.selection,f.roster,conflicting,
        f.lot,f.bootstrap,21,{}, {},metadata);},"NPC claims original lot identity");
}
void legacy_avatar_containment_tests(){
    Fixture source;source.start();source.step();auto saved=source.snapshot();
    saved_avatar_fixture_profile(source.baseline);
    saved_avatar_fixture_profile(saved.world,&source.baseline);
    saved_avatar_fixture_full_slots(source.baseline);
    for(auto& entity:saved.world.entities)if(entity.kind==odh::EntityKind::avatar){
        require(entity.person_data.size()==88&&std::all_of(entity.person_data.begin()+80,entity.person_data.end(),[](auto value){return value==0;}),
            "baseline fixture has inherited TS1 person bank with empty old-save extension");
        entity.person_data.resize(80);saved.world.word_count-=8;
    }
    const auto bytes=odh::encode_session_bundle(saved);
    auto legacy=prepare(bytes,source);
    require(legacy->world().find(1)->slots.size()==1&&
        odh::encode_world_snapshot(legacy->world().snapshot())==odh::encode_world_snapshot(saved.world),
        "legacy saved-avatar checkpoint keeps exact raw slots and words on zero-tick reopen");
    odh::VmFrame frame;frame.context.caller=1;frame.context.stack_object=1;
    require(legacy->world().read(frame,20,2)==0&&legacy->world().read(frame,3,67)==3,
        "legacy checkpoint exposes known empty source head/pelvis without stored normalization");
    require(legacy->world().find(1)->person_data.size()==80&&legacy->world().read(frame,18,84)==0&&
        !legacy->world().read(frame,18,88),"legacy raw80 person bank exposes all known TS1 extension fields, without generic101 padding");
    auto normalized=saved;saved_avatar_fixture_full_slots(normalized.world);
    for(auto& entity:normalized.world.entities)if(odh::ts1_saved_short_person_data(entity)){
        normalized.world.word_count+=88-entity.person_data.size();entity.person_data.resize(88,0);
    }
    auto full=prepare(odh::encode_session_bundle(normalized),source);
    require(full->world().find(1)->slots.size()==3,"new source avatar checkpoint with full slots restores against same baseline");
    require(full->world().find(1)->person_data.size()==88,"new imported TS1 bank88 restores against the same source baseline");
    auto mismatch=saved;mismatch.world.entities[0].saved_object_banks->extras[0]=1;
    rejects([&]{prepare(odh::encode_session_bundle(mismatch),source);},"padding equivalence cannot hide changed original banks");
    auto unsupported=saved;unsupported.world.entities[0].slots.resize(4,0);unsupported.world.word_count+=3;
    rejects([&]{prepare(odh::encode_session_bundle(unsupported),source);},"saved-avatar padding does not admit a fourth slot");
}
void shared_check_register_restore_tests(){
    Fixture f;f.start();f.step();auto& owner=f.first->vm;
    owner.temps()[0]=7;owner.temp_xl()[0]=100000003;
    odh::VmFrame address;
    require(owner.effective_memory().write(address,12,7,321),"persistent Thread advertisement seed");
    const auto root=odh::routine_key(4096);
    const odh::RoutineSet outer_program{{root,{0x8002,{{99,254,255,{}}},{},odh::Bhav::Metadata{0,4,0,0}}}};
    const odh::RoutineSet inner_program{{root,{0x8002,{{98,254,255,{}}},{},odh::Bhav::Metadata{0,4,0,0}}}};
    odh::Vm outer(outer_program);
    const odh::VmPrimitive outer_handler=[&](const auto& frame,const auto&,const auto&,const auto&){
        require(&outer.temps()==&owner.temps()&& &outer.temp_xl()==&owner.temp_xl(),
            "disposable outer Check aliases the actual Caller Thread banks immediately");
        outer.temps()[0]+=1;
        odh::Vm inner(inner_program);
        const odh::VmPrimitive inner_handler=[&](const auto&,const auto&,const auto&,const auto&){
            require(&inner.temps()==&owner.temps()&&inner.temps()[0]==8&&outer.temps()[0]==8,
                "nested Check reads current real shared values, not a stale parent copy");
            inner.temps()[0]+=10;inner.temp_xl()[0]+=1000;
            return odh::PrimitiveOutcome{odh::PrimitiveDisposition::goto_false,{}};
        };
        require(inner.start(root,{1,1,1})==odh::VmStatus::running&&
            inner.run_synchronous_check_with_registers(owner,true,8,inner_handler).status==odh::VmStatus::returned_false,
            "nested Check returns false after shared register effects");
        require(owner.temps()[0]==18&&outer.temps()[0]==18&&outer.temp_xl()[0]==100001003,
            "nested register writes are visible before the outer Check returns");
        outer.temps()[0]+=2;outer.temp_xl()[0]+=1;
        require(outer.effective_memory().write(frame,12,7,42),"outer disposable advertisement effect");
        return odh::PrimitiveOutcome{odh::PrimitiveDisposition::goto_false,{}};
    };
    require(outer.start(root,{1,1,1})==odh::VmStatus::running&&
        outer.run_synchronous_check_with_registers(owner,true,8,outer_handler).status==odh::VmStatus::returned_false,
        "outer Check preserves its shared effects on a false result");
    require(owner.temps()[0]==20&&owner.temp_xl()[0]==100001004&&
        outer.temps()[0]==20&& &outer.temps()!=&owner.temps()&&
        owner.effective_memory().read(address,12,7)==321,
        "scoped alias detaches with final values while fresh Check advertisements stay isolated");
    outer.temps()[0]=-9;
    require(owner.temps()[0]==20,"detached disposable Check cannot mutate a surviving Caller Thread");
    const auto saved=f.snapshot();auto candidate=prepare(odh::encode_session_bundle(saved),f);
    odh::RandomState rng{999};std::uint64_t tick=999;
    Execution first(candidate->world(),1,rng),second(candidate->world(),2,rng);
    odh::EntityVmScheduler scheduler{4};first.schedule(scheduler,1);second.schedule(scheduler,2);
    candidate->restore_execution({{1,&first.vm,&first.controllers,&first.playback,{}},
        {2,&second.vm,&second.controllers,&second.playback,{}}},scheduler,rng,tick);
    require(tick==f.tick&&first.vm.temps()[0]==20&&first.vm.temp_xl()[0]==100001004&&
        first.vm.effective_memory().read(address,12,7)==321&&first.vm.pending(),
        "fresh actual resident/controllers restore nested shared-register results with their original pending Main");
}
void named_resource_restore_tests(){
    for(const bool semi:{false,true}){
        Fixture f;constexpr odh::CodeOwner owner=0x80000000U;
        const odh::RoutineKey key{semi?odh::RoutineScope::named_semiglobal:odh::RoutineScope::named_private,
            static_cast<std::uint16_t>(semi?42:0)};
        const odh::RoutineSet bank{{key,program().begin()->second}};
        Execution first(f.world,1,f.rng,false,{{owner,bank}}),second(f.world,2,f.rng);
        odh::EntityVmScheduler scheduler{4};first.schedule(scheduler,1);second.schedule(scheduler,2);
        require(first.vm.start(key,{1,1,1,owner},{7})==odh::VmStatus::running&&
            second.vm.start(odh::routine_key(4096),{2,2,2,2},{4})==odh::VmStatus::running&&scheduler.tick(1,16,8),
            "start actual named resource frame and reach pending Sleep");
        auto saved=f.snapshot();saved.host.tick=1;saved.host.random_seed=f.rng.seed();saved.generic_execution=true;
        saved.next_code_owner=owner+1;saved.runtime_entities={first.snapshot(1),second.snapshot(2)};saved.scheduler=scheduler.snapshot();
        odh::SessionResourceCodeOwner manifest;manifest.owner=owner;manifest.resource_guid=98765;
        manifest.source="Objects.far";manifest.asset="Named.iff";manifest.definition_chunk_id=128;
        manifest.root_routine=key.id;manifest.bank_kind=semi?odh::SessionCodeBankKind::complete_named_semiglobal:
            odh::SessionCodeBankKind::complete_named_private;
        if(semi)manifest.linked_semiglobal=odh::SessionLinkedSemiglobal{"Linked","Global.far","Linked.iff"};
        manifest.routine_fingerprint=odh::session_loaded_bank_fingerprint(bank);saved.runtime_entities.front().resource_owners={manifest};
        const auto bytes=odh::encode_session_bundle(saved);auto candidate=prepare(bytes,f);
        odh::RandomState rng{999};std::uint64_t tick=999;
        Execution resumed(candidate->world(),1,rng),other(candidate->world(),2,rng);
        odh::EntityVmScheduler restored_scheduler{4};resumed.schedule(restored_scheduler,1);other.schedule(restored_scheduler,2);
        rejects([&]{candidate->install_resource_owners(1,resumed.vm,[&](const auto&){
            return std::make_pair(program(),manifest.linked_semiglobal);});},"named root cannot be replaced by numerically inferred bank");
        candidate->install_resource_owners(1,resumed.vm,[&](const auto& record){
            require(odh::session_resource_root_key(record).scope==key.scope&&odh::session_resource_root_key(record).id==key.id,"restore loader keeps original named entry identity");
            return std::make_pair(bank,manifest.linked_semiglobal);});
        candidate->restore_execution({{1,&resumed.vm,&resumed.controllers,&resumed.playback,{}},
            {2,&other.vm,&other.controllers,&other.playback,{}}},restored_scheduler,rng,tick);
        require(tick==1&&resumed.vm.stack().front().key.scope==key.scope&&resumed.vm.stack().front().key.id==key.id&&resumed.vm.pending()&&
            resumed.vm.pending()->instruction.key.scope==key.scope&&resumed.vm.pending()->instruction.key.id==key.id&&resumed.vm.stack().front().context.code_owner==owner,
            "fresh actual controllers restore named frame and pending resource without numeric redirection");
        for(std::uint64_t next=2;next<=12;++next)
            require(scheduler.tick(next,16,8)&&restored_scheduler.tick(next,16,8)&&
                resumed.vm.status()==first.vm.status()&&resumed.vm.steps()==first.vm.steps()&&rng.seed()==f.rng.seed(),
                "fresh named continuation matches uninterrupted execution and RNG at every subsequent tick");
        require(resumed.vm.stack().empty()&&(resumed.vm.status()==odh::VmStatus::returned_true||
            resumed.vm.status()==odh::VmStatus::returned_false),"restored named routine actually finishes its pending Sleep and returns");
    }
}
void signed_motive_restore_tests(){
    Fixture f(16);f.start();f.step();auto saved=f.snapshot();saved.simulation.emplace();
    odh::Ts1Clock clock;require(clock.initialize(7,17,4,6,1997),"signed motive clock");
    auto time=clock.snapshot();time.ticks=saved.host.tick;saved.simulation->clock=time;
    for(std::int16_t i=0;i<9;++i)if(const auto value=clock.global(i))
        saved.host.global_values.push_back({static_cast<std::uint16_t>(i),*value});
    std::sort(saved.host.global_values.begin(),saved.host.global_values.end(),[](const auto& a,const auto& b){return a.index<b.index;});
    for(const auto& runtime:saved.runtime_entities)saved.simulation->avatars.push_back({runtime.id,{}});
    auto& sleeping=saved.world.entities.front();sleeping.motives[11]=-40;
    sleeping.motives[5]=125;sleeping.motives[13]=-300;
    const auto restored=prepare(odh::encode_session_bundle(saved),f);
    const auto* actual=restored->world().find(sleeping.id);
    require(actual&&actual->motives[11]==-40&&actual->motives[5]==125&&actual->motives[13]==-300,
        "fresh full-bank resident restore preserves signed sleep, overfilled energy and room score");
}
void travel_restore_authority_tests(){
    Fixture f;
    for(const auto& m:f.roster)require(f.world.set_person_data(m.id,31,m.neighbor_id)&&f.world.set_person_data(m.id,61,21),"bind actual travel family identities");
    f.baseline=f.world.snapshot();auto bundle=f.snapshot();bundle.generic_execution=true;bundle.neighbor_inventory=odh::NeighborInventorySnapshot{};
    odh::Ts1TravelState travel;travel.home_house=travel.current_house=7;travel.home_mode=travel.current_mode=1;travel.family_chunk_id=21;travel.family_number=12;travel.neighborhood_identity.fill(0x11);travel.family_identity.fill(0x22);
    for(const auto& m:f.roster)travel.permanent_roster.push_back({m.guid,m.neighbor_id,m.object_definition_id,m.character_asset,m.character_fingerprint});
    travel.travelers={travel.permanent_roster.front()};travel.caller_guid=f.selection.resident_guid;travel.caller_neighbor_id=f.selection.resident_neighbor_id;
    odh::Bhav routine{0x8002,{{1,1,255,{17,0,0,0,0,0,0,0}},{0,254,255,{}}},{},odh::Bhav::Metadata{0,4,4,0}};
    odh::Vm vm(odh::RoutineSet{},f.world.vm_memory(),{},[&](const auto& frame,const auto& instruction,const auto&,const auto& dispatch){
        odh::Ts1TravelRequest request;request.destination=23;request.thread_owner={1,f.world.find(1)->generation};request.run_generation=dispatch.origin->run_generation();request.ticket=dispatch.ticket;request.source_frame=frame;request.source_instruction=instruction;travel.pending=request;
        return odh::PrimitiveOutcome{odh::PrimitiveDisposition::goto_true_next_tick,{}};
    },{{1,{{odh::routine_key(4096),routine}}}});
    require(vm.start(odh::routine_key(4096),{1,1,1,1})==odh::VmStatus::running&&vm.run(1)==odh::VmStatus::next_tick,"actual home Generic17 deferred boundary");
    odh::EntityThreadControllers controllers(f.world,1);require(controllers.bind(vm),"bind travel VM controller to actual post-start generation");
    bundle.runtime_entities.front().vm=vm.snapshot();bundle.runtime_entities.front().controllers=controllers.snapshot();
    for(auto& entry:bundle.scheduler.entities)if(entry.id==1){entry.status=vm.status();entry.steps=vm.snapshot().steps;entry.run_generation=vm.run_generation();}
    bundle.travel=travel;odh::seal_ts1_travel_request(*bundle.travel,bundle);const auto bytes=odh::encode_session_bundle(bundle);
    rejects([&]{prepare(bytes,f);},"ordinary household load must reject HOME TRIP with advanced VM");
    rejects([&]{odh::SingleResidentSessionRestore::prepare(bytes,f.resources,f.selection,f.baseline,f.lot,f.bootstrap,21);},"ordinary legacy load must reject HOME TRIP");
    const auto explicit_prepare=[&](const std::vector<std::uint8_t>& data,const odh::Ts1TravelState& expected){return odh::HouseholdSessionRestore::prepare(data,f.resources,f.selection,f.roster,f.baseline,f.lot,f.bootstrap,21,{},{},{},expected);};
    auto mismatch=*bundle.travel;mismatch.pending->destination=24;rejects([&]{explicit_prepare(bytes,mismatch);},"expected deferred destination must match exact TRIP");
    auto absent=bundle;absent.travel.reset();rejects([&]{explicit_prepare(odh::encode_session_bundle(absent),*bundle.travel);},"expected travel cannot be silently absent");
    auto validated=explicit_prepare(bytes,*bundle.travel);require(validated->travel()&&validated->travel()->pending,"explicit authenticated home travel prepare retains deferred intent");
    odh::EntityVmScheduler unused(4);odh::RandomState rng(5);std::uint64_t tick=0;
    rejects([&]{validated->restore_execution({},unused,rng,tick);},"matching TRIP cannot resume before explicit travel authority handoff");
    auto consumed=validated->consume_travel();require(consumed&&odh::encode_ts1_travel_state(*consumed)==odh::encode_ts1_travel_state(*bundle.travel),"enabled caller consumes exact validated travel authority");
    rejects([&]{validated->consume_travel();},"travel authority cannot be consumed twice");
}

void traveling_avatar_resource_restore_tests(){
    Fixture f(16);
    odh::World empty(odh::WorldLimits{},odh::AttributePolicy::ts1_bounded);
    const auto installed=empty.snapshot();
    odh::World object_resource(odh::WorldLimits{},odh::AttributePolicy::ts1_bounded);
    require(object_resource.create(3,odh::EntityKind::object)&&
        object_resource.define_type(3,0xabc123),"create source object metadata");
    const auto object=*object_resource.find(3); // Generation1, before travelers2/3.
    odh::World ordered(odh::WorldLimits{},odh::AttributePolicy::ts1_bounded);
    require(ordered.create(3,odh::EntityKind::object)&&ordered.define_type(3,0xabc123),
        "source script creates object before travelers");
    for(std::size_t i=0;i<2;++i){
        const auto id=static_cast<std::int16_t>(i+1);
        require(ordered.create(id,odh::EntityKind::avatar,{1,88,1,16})&&
            ordered.define_type(id,f.roster[i].guid)&&
            ordered.declare_definition_word(id,20,static_cast<std::int16_t>(10+id))&&
            ordered.define_entry_points(id,{{0,4096}})&&
            ordered.set_person_data(id,31,f.roster[i].neighbor_id)&&ordered.set_person_data(id,61,21),
            "source creates exact traveler after script objects");
        f.roster[i].generation=ordered.find(id)->generation;
    }
    odh::EntityVmScheduler scheduler(4);
    Execution first(ordered,1,f.rng),second(ordered,2,f.rng);
    first.schedule(scheduler,1);second.schedule(scheduler,2);
    auto bundle=f.snapshot();bundle.generic_execution=true;
    bundle.world=ordered.snapshot();bundle.runtime_entities={first.snapshot(1),second.snapshot(2)};
    bundle.scheduler=scheduler.snapshot();
    bundle.relationships=odh::RelationshipState{}.snapshot();
    bundle.host.house_number=23;bundle.host.neighborhood_mode=2;
    const odh::LotContext lot(23,std::vector<std::int16_t>(32));
    bundle.host.global_values=globals(lot,f.bootstrap);
    bundle.created_objects.push_back({3,object.generation,0xabc123,"GameData/Objects/Objects.far","Prototype.iff",128});
    odh::Ts1TravelState travel;travel.home_house=7;travel.current_house=23;
    travel.home_mode=1;travel.current_mode=2;travel.family_chunk_id=21;travel.family_number=12;
    travel.neighborhood_identity.fill(0x11);travel.family_identity.fill(0x22);travel.family_activated=true;
    for(const auto& m:bundle.household_roster)travel.permanent_roster.push_back({m.guid,m.neighbor_id,m.object_definition_id,m.character_asset,m.character_fingerprint});
    travel.travelers=travel.permanent_roster;travel.caller_guid=bundle.host.resident_guid;travel.caller_neighbor_id=bundle.host.resident_neighbor_id;
    bundle.travel=travel;bundle.neighbor_inventory=odh::NeighborInventorySnapshot{};
    const auto bytes=odh::encode_session_bundle(bundle);
    const auto resolve_object=[&](const auto& origin){require(origin.id==3&&origin.generation==1,"object still uses original immutable generation floor");return object;};
    unsigned constructions=0;
    const odh::SessionTravelAvatarResolver resolve_traveler=[&](const auto& member){
        ++constructions;auto metadata=f.baseline.entities.at(static_cast<std::size_t>(member.id-1));
        metadata.generation=member.generation;metadata.person_data[31]=member.neighbor_id;metadata.person_data[61]=21;return metadata;
    };
    const auto prepare=[&](const odh::SessionTravelAvatarResolver& resolver){return odh::HouseholdSessionRestore::prepare(
        bytes,bundle.resources,bundle.host,bundle.household_roster,installed,lot,f.bootstrap,21,{},resolve_object,{},travel,resolver);};
    rejects([&]{prepare({});},"missing travel constructor cannot import arbitrary household avatars");
    auto candidate=prepare(resolve_traveler);
    require(constructions==2&&candidate->world().find(3)->generation==1&&
        candidate->world().find(1)->generation==2&&candidate->world().find(2)->generation==3,
        "objects created before arriving travelers retain their valid original provenance");
    require(candidate->consume_travel().has_value(),"arriving household still requires explicit travel consumption");
    rejects([&]{prepare([&](const auto& member){auto metadata=resolve_traveler(member);++metadata.generation;return metadata;});},
        "replaced traveler generation is rejected");
    rejects([&]{prepare([&](const auto& member){auto metadata=resolve_traveler(member);metadata.person_data[31]++;return metadata;});},
        "constructor cannot substitute a different neighbor identity");
    // The exact same HHRD/resource authority admits residents newly created at
    // HOME. No TRIP, fake original IDs, or enlarged original generation floor.
    auto home=bundle;home.travel.reset();home.host.house_number=7;home.host.neighborhood_mode=1;
    home.host.global_values=globals(f.lot,f.bootstrap);
    const auto home_bytes=odh::encode_session_bundle(home);
    const auto prepare_home=[&](const odh::SessionHouseholdAvatarResolver& resolver){
        return odh::HouseholdSessionRestore::prepare(home_bytes,home.resources,home.host,home.household_roster,
            installed,f.lot,f.bootstrap,21,{},resolve_object,{},std::nullopt,resolver);
    };
    rejects([&]{prepare_home({});},"new HOME residents require exact external character resolver");
    constructions=0;auto restored_home=prepare_home(resolve_traveler);
    require(constructions==2&&!restored_home->consume_travel()&&restored_home->world().find(1)->generation==2&&
        restored_home->world().find(2)->generation==3&&restored_home->world().find(3)->generation==1,
        "HOME resident constructors and earlier source object all preserve actual fresh identities");
    for(unsigned bad=0;bad<8;++bad){
        rejects([&]{prepare_home([&](const auto& member){
            auto metadata=resolve_traveler(member);
            switch(bad){
            case 0:++metadata.generation;break;
            case 1:++metadata.definition->guid;break;
            case 2:++metadata.person_data[31];break;
            case 3:++metadata.person_data[61];break;
            case 4:metadata.dead=true;break;
            case 5:metadata.ui_ghost=true;break;
            case 6:metadata.position=odh::EntityPosition{8,8,1,0};break;
            case 7:metadata.kind=odh::EntityKind::object;break;
            }
            return metadata;
        });},"HOME metadata cannot substitute identity, family, class or position");
    }
    auto wrong_roster=home.household_roster;wrong_roster.front().character_fingerprint[0]^=1;
    const auto before_resolves=constructions;
    rejects([&]{odh::HouseholdSessionRestore::prepare(home_bytes,home.resources,home.host,wrong_roster,
        installed,f.lot,f.bootstrap,21,{},resolve_object,{},std::nullopt,resolve_traveler);},
        "untrusted HHRD cannot replace externally authenticated character fingerprint");
    require(constructions==before_resolves,"family/resource mismatch rejects before metadata construction");
    odh::World occupied(odh::WorldLimits{},odh::AttributePolicy::ts1_bounded);
    require(occupied.create(3,odh::EntityKind::object)&&occupied.define_type(3,0xabc123)&&
        occupied.create(1,odh::EntityKind::object)&&occupied.define_type(1,0xdead),"original ID collision fixture");
    auto collision=home;collision.created_objects.clear();const auto collision_bytes=odh::encode_session_bundle(collision);
    rejects([&]{odh::HouseholdSessionRestore::prepare(collision_bytes,collision.resources,collision.host,
        collision.household_roster,occupied.snapshot(),f.lot,f.bootstrap,21,{}, {},{},std::nullopt,resolve_traveler);},
        "HHRD cannot replace an original object's ID with a new resident");
    require(occupied.create(4,odh::EntityKind::object)&&occupied.remove(4),"original generation history survives a collected body");
    rejects([&]{odh::HouseholdSessionRestore::prepare(collision_bytes,collision.resources,collision.host,
        collision.household_roster,occupied.snapshot(),f.lot,f.bootstrap,21,{}, {},{},std::nullopt,resolve_traveler);},
        "new household generation must exceed original history even when its body is gone");
    auto stale=home;stale.created_objects.clear();
    // An omitted source entity remains forbidden; HHRD does not authorize it.
    rejects([&]{odh::HouseholdSessionRestore::prepare(odh::encode_session_bundle(stale),home.resources,home.host,
        home.household_roster,installed,f.lot,f.bootstrap,21,{}, {},{},std::nullopt,resolve_traveler);},
        "household provenance cannot smuggle an unrelated unrecorded source object");
}

void offworld_resident_restore_tests(){
    // A saved household can retain one or both residents while they are at
    // school/work. Absence of a lot position is not absence of that resident.
    for(const unsigned departed:{1U,2U,3U}) {
        Fixture f;
        require(f.world.set_position(1,{10,20,1,2})&&f.world.set_position(2,{30,40,1,6}),
            "original household starts positioned before departure");
        f.baseline=f.world.snapshot();
        require(f.world.set_person_data(1,33,37)&&f.world.set_person_data(2,33,51),
            "departing residents retain distinct priority metadata");
        f.start();f.step();
        for(std::int16_t id:{1,2})if(departed&(1U<<(id-1)))
            require(f.world.place_out_of_world(id,static_cast<std::uint8_t>(id+1)),
                "source departure clears position while preserving live resident identity");
        const auto saved=f.snapshot();const auto bytes=odh::encode_session_bundle(saved);
        require(saved.runtime_entities[0].vm.status==odh::VmStatus::waiting&&
            saved.runtime_entities[1].vm.status==odh::VmStatus::waiting,
            "offworld archive retains real pending Sleep jobs and scheduler bindings");
        auto candidate=prepare(bytes,f);
        for(std::int16_t id:{1,2}) {
            const auto* resident=candidate->world().find(id);
            require(resident&&resident->definition&&resident->generation==f.world.find(id)->generation&&
                resident->position.has_value()==((departed&(1U<<(id-1)))==0)&&
                resident->person_data[33]==f.world.find(id)->person_data[33],
                "prepare retains exact on/offworld presence and resident metadata without placement");
        }
        odh::RandomState rng{999};std::uint64_t tick=999;
        Execution a(candidate->world(),1,rng),b(candidate->world(),2,rng);
        odh::EntityVmScheduler scheduler{4};a.schedule(scheduler,1);b.schedule(scheduler,2);
        candidate->restore_execution({{1,&a.vm,&a.controllers,&a.playback,{}},
            {2,&b.vm,&b.controllers,&b.playback,{}}},scheduler,rng,tick);
        auto reopened=saved;reopened.world=candidate->world().snapshot();
        reopened.runtime_entities={a.snapshot(1),b.snapshot(2)};reopened.scheduler=scheduler.snapshot();
        reopened.host.tick=tick;reopened.host.random_seed=rng.seed();
        require(odh::encode_session_bundle(reopened)==bytes&&
            odh::same_session_household_roster(odh::decode_session_bundle(bytes).household_roster,f.roster),
            "full offworld World/VM/controllers/scheduler and authenticated roster reopen byte-exactly");
        auto wrong=f.roster;++wrong.front().neighbor_id;
        rejects([&]{odh::HouseholdSessionRestore::prepare(bytes,f.resources,f.selection,wrong,
            f.baseline,f.lot,f.bootstrap,21);},
            "offworld presence never bypasses exact household/resource roster authentication");
    }
}
void retired_household_source_tests(){
    Fixture f;for(const auto& member:f.roster)require(f.world.set_person_data(member.id,31,member.neighbor_id)&&
        f.world.set_person_data(member.id,61,21),"source household actual personal identity");
    f.baseline=f.world.snapshot();f.start();
    require(f.first->controllers.join_at(f.tick)&&f.second->controllers.join_at(f.tick),
        "actual prepared household Main/service generations join before first capture");
    auto source=f.snapshot();source.generic_execution=true;
    odh::SessionFamilyLease lease;lease.neighborhood_identity.fill(0x51);lease.family_identity.fill(0x52);
    lease.occupancy_epoch=7;lease.family_chunk_id=21;source.family_lease=lease;
    const auto bytes=odh::encode_session_bundle(source);const auto member=f.roster.at(1);
    const odh::SessionRetiredHouseholdAvatar origin{member,lease,odh::sha256(bytes.data(),bytes.size())};
    odh::validate_retired_household_avatar_source(origin,bytes);
    for(unsigned scenario=0;scenario<4;++scenario){auto bad=origin;
        if(scenario==0)++bad.member.neighbor_id;else if(scenario==1)++bad.source_lease.occupancy_epoch;
        else if(scenario==2)bad.member.character_fingerprint[0]^=1;else bad.source_checkpoint_sha256[0]^=1;
        rejects([&]{odh::validate_retired_household_avatar_source(bad,bytes);},"historical source mismatch accepted");
    }
    auto candidate=source;odh::World live;odh::RelationshipState relationships;
    require(live.restore_snapshot(source.world)&&relationships.restore_snapshot(source.relationships)&&
        live.mark_dead({member.id,member.generation})&&relationships.remove_inbound_object_references(live,member.id,member.generation)&&
        live.remove_retaining_body({member.id,member.generation}),"actual retained household body and inbound relation cleanup");
    candidate.world=live.snapshot();candidate.relationships=relationships.snapshot();candidate.household_roster.resize(1);candidate.runtime_entities.resize(1);
    candidate.scheduler.entities.resize(1);candidate.object_history=odh::SessionObjectHistory{};
    candidate.object_history->complete=true;candidate.object_history->reusable_entity_ids=true;
    candidate.object_history->retired_household_avatars={origin};
    auto& frame=candidate.runtime_entities.front().vm.stack.front();
    require(odh::vm_context_set_reference(frame.context,odh::VmEntityRole::stack_object,{member.id,member.generation}),
        "foreign active Main owns exact retired household reference");
    const auto encoded=odh::encode_session_bundle(candidate);unsigned calls=0;
    const odh::SessionRetiredHouseholdAvatarResolver resolve=[&](const auto& request){
        ++calls;odh::validate_retired_household_avatar_source(request,bytes);
        const auto body=std::find_if(source.world.entities.begin(),source.world.entities.end(),[&](const auto& e){
            return e.id==request.member.id&&e.generation==request.member.generation;});
        require(body!=source.world.entities.end(),"exact resource metadata fixture");return *body;
    };
    const auto run=[&](const auto& archive,const odh::SessionRetiredHouseholdAvatarResolver& resolver){
        return odh::HouseholdSessionRestore::prepare(archive,f.resources,f.selection,candidate.household_roster,
            f.baseline,f.lot,f.bootstrap,21,{}, {}, {},std::nullopt,{},resolver);};
    rejects([&]{run(encoded,{});},"retired household lacks historical resolver");
    const auto restored=run(encoded,resolve);
    require(calls==1&&restored->world().snapshot().retained_entities.size()==1&&restored->world().find(1)&&
        !restored->world().find(2),"historically proven original household retires independently of active family roster");
    rejects([&]{run(encoded,[&](const auto& request){auto value=resolve(request);++value.generation;return value;});},
        "historical resolver replaced generation accepted");
    auto collected=candidate;collected.runtime_entities.front().vm.stack.front().context=source.runtime_entities.front().vm.stack.front().context;
    require(live.collect_retained_bodies({})&&relationships.forget_object_source(member.id,member.generation),"real unreferenced household body/source collection");collected.world=live.snapshot();collected.relationships=relationships.snapshot();
    const auto collected_bytes=odh::encode_session_bundle(collected);
    const auto reopened=run(collected_bytes,resolve);
    require(reopened->world().snapshot().retained_entities.empty(),"historical original retirement remains proven after actual body collection");
}
void tests(){
    retired_household_source_tests();
    offworld_resident_restore_tests();
    traveling_avatar_resource_restore_tests();
    travel_restore_authority_tests();
    shared_check_register_restore_tests();
    named_resource_restore_tests();
    signed_motive_restore_tests();
    legacy_avatar_containment_tests();
    route_stack_profile_tests();npc_restore_tests();npc_restore_tests(true);generic_execution_tests();equivalent_action_owner_cache_tests();Fixture f;f.start();odh::VmFrame attribute_frame;
    attribute_frame.context.caller=1;attribute_frame.context.stack_object=1;
    require(f.world.write(attribute_frame,0,4,99),"TS1 attribute bank grows on write");
    f.step();auto saved=f.snapshot();const auto bytes=odh::encode_session_bundle(saved);
    {
        auto typed=saved;typed.host.global_values.clear();
        odh::RuntimeGlobals globals(std::vector<std::int16_t>(32),odh::Ts1GlobalActivation::saved_new,7);
        require(globals.write(9,-11)&&globals.write(10,99)&&globals.write(0,-123)&&globals.write(3,1),
            "source raw global values can differ from actual family/lot/clock");
        typed.runtime_globals=globals.snapshot();typed.simulation.emplace();
        odh::Ts1Clock clock;require(clock.initialize(7,17,4,6,1997),"RGLB clock init");
        auto time=clock.snapshot();time.ticks=typed.host.tick;typed.simulation->clock=time;
        for(const auto& runtime:typed.runtime_entities)typed.simulation->avatars.push_back({runtime.id,{}});
        const auto encoded=odh::encode_session_bundle(typed);
        auto restored=prepare(encoded,f);
        require(restored->runtime_globals()&&restored->runtime_globals()->words==typed.runtime_globals->words&&
            restored->runtime_globals()->selected_sim_is_runtime_id==typed.runtime_globals->selected_sim_is_runtime_id,
            "typed raw global bank survives fresh prepared restore exactly");
        require(restored->family_global()==21&&restored->lot().read(10)==7&&restored->host().family_chunk_id==21&&
            restored->host().house_number==7,"raw G9/G10 writes do not activate another family or house");
        odh::RuntimeGlobals resumed(std::vector<std::int16_t>(32),odh::Ts1GlobalActivation::saved_new,7);
        require(resumed.restore(*restored->runtime_globals())&&clock.restore(restored->simulation()->clock)&&
            resumed.read(9)==-11&&resumed.read(10)==99&&resumed.read(0,&clock)==7&&resumed.snapshot().words[0]==-123,
            "restored provider preserves clock shadows separately from authoritative derived time");
        auto again=typed;again.runtime_globals=restored->runtime_globals();
        require(odh::encode_session_bundle(again)==encoded,"prepared restore does not normalize typed global bytes");
        auto mixed=typed;mixed.host.global_values.push_back({9,21});
        rejects([&]{prepare(odh::encode_session_bundle(mixed),f);},"mixed typed and legacy global authorities accepted");
        auto wrong=typed;wrong.host.family_chunk_id=22;
        rejects([&]{prepare(odh::encode_session_bundle(wrong),f);},"typed globals bypassed actual family selection lease");
    }

    {
        auto with_sim=saved;with_sim.simulation.emplace();
        odh::Ts1Clock clock;clock.initialize(7,17,4,6,1997);auto time=clock.snapshot();time.ticks=with_sim.host.tick;
        with_sim.simulation->clock=time;clock.restore(time);
        for(const auto& runtime:with_sim.runtime_entities)with_sim.simulation->avatars.push_back({runtime.id,{}});
        with_sim.simulation->avatars.front().decay.fractions[0]=std::numeric_limits<std::int16_t>::min();
        with_sim.simulation->avatars.front().decay.fractions[1]=std::numeric_limits<std::int16_t>::max();
        for(std::int16_t i=0;i<9;++i)if(const auto v=clock.global(i))with_sim.host.global_values.push_back({static_cast<std::uint16_t>(i),*v});
        std::sort(with_sim.host.global_values.begin(),with_sim.host.global_values.end(),[](const auto& a,const auto& b){return a.index<b.index;});
        auto restored_sim=prepare(odh::encode_session_bundle(with_sim),f);
        require(restored_sim->simulation() && restored_sim->simulation()->clock.minute==4 && !restored_sim->lot().read(0),
            "SIMS globals are accepted and remain derived outside LotContext");
        require(restored_sim->simulation()->avatars.front().decay.fractions==with_sim.simulation->avatars.front().decay.fractions,
            "fresh resident restore preserves signed decay carry");
        with_sim.host.global_values.erase(with_sim.host.global_values.begin());
        rejects([&]{prepare(odh::encode_session_bundle(with_sim),f);},"SIMS missing clock global accepted");
    }
    for(const auto id:{std::int16_t{1},std::int16_t{2}}){
        Fixture local;local.start();auto route=local_route_bundle(local,id);
        auto accepted=prepare(odh::encode_session_bundle(route),local);
        require(accepted->local_route(id)&&accepted->dynamic_action_owners(id).empty(),
            "existing primary and secondary local Route45 profiles remain accepted without DYNO");
        for(const std::uint8_t mode:{std::uint8_t{2},std::uint8_t{3},std::uint8_t{4},std::uint8_t{5}}){
            auto body=route;auto& state=body.runtime_entities[static_cast<std::size_t>(id-1)];
            state.vm.pending->instruction.instruction={22,254,255,{mode,9,8,7,6,5,4,3}};
            auto& turn=*state.route;turn.target=id;turn.target_generation=turn.actor_generation;
            turn.target_position=turn.expected;turn.path={{turn.expected.x,turn.expected.y}};
            turn.frame.state=odh::RouteFrameState::turn_only;turn.entry_flags=1;
            turn.facing=(mode&1U)?6:2;turn.look_towards.emplace();
            turn.look_towards->mode=mode;
            turn.look_towards->start_radian_direction=turn.expected_radian_direction;
            turn.look_towards->target_radian_direction=(mode&1U)?4.7123889803846898577f:turn.expected_radian_direction;
            const auto body_bytes=odh::encode_session_bundle(body);
            const auto seed=local.rng.seed();
            auto restored_body=prepare(body_bytes,local);
            require(restored_body->local_route(id)&&restored_body->local_route(id)->look_towards&&
                restored_body->local_route(id)->look_towards->mode==mode&&
                restored_body->local_route(id)->target==id&&local.rng.seed()==seed,
                "either resident prepares exact frozen body22 mode without reranking or consuming source RNG");
            auto malformed=body;malformed.runtime_entities[static_cast<std::size_t>(id-1)].route->look_towards.reset();
            rejects([&]{prepare(odh::encode_session_bundle(malformed),local);},
                "pending22 cannot use a plain route as body-turn authority");
            malformed=body;malformed.runtime_entities[static_cast<std::size_t>(id-1)].vm.pending->instruction.instruction.operands[0]=1;
            rejects([&]{prepare(odh::encode_session_bundle(malformed),local);},
                "camera22 cannot claim a frozen body-turn marker");
            malformed=body;malformed.runtime_entities[static_cast<std::size_t>(id-1)].route->ticket++;
            rejects([&]{prepare(odh::encode_session_bundle(malformed),local);},
                "body-turn marker cannot admit a different pending ticket");
        }
        for(const auto direction:{std::uint8_t{254},std::uint8_t{255},std::uint8_t{0},std::uint8_t{1},
            std::uint8_t{2},std::uint8_t{3},std::uint8_t{4},std::uint8_t{5},std::uint8_t{6},std::uint8_t{7}})
        for(std::uint8_t flags=0;flags<4;++flags){
            auto relative=route;auto& state=relative.runtime_entities[static_cast<std::size_t>(id-1)];
            state.vm.pending->instruction.instruction={27,254,255,{0,0,254,direction,0,0,flags,0}};
            auto& progress=*state.route;progress.entry_flags=0;progress.face_anywhere=direction==255;
            progress.call_failure_trees=(flags&2U)==0;progress.facing=direction<=7?direction:0;
            progress.path.clear();for(std::int32_t x=5;x<=28;++x)progress.path.push_back({x,12});
            auto candidate=prepare(odh::encode_session_bundle(relative),local);
            require(candidate->local_route(id)&&candidate->local_route(id)->call_failure_trees==((flags&2U)==0)&&
                candidate->local_route(id)->face_anywhere==(direction==255),
                "generic authored OnTopOf route prepares for either resident without a resource exception");
        }
    }
    auto candidate=prepare(bytes,f);
    require(candidate->family_budget()==std::optional<std::int32_t>{4321},
        "household session restores the active family's runtime budget");
    for(;f.tick<6;)f.step();resume(*candidate,f.snapshot());
    for(const auto routed_id:{std::int16_t{1},std::int16_t{2}}){
        const auto idle_id=static_cast<std::int16_t>(routed_id==1?2:1);
        Fixture route_fixture;
        require(route_fixture.world.set_position(1,{16,88,1,0})&&
            route_fixture.world.set_position(2,{32,88,1,0}),"position household residents");
        route_fixture.baseline=route_fixture.world.snapshot();route_fixture.start();
        odh::VmFrame frame;frame.context.caller=1;frame.context.stack_object=1;
        require(route_fixture.world.write(frame,0,4,99),"route resident bank grows");
        auto routed=go_here_bundle(route_fixture,routed_id);const auto routed_bytes=odh::encode_session_bundle(routed);
        auto generic_route=routed;generic_route.generic_execution=true;
        generic_route.next_code_owner=0x80000001U;
        auto generic_candidate=prepare(odh::encode_session_bundle(generic_route),route_fixture);
        require(generic_candidate->generic_execution()&&generic_candidate->local_route(routed_id)&&
            generic_candidate->resource_owners(routed_id).empty()&&
            generic_candidate->dynamic_action_owners(routed_id).size()==1,
            "GEXP retains the existing exact Go Here DYNO and temporary marker profile without requiring new manifests");
        auto stacked_go_here=routed;move_to_route_stack(stacked_go_here.runtime_entities[static_cast<std::size_t>(routed_id-1)]);
        const auto stacked_candidate=prepare(odh::encode_session_bundle(stacked_go_here),route_fixture);
        require(stacked_candidate->route_stack(routed_id)&&stacked_candidate->dynamic_action_owners(routed_id).size()==1,
            "Go Here marker ownership follows the root route job when persisted as a stack");
        odh::RandomState stacked_rng{999};Execution stacked_execution(stacked_candidate->world(),routed_id,stacked_rng);
        stacked_candidate->install_dynamic_action_owners(routed_id,stacked_execution.vm,
            [](const odh::SessionDynamicActionOwner&){return std::make_pair(go_here_program(),
                std::optional<odh::SessionLinkedSemiglobal>{});});
        auto route_candidate=prepare(routed_bytes,route_fixture);
        require(route_candidate->dynamic_action_owners(routed_id).size()==1&&
            route_candidate->dynamic_action_owners(idle_id).empty()&&
            !route_candidate->local_route(idle_id),"only the routed resident owns Go Here DYNO");
        odh::RandomState route_rng{999};std::uint64_t route_tick=999;odh::EntityVmScheduler route_scheduler{4};
        Execution route_a(route_candidate->world(),1,route_rng),route_b(route_candidate->world(),2,route_rng);
        route_a.schedule(route_scheduler,1);route_b.schedule(route_scheduler,2);
        auto loader=[](const odh::SessionDynamicActionOwner&){return std::make_pair(go_here_program(),
            std::optional<odh::SessionLinkedSemiglobal>{});};
        rejects([&]{route_candidate->install_dynamic_action_owners(idle_id,routed_id==1?route_b.vm:route_a.vm,loader);},
            "idle resident accepted routed resident dynamic owner installation");
        rejects([&]{route_candidate->restore_execution({{1,&route_a.vm,&route_a.controllers,&route_a.playback,{}},
            {2,&route_b.vm,&route_b.controllers,&route_b.playback,{}}},route_scheduler,route_rng,route_tick);},
            "household restore accepted missing routed resident dynamic owner installation");
        require(route_tick==999&&route_rng.seed()==999,"missing owner failure published tick or RNG");

        auto bad_fingerprint=prepare(routed_bytes,route_fixture);
        rejects([&]{bad_fingerprint->install_dynamic_action_owners(routed_id,routed_id==1?route_a.vm:route_b.vm,
            [](const auto&){auto wrong=program();wrong[odh::routine_key(4096)].instructions[0].opcode=1;
                return std::make_pair(std::move(wrong),std::optional<odh::SessionLinkedSemiglobal>{});});},
            "household restore accepted changed dynamic routine fingerprint");
        bad_fingerprint->install_dynamic_action_owners(routed_id,routed_id==1?route_a.vm:route_b.vm,loader);
        Execution wrong_a(route_candidate->world(),1,route_rng),wrong_b(route_candidate->world(),2,route_rng);
        odh::EntityVmScheduler wrong_scheduler{4};wrong_a.schedule(wrong_scheduler,1);wrong_b.schedule(wrong_scheduler,2);
        rejects([&]{bad_fingerprint->restore_execution({{1,&wrong_a.vm,&wrong_a.controllers,&wrong_a.playback,{}},
            {2,&wrong_b.vm,&wrong_b.controllers,&wrong_b.playback,{}}},wrong_scheduler,route_rng,route_tick);},
            "household restore accepted dynamic owner installed into a different VM");
        require(route_tick==999&&route_rng.seed()==999,"wrong VM failure published tick or RNG");

        auto success=prepare(routed_bytes,route_fixture);
        Execution good_a(success->world(),1,route_rng),good_b(success->world(),2,route_rng);
        odh::EntityVmScheduler good_scheduler{4};good_a.schedule(good_scheduler,1);good_b.schedule(good_scheduler,2);
        success->install_dynamic_action_owners(routed_id,routed_id==1?good_a.vm:good_b.vm,loader);
        success->restore_execution({{1,&good_a.vm,&good_a.controllers,&good_a.playback,{}},
            {2,&good_b.vm,&good_b.controllers,&good_b.playback,{}}},good_scheduler,route_rng,route_tick);

        const auto routed_index=static_cast<std::size_t>(routed_id-1);
        const auto idle_index=static_cast<std::size_t>(idle_id-1);
        require(route_tick==routed.host.tick&&route_rng.seed()==routed.host.random_seed,
            "Go Here execution restore did not commit saved tick and RNG");
        auto restored=routed;restored.world=success->world().snapshot();
        restored.runtime_entities={good_a.snapshot(1),good_b.snapshot(2)};
        // Execution captures the VM/controller/playback graph. Local routes and
        // their verified owner descriptors are captured separately by the host.
        for(std::size_t n=0;n<restored.runtime_entities.size();++n) {
            restored.runtime_entities[n].route=routed.runtime_entities[n].route;
            restored.runtime_entities[n].dynamic_action_owners=routed.runtime_entities[n].dynamic_action_owners;
        }
        restored.scheduler=good_scheduler.snapshot();
        auto upgraded=routed;odh::bind_legacy_session_entity_references(upgraded,success->world());
        require(odh::encode_session_bundle(restored)==odh::encode_session_bundle(upgraded),
            "Go Here restore changed execution beyond the explicit legacy reference/ActionTree upgrade");
        require(odh::encode_session_bundle(odh::decode_session_bundle(routed_bytes))==routed_bytes,
            "legacy Go Here value archive must retain its original wire form before live migration");

        // Both continuations are independently coherent and have distinct markers.
        // Reject the unsupported two-walker profile in HouseholdSessionRestore,
        // rather than relying on malformed route identity or archive validation.
        auto mixed=routed;auto ordinary=local_route_bundle(route_fixture,idle_id);
        mixed.runtime_entities[idle_index]=ordinary.runtime_entities[idle_index];
        auto& ordinary_route=*mixed.runtime_entities[idle_index].route;
        const auto& idle_actor=mixed.world.entities[idle_index];
        const auto& routed_actor=mixed.world.entities[routed_index];
        ordinary_route.expected=*idle_actor.position;
        ordinary_route.expected_radian_direction=idle_actor.radian_direction;
        ordinary_route.target_position=*routed_actor.position;
        ordinary_route.path={{ordinary_route.expected.x,ordinary_route.expected.y}};
        mixed.scheduler.entities[idle_index]=ordinary.scheduler.entities[idle_index];
        auto mixed_candidate=prepare(odh::encode_session_bundle(mixed),route_fixture);
        require(mixed_candidate->local_route(idle_id)&&mixed_candidate->dynamic_action_owners(idle_id).empty(),
            "one DYNO owner must preserve another resident's ordinary Route45 profile");

        auto both=routed;auto other=go_here_bundle(route_fixture,idle_id,4);
        odh::World extra_world;require(extra_world.restore_snapshot(routed.world),"restore extra-marker World");
        require(extra_world.create(4,odh::EntityKind::object,{2,0,1,0})&&
            extra_world.define_type(4,0x7c4)&&extra_world.set_ui_ghost(4,true)&&
            extra_world.set_static_footprint(4,false)&&extra_world.set_position(4,{8,88,1,0}),
            "create second coherent Destination marker");
        other.runtime_entities[idle_index].route->target_generation=extra_world.find(4)->generation;
        other.runtime_entities[idle_index].dynamic_action_owners.front().target_generation=extra_world.find(4)->generation;
        both.runtime_entities[idle_index]=other.runtime_entities[idle_index];
        both.world=extra_world.snapshot();
        both.scheduler.entities[idle_index]=other.scheduler.entities[idle_index];
        const auto both_bytes=odh::encode_session_bundle(both);
        require(odh::decode_session_bundle(both_bytes).runtime_entities[idle_index].route.has_value(),
            "two-walker negative fixture must be a coherent session bundle");
        rejects([&]{prepare(both_bytes,route_fixture);},
            "household restore accepted two dynamic Go Here walkers and markers");

        auto extra_marker=routed;
        extra_marker.world=extra_world.snapshot();
        extra_marker.world.entities.back().static_footprint=true;
        const auto extra_marker_bytes=odh::encode_session_bundle(extra_marker);
        rejects([&]{prepare(extra_marker_bytes,route_fixture);},
            "household restore accepted an unrelated extra Destination marker");
        auto missing_marker=routed;missing_marker.world.entities.pop_back();
        rejects([&]{prepare(odh::encode_session_bundle(missing_marker),route_fixture);},
            "household restore accepted a missing Destination marker");
        auto wrong_marker=routed;wrong_marker.world.entities.back().static_footprint=true;
        const auto wrong_marker_bytes=odh::encode_session_bundle(wrong_marker);
        rejects([&]{prepare(wrong_marker_bytes,route_fixture);},
            "household restore accepted a nontemporary Destination marker");
        auto unrelated=routed;
        unrelated.runtime_entities[routed_index].dynamic_action_owners.front().owner++;
        rejects([&]{prepare(odh::encode_session_bundle(unrelated),route_fixture);},
            "household restore accepted an unrelated dynamic owner");
        auto extra=routed;auto extra_owner=extra.runtime_entities[routed_index].dynamic_action_owners[0];
        extra_owner.owner++;extra.runtime_entities[routed_index].vm.stack.back().context.code_owner=extra_owner.owner;
        extra.runtime_entities[routed_index].vm.pending->context.code_owner=extra_owner.owner;
        extra.runtime_entities[routed_index].vm.pending->instruction.code_owner=extra_owner.owner;
        extra.runtime_entities[routed_index].dynamic_action_owners.push_back(extra_owner);
        rejects([&]{prepare(odh::encode_session_bundle(extra),route_fixture);},
            "household restore accepted additional resident DYNO owner");
        extra=routed;extra.runtime_entities[idle_index].route=extra.runtime_entities[routed_index].route;
        rejects([&]{prepare(odh::encode_session_bundle(extra),route_fixture);},
            "household restore accepted a route copied to the wrong resident identity");
        extra=routed;extra.world.entities.back().position->x++;
        rejects([&]{prepare(odh::encode_session_bundle(extra),route_fixture);},
            "household restore accepted changed Destination marker position");
    }
    auto bad=saved;bad.household_roster[1].character_fingerprint[0]^=1;
    rejects([&]{prepare(odh::encode_session_bundle(bad),f);},"changed character hash accepted");
    bad=saved;bad.household_roster[1].neighbor_id=42+1;
    rejects([&]{prepare(odh::encode_session_bundle(bad),f);},"changed neighbor roster accepted");
    bad=saved;bad.household_roster[1].object_definition_id++;
    rejects([&]{prepare(odh::encode_session_bundle(bad),f);},"changed OBJD roster accepted");
    bad=saved;bad.household_roster[1].character_asset="../Characters/evil.iff";
    rejects([&]{odh::encode_session_bundle(bad);},"noncanonical asset path accepted");
    bad=saved;bad.household_roster.pop_back();rejects([&]{odh::encode_session_bundle(bad);},"partial household accepted");
    bad=saved;bad.runtime_entities.pop_back();rejects([&]{odh::encode_session_bundle(bad);},"partial runtime accepted");
    auto archive=odh::decode_session_archive(bytes);
    archive.sections.erase(std::remove_if(archive.sections.begin(),archive.sections.end(),[](const auto& s){return s.id==std::array<char,4>{'H','H','R','D'};}),archive.sections.end());
    rejects([&]{odh::decode_session_bundle(odh::encode_session_archive(archive));},"HOST v2 accepted missing roster");
    rejects([&]{odh::SingleResidentSessionRestore::prepare(bytes,f.resources,f.selection,f.baseline,f.lot,f.bootstrap,21);},"legacy loader accepted household");
    archive=odh::decode_session_archive(bytes);
    for(auto& section:archive.sections)if(section.id==std::array<char,4>{'H','H','R','D'})section.required=false;
    rejects([&]{odh::decode_session_bundle(odh::encode_session_archive(archive));},"optional household roster accepted");
    archive=odh::decode_session_archive(bytes);
    for(auto& section:archive.sections)if(section.id==std::array<char,4>{'H','O','S','T'})section.data[8]=1;
    rejects([&]{odh::decode_session_bundle(odh::encode_session_archive(archive));},"legacy HOST with roster accepted");
    archive=odh::decode_session_archive(bytes);
    for(auto& section:archive.sections)if(section.id==std::array<char,4>{'H','H','R','D'})section.data[0]=2;
    rejects([&]{odh::decode_session_bundle(odh::encode_session_archive(archive));},"unsupported roster version accepted");
    bad=saved;bad.household_roster[1].neighbor_id=bad.household_roster[0].neighbor_id;
    rejects([&]{odh::encode_session_bundle(bad);},"duplicated NBRS member accepted");
    bad=saved;bad.household_roster[1].character_asset=bad.household_roster[0].character_asset;
    rejects([&]{odh::encode_session_bundle(bad);},"duplicated character resource accepted");
    bad=saved;bad.world.entities[1].person_data[0]=1;
    rejects([&]{prepare(odh::encode_session_bundle(bad),f);},"secondary seated state accepted");
    bad=saved;bad.runtime_entities[1].vm.stack[0].context.code_owner=1;
    rejects([&]{prepare(odh::encode_session_bundle(bad),f);},"secondary private owner mismatch accepted");
    auto truncated=bytes;truncated.pop_back();rejects([&]{prepare(truncated,f);},"truncated household accepted");
    // A valid one-resident v2 must be rejected specifically by the legacy profile,
    // even though its old exactly-one-VM restriction would otherwise accept it.
    auto one=saved;one.household_roster.resize(1);one.runtime_entities.resize(1);one.scheduler.entities.resize(1);
    if(one.scheduler.budget_cursor==2)one.scheduler.budget_cursor=1;
    const auto one_bytes=odh::encode_session_bundle(one);
    require(odh::decode_session_bundle(one_bytes).runtime_entities.size()==1,"one-resident v2 fixture is invalid");
    rejects([&]{odh::SingleResidentSessionRestore::prepare(one_bytes,f.resources,f.selection,f.baseline,f.lot,f.bootstrap,21);},
        "legacy single loader accepted valid one-resident household v2");
    one.household_roster.clear();
    require(bool(odh::SingleResidentSessionRestore::prepare(odh::encode_session_bundle(one),f.resources,f.selection,f.baseline,f.lot,f.bootstrap,21)),
        "equivalent one-resident legacy fixture must remain accepted");
    // Carry is a nonempty resource-resolved Playback value owned by resident 2.
    auto carrying=saved;odh::PlaybackResourceKey key;key.source=1;key.id=7;
    key.caller=2;key.callee=2;key.stack_object=2;key.code_owner=2;
    carrying.runtime_entities[1].playback.carry=key;
    auto dynamic_carry=carrying;
    dynamic_carry.runtime_entities[1].playback.carry->code_owner=0x80000000U;
    rejects([&]{prepare(odh::encode_session_bundle(dynamic_carry),f);},
        "secondary carry with unserialized dynamic owner accepted");
    auto carry=prepare(odh::encode_session_bundle(carrying),f);
    odh::RandomState carry_rng{999};std::uint64_t carry_tick=999;
    Execution carry_a(carry->world(),1,carry_rng),carry_b(carry->world(),2,carry_rng);
    odh::EntityVmScheduler carry_scheduler{4};carry_a.schedule(carry_scheduler,1);carry_b.schedule(carry_scheduler,2);
    auto clip=std::make_shared<odh::AnimationHeader>();clip->name="synthetic-secondary-carry";
    std::size_t resolved=0;
    const odh::PlaybackRestoreResolver resolver=[&](const auto& resource)->std::shared_ptr<const odh::AnimationHeader>{
        require(resource==key,"secondary playback resource ownership changed");++resolved;return clip;
    };
    carry->restore_execution({{1,&carry_a.vm,&carry_a.controllers,&carry_a.playback,{}},
        {2,&carry_b.vm,&carry_b.controllers,&carry_b.playback,resolver}},carry_scheduler,carry_rng,carry_tick);
    require(resolved==1&&!carry_a.playback.carry_clip()&&carry_b.playback.carry_clip()==clip,
        "secondary playback was omitted or restored to primary");
    require(carry_b.snapshot(2).playback.carry==carrying.runtime_entities[1].playback.carry,
        "secondary carry snapshot did not survive restore");
    auto missing=prepare(bytes,f);resume(*missing,f.snapshot(),true);
    auto partial=prepare(bytes,f);resume(*partial,f.snapshot(),false,true);
    auto alias=prepare(bytes,f);resume(*alias,f.snapshot(),false,false,true);
    std::cout<<"household roster and atomic-candidate tests passed\n";
}
}
int main(int argc,char** argv){try{if(argc==1){tests();return 0;}require(argc==3,"mode path expected");
    Fixture f;f.start();f.step();if(std::string(argv[1])=="--write"){auto bytes=odh::encode_session_bundle(f.snapshot());
        std::ofstream out(argv[2],std::ios::binary);out.write(reinterpret_cast<const char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));
        require(bool(out),"write archive");return 0;}
    require(std::string(argv[1])=="--resume","unknown mode");auto size=std::filesystem::file_size(argv[2]);require(size<1024*1024,"fixture bound");
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));std::ifstream in(argv[2],std::ios::binary);
    in.read(reinterpret_cast<char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));require(bool(in),"read archive");
    auto candidate=prepare(bytes,f);for(;f.tick<6;)f.step();resume(*candidate,f.snapshot());
    std::cout<<"fresh-process household continuation matches continuous execution\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
