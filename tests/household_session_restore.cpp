#include "odh/household_session_restore.hpp"
#include "odh/single_resident_session_restore.hpp"
#include "odh/seated_session_continuation.hpp"
#include <fstream>
#include <algorithm>
#include <filesystem>
#include <iostream>
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
    Fixture(){
        for(std::int16_t id:{1,2}){require(world.create(id,odh::EntityKind::avatar,{1,88,1,8})&&
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
void generic_execution_tests(){
    Fixture f;f.start();f.step();auto b=f.snapshot();b.generic_execution=true;
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
void tests(){generic_execution_tests();Fixture f;f.start();odh::VmFrame attribute_frame;
    attribute_frame.context.caller=1;attribute_frame.context.stack_object=1;
    require(f.world.write(attribute_frame,0,4,99),"TS1 attribute bank grows on write");
    f.step();auto saved=f.snapshot();const auto bytes=odh::encode_session_bundle(saved);
    {
        auto with_sim=saved;with_sim.simulation.emplace();
        odh::Ts1Clock clock;clock.initialize(7,17,4,6,1997);auto time=clock.snapshot();time.ticks=with_sim.host.tick;
        with_sim.simulation->clock=time;clock.restore(time);
        for(const auto& runtime:with_sim.runtime_entities)with_sim.simulation->avatars.push_back({runtime.id,{}});
        for(std::int16_t i=0;i<9;++i)if(const auto v=clock.global(i))with_sim.host.global_values.push_back({static_cast<std::uint16_t>(i),*v});
        std::sort(with_sim.host.global_values.begin(),with_sim.host.global_values.end(),[](const auto& a,const auto& b){return a.index<b.index;});
        auto restored_sim=prepare(odh::encode_session_bundle(with_sim),f);
        require(restored_sim->simulation() && restored_sim->simulation()->clock.minute==4 && !restored_sim->lot().read(0),
            "SIMS globals are accepted and remain derived outside LotContext");
        with_sim.host.global_values.erase(with_sim.host.global_values.begin());
        rejects([&]{prepare(odh::encode_session_bundle(with_sim),f);},"SIMS missing clock global accepted");
    }
    for(const auto id:{std::int16_t{1},std::int16_t{2}}){
        Fixture local;local.start();auto route=local_route_bundle(local,id);
        auto accepted=prepare(odh::encode_session_bundle(route),local);
        require(accepted->local_route(id)&&accepted->dynamic_action_owners(id).empty(),
            "existing primary and secondary local Route45 profiles remain accepted without DYNO");
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
        require(odh::encode_session_bundle(restored)==routed_bytes,
            "Go Here restore changed the routed or idle resident execution state");

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
