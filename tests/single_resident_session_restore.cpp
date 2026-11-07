#include "saved_avatar_containment_fixture.hpp"
#include "odh/avatar_person_data.hpp"
#include "odh/world_snapshot_codec.hpp"
#include "odh/single_resident_session_restore.hpp"
#include <fstream>
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <filesystem>

namespace {
void require(bool value,const char* text) {if(!value)throw std::runtime_error(text);}
template<class F> void rejects(F operation,const char* text) {
    try{operation();}catch(const odh::FormatError&){return;}
    throw std::runtime_error(text);
}
odh::RoutineSet program() {
    odh::Bhav main{0x8002,{{0,254,255,{}}},{},odh::Bhav::Metadata{0,4,4,0}};
    return {{odh::routine_key(4096),std::move(main)}};
}
std::vector<odh::SessionGlobalValue> capture_globals(const odh::LotContext& lot,
    const odh::Ts1BootstrapGlobals& bootstrap,std::optional<std::int16_t> family) {
    std::vector<odh::SessionGlobalValue> values;
    for(std::int16_t index=0;index<38;++index){
        auto value=index==9?family:std::nullopt;
        if(!value)value=bootstrap.read(index);
        if(!value)value=lot.read(index);
        if(value)values.push_back({static_cast<std::uint16_t>(index),*value});
    }
    return values;
}
struct Fixture {
    odh::World world{odh::WorldLimits{},odh::AttributePolicy::ts1_bounded};
    odh::RandomState random{0x12345678};
    odh::EntityThreadControllers controllers{world,1,[&]{random.next(100);}};
    odh::Playback playback{world,{}};
    odh::Vm vm{odh::RoutineSet{},world.vm_memory(),{},controllers.sleep_primitive(),{{1,program()}}};
    odh::EntityVmScheduler scheduler{4};
    odh::RelationshipState relationships;
    odh::LotContext lot{7,std::vector<std::int16_t>(32)};
    odh::Ts1BootstrapGlobals bootstrap;
    odh::SessionHostState selection;
    odh::SessionResourceIdentity resources;
    odh::WorldSnapshot baseline;
    std::uint64_t tick=0;
    Fixture() {
        require(world.create(1,odh::EntityKind::avatar,{1,88,1,8})&&world.define_type(1,123456),"fixture resident");
        require(world.declare_definition_word(1,20,11)&&world.define_entry_points(1,{{0,4096}}),"fixture immutable metadata");
        require(lot.write(12,321),"fixture mutable lot global");
        baseline=world.snapshot();
        require(relationships.import_object_relationships(world,{{1,{{1,{12,-9}}}}}),"fixture object relationship");
        require(controllers.bind(vm)&&playback.bind(vm),"fixture controllers bind");
        require(scheduler.add(1,vm,[&](std::uint64_t now){return controllers.advance(now);},
            [&]{return controllers.observe();},[&]{return controllers.notify();}),"fixture schedule");
        selection.neighborhood_mode=1;selection.house_number=7;selection.family_chunk_id=21;
        selection.family_number=12;selection.resident_guid=123456;selection.resident_neighbor_id=41;
        selection.runtime_avatar_id=1;resources.resource_fingerprint.fill(0x5a);
    }
    void start() {require(vm.start(odh::routine_key(4096),{1,1,1,1},{3})==odh::VmStatus::running,"start fixture Main");}
    void step() {require(scheduler.tick(++tick,8,8),"fixture tick");}
    odh::SessionBundle snapshot() {
        odh::SessionBundle bundle;bundle.resources=resources;bundle.host=selection;
        bundle.host.tick=tick;bundle.host.random_seed=random.seed();
        bundle.host.global_values=capture_globals(lot,bootstrap,21);
        bundle.host.family_budget=1234;
        bundle.world=world.snapshot();bundle.relationships=relationships.snapshot();
        odh::PlaybackSnapshot state;require(playback.capture_snapshot(state),"capture animation");
        bundle.runtime_entities={{1,vm.snapshot(),controllers.snapshot(),state}};
        bundle.scheduler=scheduler.snapshot();return bundle;
    }
};
auto prepare(const std::vector<std::uint8_t>& bytes,Fixture& source) {
    return odh::SingleResidentSessionRestore::prepare(bytes,source.resources,source.selection,
        source.baseline,source.lot,source.bootstrap,21);
}
void continue_candidate(odh::SingleResidentSessionRestore& candidate,const odh::SessionBundle& expected,
                        bool missing_program=false,bool missing_hooks=false,bool transfer=false) {
    auto transferred=transfer?candidate.take_world():std::unique_ptr<odh::World>{};
    auto& world=transferred?*transferred:candidate.world();
    if(transfer){
        rejects([&]{candidate.take_world();},"candidate World transferred twice");
        rejects([&]{candidate.world();},"transferred World getter allowed stale ownership");
    }
    odh::RandomState random{999};std::uint64_t tick=999;
    odh::EntityThreadControllers controllers(world,1,[&]{random.next(100);});
    odh::Playback playback(world,{});
    odh::Vm vm(odh::RoutineSet{},world.vm_memory(),{},controllers.sleep_primitive(),
        missing_program?odh::RoutineOwners{}:odh::RoutineOwners{{1,program()}});
    require(controllers.bind(vm)&&playback.bind(vm),"bind candidate before Main");
    odh::EntityVmScheduler scheduler(4);
    if(missing_hooks)require(scheduler.add(1,vm),"register incomplete candidate");
    else require(scheduler.add(1,vm,[&](std::uint64_t now){return controllers.advance(now);},
        [&]{return controllers.observe();},[&]{return controllers.notify();}),"register candidate");
    if(missing_program||missing_hooks) {
        rejects([&]{candidate.restore_execution(vm,controllers,playback,scheduler,{},random,tick);},"incomplete execution graph accepted");
        require(random.seed()==999&&tick==999,"failure published RNG/tick");return;
    }
    candidate.restore_execution(vm,controllers,playback,scheduler,{},random,tick);
    require(tick==1&&vm.status()==odh::VmStatus::waiting,"restore actual Sleep continuation");
    rejects([&]{candidate.restore_execution(vm,controllers,playback,scheduler,{},random,tick);},"candidate restored twice");
    for(;tick<6;)require(scheduler.tick(++tick,8,8),"restored tick");
    odh::PlaybackSnapshot state;require(playback.capture_snapshot(state),"capture resumed animation");
    auto resumed=expected;resumed.world=world.snapshot();resumed.relationships=candidate.relationships().snapshot();
    resumed.runtime_entities={{1,vm.snapshot(),controllers.snapshot(),state}};
    resumed.scheduler=scheduler.snapshot();resumed.host.tick=tick;resumed.host.random_seed=random.seed();
    resumed.host.global_values=capture_globals(candidate.lot(),candidate.bootstrap(),candidate.family_global());
    require(resumed.host.global_values.size()==expected.host.global_values.size(),"resumed global roster changed");
    for(std::size_t n=0;n<resumed.host.global_values.size();++n)
        require(resumed.host.global_values[n].index==expected.host.global_values[n].index&&
            resumed.host.global_values[n].value==expected.host.global_values[n].value,"resumed global value changed");
    require(odh::encode_session_bundle(resumed)==odh::encode_session_bundle(expected),
        "restored continuation differs from uninterrupted simulation");
}
void restore_carry(odh::SingleResidentSessionRestore& candidate,const odh::PlaybackResourceKey& key,bool resolves) {
    odh::RandomState random{999};std::uint64_t tick=999;
    odh::EntityThreadControllers controllers(candidate.world(),1);
    odh::Playback playback(candidate.world(),{});
    odh::Vm vm(odh::RoutineSet{},candidate.world().vm_memory(),{},controllers.sleep_primitive(),{{1,program()}});
    require(controllers.bind(vm)&&playback.bind(vm),"bind carry candidate");
    odh::EntityVmScheduler scheduler(4);
    require(scheduler.add(1,vm,[&](std::uint64_t now){return controllers.advance(now);},
        [&]{return controllers.observe();},[&]{return controllers.notify();}),"carry scheduler");
    auto clip=std::make_shared<odh::AnimationHeader>();clip->name="synthetic-carry";
    std::size_t calls=0;
    const odh::PlaybackRestoreResolver resolver=[&](const auto& resource)->std::shared_ptr<const odh::AnimationHeader>{
        ++calls;require(resource==key,"exact Playback resource key lost");return resolves?clip:nullptr;
    };
    if(resolves){
        candidate.restore_execution(vm,controllers,playback,scheduler,resolver,random,tick);
        require(playback.carry_clip()==clip&&calls==1,"carry resource not resolved exactly once");
    }else {
        rejects([&]{candidate.restore_execution(vm,controllers,playback,scheduler,resolver,random,tick);},
            "missing animation resource accepted");
        require(random.seed()==999&&tick==999&&calls==1,"failed animation published host tick/RNG");
    }
}
odh::SessionBundle body_turn_bundle(Fixture& fixture,std::uint8_t mode){
    auto bundle=fixture.snapshot();odh::World world;
    require(world.restore_snapshot(bundle.world)&&world.set_position(1,{16,16,1,0}),"position legacy body-turn caller");
    bundle.world=world.snapshot();auto& state=bundle.runtime_entities.front();auto& vm=state.vm;
    const odh::VmContext context{1,1,1,1};
    vm.status=odh::VmStatus::waiting;vm.steps=2;vm.ticket_sequence=1;
    vm.stack={{odh::routine_key(4096),0,context,std::vector<std::int16_t>(4),std::vector<std::int16_t>(4)}};
    const odh::Instruction instruction{22,254,255,{mode,9,8,7,6,5,4,3}};
    vm.pending=odh::VmPending{1,odh::VmTrace{1,1,odh::routine_key(4096),0,instruction,1},context};
    state.controllers.interactions.actor_generation=world.find(1)->generation;
    state.controllers.interactions.run_generation=vm.run_generation;
    state.route.emplace();auto& route=*state.route;
    route.ticket=1;route.actor=1;route.target=1;route.actor_generation=world.find(1)->generation;
    route.target_generation=route.actor_generation;route.run_generation=vm.run_generation;
    route.expected=*world.find(1)->position;route.target_position=route.expected;
    route.frame.state=odh::RouteFrameState::turn_only;route.path={{16,16}};route.next=1;
    route.entry_flags=1;route.facing=(mode&1U)?4:0;route.same_room_verified=true;
    route.look_towards.emplace();route.look_towards->mode=mode;
    route.look_towards->target_radian_direction=(mode&1U)?3.14159265358979323846f:0;
    auto& scheduled=bundle.scheduler.entities.front();scheduled.status=vm.status;
    scheduled.steps=vm.steps;scheduled.run_generation=vm.run_generation;
    return bundle;
}
void body_turn_tests(){
    for(const std::uint8_t mode:{std::uint8_t{2},std::uint8_t{3},std::uint8_t{4},std::uint8_t{5}}){
        Fixture fixture;fixture.start();const auto body=body_turn_bundle(fixture,mode);
        const auto bytes=odh::encode_session_bundle(body);const auto seed=fixture.random.seed();
        auto candidate=prepare(bytes,fixture);
        auto stack_body=body;auto& stack_state=stack_body.runtime_entities.front();
        odh::RouteJobSnapshot job;static_cast<odh::LocalRouteSnapshot&>(job)=*stack_state.route;
        job.pending=*stack_state.vm.pending;job.route_frame=stack_state.vm.stack.back();
        stack_state.route_stack.emplace();stack_state.route_stack->jobs.push_back(job);stack_state.route.reset();
        const auto stack_candidate=prepare(odh::encode_session_bundle(stack_body),fixture);
        require(stack_candidate->route_stack(1)&&stack_candidate->route_stack(1)->jobs.front().look_towards,
            "legacy single-resident profile recognizes its own verified body-turn stack without admitting arbitrary owners");
        const auto* route=candidate->local_route(1);
        require(route&&route->look_towards&&route->look_towards->mode==mode&&route->target==1&&fixture.random.seed()==seed,
            "legacy boundary admits only the exact frozen body-turn marker without source RNG draws");
        auto malformed=body;malformed.runtime_entities.front().route->look_towards.reset();
        rejects([&]{prepare(odh::encode_session_bundle(malformed),fixture);},"legacy pending22 requires its body-turn marker");
        malformed=body;malformed.runtime_entities.front().vm.pending->instruction.instruction.operands[0]=0;
        rejects([&]{prepare(odh::encode_session_bundle(malformed),fixture);},"head22 cannot borrow body-turn restore authority");
        malformed=body;malformed.runtime_entities.front().route->ticket++;
        rejects([&]{prepare(odh::encode_session_bundle(malformed),fixture);},"legacy body-turn marker rejects mismatched ticket");
        // Restore the pending instruction into the actual fresh execution
        // graph. Materializing a route remains a host step and performs no VM
        // dispatch or random draw inside the session candidate.
        odh::RandomState random{999};std::uint64_t tick=999;
        odh::EntityThreadControllers controllers(candidate->world(),1);odh::Playback playback(candidate->world(),{});
        auto routines=program();routines.at(odh::routine_key(4096)).instructions[0]=body.runtime_entities.front().vm.pending->instruction.instruction;
        odh::Vm vm({},candidate->world().vm_memory(),{},controllers.sleep_primitive(),{{1,routines}});
        require(controllers.bind(vm)&&playback.bind(vm),"bind legacy body-turn execution graph");
        odh::EntityVmScheduler scheduler(4);
        require(scheduler.add(1,vm,[&](auto now){return controllers.advance(now);},
            [&]{return controllers.observe();},[&]{return controllers.notify();}),"schedule restored legacy body-turn actor");
        candidate->restore_execution(vm,controllers,playback,scheduler,{},random,tick);
        require(vm.status()==odh::VmStatus::waiting&&vm.pending()&&vm.pending()->instruction.instruction.opcode==22&&
            vm.steps()==body.runtime_entities.front().vm.steps&&random.seed()==body.host.random_seed&&tick==body.host.tick&&
            candidate->local_route(1)->look_towards->mode==mode,
            "legacy body-turn restore preserves pending source PC, VM work and host clock/RNG without materializing or running the route");
    }
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
void dialog_restore_case() {
    Fixture source;source.start();source.step();auto bundle=source.snapshot();
    auto& state=bundle.runtime_entities.front();state.controllers.sleep.job.reset();
    odh::Instruction instruction{36,254,255,{}};instruction.operands[5]=1;
    state.vm.pending->instruction.instruction=instruction;
    require(source.world.bind_context_live(state.vm.stack.back().context),"bind dialog source frame");
    state.vm.pending->context=state.vm.stack.back().context;
    odh::DialogState dialog;dialog.thread_owner={1,source.world.find(1)->generation};
    dialog.source=state.vm.stack.back();dialog.instruction=instruction;
    dialog.ticket=state.vm.pending->ticket;dialog.run_generation=state.vm.run_generation;
    dialog.prompt.title="Serviço";dialog.prompt.message="Contratar?";dialog.prompt.yes="Sim";dialog.prompt.no="Não";
    bundle.dialog=dialog;
    auto candidate=prepare(odh::encode_session_bundle(bundle),source);
    odh::DialogServices services;services.thread_owner=[&](const odh::Vm&)->std::optional<odh::WorldEntityReference>{
        return odh::WorldEntityReference{1,candidate->world().find(1)->generation};
    };
    odh::DialogController dialogs(candidate->world(),std::move(services));
    odh::EntityThreadControllers controllers(candidate->world(),1);
    odh::Playback playback(candidate->world(),{});
    auto routines=program();routines.at(odh::routine_key(4096)).instructions.front()=instruction;
    odh::Vm vm({},candidate->world().vm_memory(),{},odh::dialog_primitive(dialogs),{{1,std::move(routines)}});
    require(controllers.bind(vm)&&playback.bind(vm),"bind restored dialog execution");
    odh::EntityVmScheduler scheduler(4);
    require(scheduler.add(1,vm,[&](std::uint64_t now){return controllers.advance(now);},
        [&]{return controllers.observe();},[&]{return controllers.notify();}),"register restored dialog");
    odh::RandomState random(999);std::uint64_t tick=999;
    candidate->restore_execution(vm,controllers,playback,scheduler,{},random,tick);
    require(candidate->dialog().has_value()&&vm.status()==odh::VmStatus::waiting,
        "single-resident restore preserves an authenticated dialog wait without a Sleep job");
    std::string diagnostic;
    require(dialogs.restore(*candidate->dialog(),vm,diagnostic)&&
        dialogs.respond(dialog.ticket,{1,""},diagnostic)&&vm.status()==odh::VmStatus::returned_false,
        "restored No response resumes the actual saved instruction once");
    require(!dialogs.respond(dialog.ticket,{0,""},diagnostic),"restored dialog cannot be answered twice");
}

void unit_tests() {
    dialog_restore_case();
    legacy_avatar_containment_tests();
    body_turn_tests();
    Fixture source;source.start();odh::VmFrame attribute_frame;
    attribute_frame.context.caller=1;attribute_frame.context.stack_object=1;
    require(source.world.write(attribute_frame,0,4,99),"TS1 attribute bank grows on write");
    source.step();const auto saved=source.snapshot();
    require(saved.world.entities[0].attributes.size()==5&&saved.world.entities[0].attributes[4]==99,
        "grown TS1 attribute bank is captured");
    const auto bytes=odh::encode_session_bundle(saved);
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
        auto restored=prepare(encoded,source);
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
        rejects([&]{prepare(odh::encode_session_bundle(mixed),source);},"mixed typed and legacy global authorities accepted");
        auto wrong=typed;wrong.host.family_chunk_id=22;
        rejects([&]{prepare(odh::encode_session_bundle(wrong),source);},"typed globals bypassed actual family selection lease");
    }

    {
        auto with_sim=saved;with_sim.simulation.emplace();
        odh::Ts1Clock clock;clock.initialize(7,17,4,6,1997);auto time=clock.snapshot();time.ticks=with_sim.host.tick;
        with_sim.simulation->clock=time;clock.restore(time);
        for(const auto& runtime:with_sim.runtime_entities)with_sim.simulation->avatars.push_back({runtime.id,{}});
        for(std::int16_t i=0;i<9;++i)if(const auto v=clock.global(i))with_sim.host.global_values.push_back({static_cast<std::uint16_t>(i),*v});
        std::sort(with_sim.host.global_values.begin(),with_sim.host.global_values.end(),[](const auto& a,const auto& b){return a.index<b.index;});
        auto restored_sim=prepare(odh::encode_session_bundle(with_sim),source);
        require(restored_sim->simulation() && restored_sim->simulation()->clock.minute==4 && !restored_sim->lot().read(0),
            "SIMS globals are accepted and remain derived outside LotContext");
        with_sim.host.global_values.erase(with_sim.host.global_values.begin());
        rejects([&]{prepare(odh::encode_session_bundle(with_sim),source);},"SIMS missing clock global accepted");
    }
    auto candidate=prepare(bytes,source);
    require(candidate->world().find(1)->attributes.size()==5&&candidate->world().find(1)->attributes[4]==99,
        "grown TS1 attribute bank is restored");
    require(candidate->family_budget()==std::optional<std::int32_t>{1234},
        "single-resident session restores the active family's runtime budget");
    require(candidate->bootstrap().read(17)==source.bootstrap.read(17)&&
        candidate->lot().read(10)==7&&candidate->lot().read(12)==321&&candidate->family_global()==21,"global restore");
    require(candidate->relationships().object_value(candidate->world(),1,1,1)==-9,"restore nonempty relationship values");
    for(;source.tick<6;)source.step();const auto expected=source.snapshot();
    continue_candidate(*candidate,expected);
    auto transferred=prepare(bytes,source);
    continue_candidate(*transferred,expected,false,false,true);
    auto changed=saved;
    changed.resources.resource_fingerprint[0]^=1;
    rejects([&]{prepare(odh::encode_session_bundle(changed),source);},"wrong resources accepted");
    changed=saved;changed.host.house_number=8;
    rejects([&]{prepare(odh::encode_session_bundle(changed),source);},"wrong lot accepted");
    changed=saved;changed.world.entities[0].definition_words[20]=22;
    rejects([&]{prepare(odh::encode_session_bundle(changed),source);},"modified immutable metadata accepted");
    changed=saved;changed.world.entities[0].person_data[0]=1;
    rejects([&]{prepare(odh::encode_session_bundle(changed),source);},"seated posture accepted without resume binding");
    changed=saved;
    for(auto& entry:changed.host.global_values)if(entry.index==9)entry.value=22;
    rejects([&]{prepare(odh::encode_session_bundle(changed),source);},"altered family global accepted");
    changed=saved;
    for(auto& entry:changed.host.global_values)if(entry.index==10)entry.value=8;
    rejects([&]{prepare(odh::encode_session_bundle(changed),source);},"altered lot global accepted");
    changed=saved;changed.host.global_values.pop_back();
    rejects([&]{prepare(odh::encode_session_bundle(changed),source);},"dropped global accepted");
    changed=saved;changed.host.global_values.push_back({99,42});
    rejects([&]{prepare(odh::encode_session_bundle(changed),source);},"unsupported global accepted");
    changed=saved;changed.world.entities[0].static_footprint=false;
    rejects([&]{prepare(odh::encode_session_bundle(changed),source);},"routing footprint accepted");
    changed=saved;changed.runtime_entities[0].vm.stack[0].context.code_owner=2;
    rejects([&]{prepare(odh::encode_session_bundle(changed),source);},"dynamic owner accepted");
    auto truncated=bytes;truncated.pop_back();
    rejects([&]{prepare(truncated,source);},"truncated archive accepted");
    odh::SessionBundleLimits limits;limits.archive.max_file_bytes=8;
    rejects([&]{odh::SingleResidentSessionRestore::prepare(bytes,source.resources,source.selection,
        source.baseline,source.lot,source.bootstrap,21,limits);},"archive byte limit ignored");
    auto no_program=prepare(bytes,source);continue_candidate(*no_program,expected,true);
    auto no_hooks=prepare(bytes,source);continue_candidate(*no_hooks,expected,false,true);
    changed=saved;
    odh::PlaybackResourceKey key;key.source=1;key.id=7;key.caller=1;key.callee=1;key.stack_object=1;key.code_owner=1;
    changed.runtime_entities[0].playback.carry=key;
    auto carry_bytes=odh::encode_session_bundle(changed);
    auto carry=prepare(carry_bytes,source);restore_carry(*carry,key,true);
    auto missing_carry=prepare(carry_bytes,source);restore_carry(*missing_carry,key,false);
    // Mutable globals retain zero values; family and lot identity remain selected.
    changed=saved;for(auto& entry:changed.host.global_values)if(entry.index!=9&&entry.index!=10)entry.value=0;
    auto zeros=prepare(odh::encode_session_bundle(changed),source);
    require(zeros->family_global()==21&&zeros->bootstrap().read(17)==0&&zeros->lot().read(10)==7,"zero globals silently dropped");
    std::cout<<"single resident restore: continuation and rejection cases passed\n";
}
}
int main(int argc,char** argv) {
    try {
        if(argc==1){unit_tests();return 0;}
        require(argc==3,"usage: --write|--resume FILE");
        Fixture source;source.start();source.step();
        if(std::string(argv[1])=="--write") {
            const auto bytes=odh::encode_session_bundle(source.snapshot());
            std::ofstream out(argv[2],std::ios::binary|std::ios::trunc);
            out.write(reinterpret_cast<const char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));
            require(bool(out),"write synthetic session fixture");return 0;
        }
        require(std::string(argv[1])=="--resume","unknown test mode");
        const auto size=std::filesystem::file_size(argv[2]);require(size<1024*1024,"test fixture too large");
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
        std::ifstream in(argv[2],std::ios::binary);in.read(reinterpret_cast<char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));
        require(bool(in),"read synthetic prior-process session fixture");
        auto candidate=prepare(bytes,source);
        for(;source.tick<6;)source.step();continue_candidate(*candidate,source.snapshot());
        std::filesystem::remove(argv[2]);std::cout<<"separate-process continuation matched uninterrupted execution\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
