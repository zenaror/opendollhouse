#include "odh/seated_session_continuation.hpp"
#include "odh/single_resident_session_restore.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class F> void rejects(F operation,const char* message){
    try{operation();}catch(const odh::FormatError&){return;}
    throw std::runtime_error(message);
}
odh::RoutineSet action(std::uint16_t id){
    return {{odh::routine_key(id),odh::Bhav{0x8002,{{2,254,255,{}}},{},odh::Bhav::Metadata{0,4,4,0}}}};
}
odh::RoutineSet main_program(){
    return {{odh::routine_key(4096),odh::Bhav{0x8002,{{0,254,255,{}}},{},odh::Bhav::Metadata{0,4,4,0}}}};
}
odh::RoutineSet animation_main_program(){
    return {{odh::routine_key(4096),odh::Bhav{0x8002,
        {{44,254,255,{7,0,0,0,1,0,0,0}}},{},odh::Bhav::Metadata{0,4,4,0}}}};
}
struct Fixture {
    odh::World world;
    odh::EntityThreadControllers controllers{world,1};
    odh::Vm vm{odh::RoutineSet{},world.vm_memory(),{},controllers.sleep_primitive(),{{1,main_program()}}};
    odh::EntityVmScheduler scheduler{4};
    odh::LotContext lot{7,std::vector<std::int16_t>(32)};
    odh::Ts1BootstrapGlobals bootstrap;
    odh::DefinitionCandidate definition{};
    odh::SessionBundle bundle;
    Fixture(){
        require(world.create(1,odh::EntityKind::avatar,{0,88,0})&&world.define_type(1,123),"actor");
        require(world.create(2,odh::EntityKind::object,{0,0,1})&&world.define_type(2,456),"chair");
        std::vector<odh::EntryPoint> eps(28);eps[27]={0,4102};
        require(world.define_entry_points(2,eps)&&world.set_position(1,{16,16,1,0})&&world.set_position(2,{32,48,1,0})&&
            world.attach(2,0,1)&&world.place_in_slot(2,0,1)&&world.set_person_data(1,0,1),"seated chair state");
        require(vm.start(odh::routine_key(4096),{1,1,1,1},{6})==odh::VmStatus::running&&controllers.bind(vm),"start Main");
        require(scheduler.add(1,vm,[&](std::uint64_t now){return controllers.advance(now);},
            [&]{return controllers.observe();},[&]{return controllers.notify();}),"scheduler");
        require(scheduler.tick(1,8,8)&&scheduler.tick(2,8,8),"Main stable wait");
        bundle.host.neighborhood_mode=1;bundle.host.house_number=7;bundle.host.family_chunk_id=21;
        bundle.host.family_number=12;bundle.host.resident_guid=123;bundle.host.runtime_avatar_id=1;bundle.host.tick=2;
        for(std::int16_t index=0;index<38;++index){
            auto value=index==9?std::optional<std::int16_t>{21}:bootstrap.read(index);
            if(!value)value=lot.read(index);
            if(value)bundle.host.global_values.push_back({static_cast<std::uint16_t>(index),*value});
        }
        bundle.resources.resource_fingerprint.fill(1);bundle.world=world.snapshot();bundle.scheduler=scheduler.snapshot();
        bundle.runtime_entities={{1,vm.snapshot(),controllers.snapshot(),{}}};
        auto& queue=bundle.runtime_entities[0].controllers.interactions;queue.sequence=1;queue.completion=odh::InteractionCompletion{1,1,1,true};
        odh::PlaybackResourceKey carry{};carry.source=1;carry.id=7;carry.caller=1;carry.callee=2;carry.stack_object=2;carry.code_owner=0x80000000U;
        bundle.runtime_entities[0].playback.carry=carry;
        odh::SessionSeatedContinuation c;c.actor_id=1;c.target_id=2;c.actor_generation=world.find(1)->generation;
        c.target_generation=world.find(2)->generation;c.target_guid=456;c.completion=*queue.completion;
        c.source="synthetic/exact.far";c.asset="chair.iff";c.definition_chunk_id=5;c.tree_table_id=8;
        c.sit_routine_fingerprint=odh::session_action_routine_fingerprint(action(4097));
        c.stand_routine_fingerprint=odh::session_action_routine_fingerprint(action(4102));
        c.code_owners={{0x80000000U,c.source,c.asset,4097,c.sit_routine_fingerprint}};
        bundle.seated_continuation=c;
        definition.source=c.source;definition.asset=c.asset;definition.chunk_id=5;
        definition.definition.guid=456;definition.definition.tree_table_id=8;
    }
    auto resolve(const odh::SessionBundle& value){return odh::resolve_seated_session_owners(value,definition,
        *world.find(2)->entry_points,[](const auto&,auto id){return action(id);});}
};
odh::SessionBundle animation_main_fixture(Fixture& source) {
    odh::World world;require(world.restore_snapshot(source.bundle.world),"animation fixture World");
    odh::EntityThreadControllers controllers(world,1);
    auto clip=std::make_shared<odh::AnimationHeader>();clip->name="synthetic-seated-Main-idle";
    odh::BoneMotion motion{};motion.frames=3;motion.time_properties={{{0,{{"xevt","100"}}}}};
    clip->motions.push_back(motion);
    odh::Playback playback(world,[&](const auto&,auto,auto){return clip;});
    odh::Vm vm(odh::RoutineSet{},world.vm_memory(),{},playback.primitive(),{{1,animation_main_program()}});
    require(vm.start(odh::routine_key(4096),{1,1,1,1})==odh::VmStatus::running&&
        controllers.bind(vm)&&playback.bind(vm),"bind seated Main animation");
    require(vm.run(8)==odh::VmStatus::waiting,"Main animation reaches pending opcode 44");
    odh::EntityVmScheduler scheduler(4);
    require(scheduler.add(1,vm,[&](std::uint64_t tick){return controllers.advance(tick);},
        [&]{return controllers.observe();},[&]{return controllers.notify();})&&
        scheduler.tick(1,8,8)&&scheduler.tick(2,8,8),"animation Main scheduler boundary");
    auto bundle=source.bundle;odh::PlaybackSnapshot snapshot;
    require(playback.capture_snapshot(snapshot)&&snapshot.state&&snapshot.state->wait,"capture Main-owned animation wait");
    bundle.runtime_entities={{1,vm.snapshot(),controllers.snapshot(),snapshot}};
    auto& queue=bundle.runtime_entities[0].controllers.interactions;
    queue.sequence=1;queue.completion=odh::InteractionCompletion{1,1,1,true};
    bundle.scheduler=scheduler.snapshot();bundle.seated_continuation->code_owners.clear();
    return bundle;
}
}
int main(){try{
    Fixture f;const auto bytes=odh::encode_session_bundle(f.bundle);const auto loaded=odh::decode_session_bundle(bytes,f.bundle.resources);
    require(odh::encode_session_bundle(loaded)==bytes&&f.resolve(loaded).count(0x80000000U),"canonical seated owner roundtrip");
    std::size_t rejections=0;
    const auto bad=[&](auto mutate){auto changed=f.bundle;mutate(changed);rejects([&]{
        odh::validate_seated_session_continuation(changed);},"invalid seated continuation accepted");++rejections;};
    bad([](auto& b){++b.seated_continuation->actor_generation;});
    bad([](auto& b){++b.seated_continuation->target_generation;});
    bad([](auto& b){++b.seated_continuation->target_guid;});
    bad([](auto& b){++b.seated_continuation->target_slot;});
    bad([](auto& b){b.world.entities[1].slots[0]=0;});
    bad([](auto& b){b.world.entities[0].attachment->positioned=false;});
    bad([](auto& b){b.world.entities[0].person_data[0]=0;});
    bad([](auto& b){b.seated_continuation->completion.result=false;});
    bad([](auto& b){++b.seated_continuation->completion.uid;});
    bad([](auto& b){b.seated_continuation->completion.tick=b.host.tick;});
    bad([](auto& b){b.seated_continuation->action=4098;});
    bad([](auto& b){b.seated_continuation->stand_action=4103;});
    bad([](auto& b){b.world.entities[1].entry_points->at(27).action=4103;});
    bad([](auto& b){b.seated_continuation->code_owners.clear();});
    bad([](auto& b){b.seated_continuation->code_owners[0].owner=1;});
    bad([](auto& b){b.seated_continuation->code_owners[0].asset="other.iff";});
    bad([](auto& b){b.seated_continuation->code_owners[0].root_routine=4102;});
    bad([](auto& b){b.seated_continuation->source=std::string(4097,'x');});
    bad([](auto& b){b.seated_continuation->source.push_back('\0');});
    bad([](auto& b){b.runtime_entities[0].controllers.interactions.active=odh::InteractionActiveSnapshot{1,1};});
    bad([](auto& b){b.runtime_entities[0].controllers.interactions.resume_tick=3;});
    bad([](auto& b){b.runtime_entities[0].vm.stack[0].context.code_owner=0x80000000U;});
    bad([](auto& b){b.runtime_entities[0].vm.pending->instruction.instruction.opcode=44;});
    auto wrong=f.definition;wrong.asset="other.iff";
    rejects([&]{odh::resolve_seated_session_owners(f.bundle,wrong,*f.world.find(2)->entry_points,
        [](const auto&,auto id){return action(id);});},"wrong exact asset accepted");
    wrong=f.definition;++wrong.chunk_id;
    rejects([&]{odh::resolve_seated_session_owners(f.bundle,wrong,*f.world.find(2)->entry_points,
        [](const auto&,auto id){return action(id);});},"wrong definition chunk accepted");
    rejects([&]{odh::resolve_seated_session_owners(f.bundle,f.definition,*f.world.find(2)->entry_points,
        [](const auto&,auto id){auto routines=action(id);routines.begin()->second.instructions[0].operands[0]=1;return routines;});},
        "changed private closure accepted with same source/asset/owner and overall manifest");
    rejects([&]{odh::resolve_seated_session_owners(f.bundle,f.definition,*f.world.find(2)->entry_points,
        [](const auto&,auto id){auto routines=action(id);if(id==4102)routines.begin()->second.header[0]=1;return routines;});},
        "changed Stand closure accepted with unchanged Sit closure");
    auto archive=odh::decode_session_archive(bytes);
    auto seat=std::find_if(archive.sections.begin(),archive.sections.end(),[](const auto& s){return s.id==std::array<char,4>{{'S','E','A','T'}};});
    require(seat!=archive.sections.end(),"SEAT section exists");seat->data[0]=2;
    rejects([&]{odh::decode_session_bundle(odh::encode_session_archive(archive));},"unknown SEAT payload version accepted");
    archive=odh::decode_session_archive(bytes);archive.sections.back().required=false;
    rejects([&]{odh::decode_session_bundle(odh::encode_session_archive(archive));},"optional SEAT section accepted");
    // The generic candidate can own seated World values, but its execution is
    // gated until exact closures have been verified and installed in this VM.
    auto baseline=f.bundle.world;baseline.entities[0].attachment.reset();baseline.entities[0].person_data[0]=0;
    baseline.entities[1].slots[0]=0;
    const auto restore_case=[&](bool verify,bool wrong_vm){
        auto candidate=odh::SingleResidentSessionRestore::prepare(bytes,f.bundle.resources,f.bundle.host,
            baseline,f.lot,f.bootstrap,21);
        odh::EntityThreadControllers controllers(candidate->world(),1);
        odh::Playback playback(candidate->world(),{});
        odh::Vm vm(odh::RoutineSet{},candidate->world().vm_memory(),{},controllers.sleep_primitive(),{{1,main_program()}});
        require(controllers.bind(vm)&&playback.bind(vm),"bind fresh seated execution");
        odh::EntityVmScheduler scheduler(4);
        require(scheduler.add(1,vm,[&](std::uint64_t now){return controllers.advance(now);},
            [&]{return controllers.observe();},[&]{return controllers.notify();}),"register fresh seated execution");
        if(verify)candidate->install_seated_continuation_owners(vm,f.definition,*f.world.find(2)->entry_points,
            [](const auto&,auto id){return action(id);});
        odh::RandomState random{999};std::uint64_t tick=999;
        auto clip=std::make_shared<odh::AnimationHeader>();clip->name="synthetic-seated-carry";
        if(!verify||wrong_vm){
            odh::Vm other{odh::RoutineSet{}};
            rejects([&]{candidate->restore_execution(wrong_vm?other:vm,controllers,playback,scheduler,
                [&](const auto&){return clip;},random,tick);},"unverified or different VM accepted seated owners");
            require(tick==999&&random.seed()==999,"failed seated restore published host state");
        }else{
            candidate->restore_execution(vm,controllers,playback,scheduler,[&](const auto&){return clip;},random,tick);
            require(tick==2&&vm.status()==odh::VmStatus::waiting&&playback.carry_clip()==clip&&
                candidate->world().find(1)->attachment->container==2,"verified seated execution restored");
            require(scheduler.tick(3,8,8),"seated Main continued at following logical tick");
        }
    };
    restore_case(false,false);restore_case(true,true);restore_case(true,false);
    const auto animation=animation_main_fixture(f);
    const auto animation_bytes=odh::encode_session_bundle(animation);
    const auto animation_loaded=odh::decode_session_bundle(animation_bytes,f.bundle.resources);
    require(odh::encode_session_bundle(animation_loaded)==animation_bytes&&
        animation_loaded.runtime_entities[0].playback.state->wait&&f.resolve(animation_loaded).empty(),
        "completed seated Sit accepts a verifiable Main-owned animation wait");
    const auto bad_animation=[&](auto mutate){auto changed=animation;mutate(changed);
        rejects([&]{odh::validate_seated_session_continuation(changed);},"invalid Main animation wait accepted");++rejections;};
    bad_animation([](auto& b){b.runtime_entities[0].playback.state->resource.code_owner=0x80000000U;});
    bad_animation([](auto& b){b.runtime_entities[0].vm.pending->context.code_owner=0x80000000U;});
    bad_animation([](auto& b){++b.runtime_entities[0].playback.state->wait->ticket;});
    bad_animation([](auto& b){++b.runtime_entities[0].playback.state->wait->expected;});
    bad_animation([](auto& b){++b.runtime_entities[0].playback.state->wait->index;});
    bad_animation([](auto& b){b.runtime_entities[0].playback.state->wait->bank=odh::VmFrameBank::locals;});
    bad_animation([](auto& b){++b.runtime_entities[0].playback.state->resource.id;});
    bad_animation([](auto& b){++b.runtime_entities[0].playback.state->resource.callee;});
    bad_animation([](auto& b){++b.runtime_entities[0].playback.state->vm_generation;});
    bad_animation([](auto& b){b.runtime_entities[0].vm.pending->instruction.instruction.operands[5]=1;});
    bad_animation([](auto& b){b.runtime_entities[0].vm.pending.reset();});
    bad_animation([](auto& b){b.runtime_entities[0].playback.state->wait.reset();});
    bad_animation([](auto& b){b.runtime_entities[0].vm.suspended_pending.push_back(*b.runtime_entities[0].vm.pending);});
    auto animation_candidate=odh::SingleResidentSessionRestore::prepare(animation_bytes,f.bundle.resources,f.bundle.host,
        baseline,f.lot,f.bootstrap,21);
    odh::EntityThreadControllers animation_controllers(animation_candidate->world(),1);
    odh::Playback animation_playback(animation_candidate->world(),{});
    odh::Vm animation_vm(odh::RoutineSet{},animation_candidate->world().vm_memory(),{},animation_playback.primitive(),
        {{1,animation_main_program()}});
    require(animation_controllers.bind(animation_vm)&&animation_playback.bind(animation_vm),"bind fresh Main animation restore");
    odh::EntityVmScheduler animation_scheduler(4);
    require(animation_scheduler.add(1,animation_vm,[&](std::uint64_t now){return animation_controllers.advance(now);},
        [&]{return animation_controllers.observe();},[&]{return animation_controllers.notify();}),"register Main animation restore");
    animation_candidate->install_seated_continuation_owners(animation_vm,f.definition,*f.world.find(2)->entry_points,
        [](const auto&,auto id){return action(id);});
    auto animation_clip=std::make_shared<odh::AnimationHeader>();animation_clip->name="synthetic-seated-Main-idle";
    odh::BoneMotion animation_motion{};animation_motion.frames=3;
    animation_motion.time_properties={{{0,{{"xevt","100"}}}}};animation_clip->motions.push_back(animation_motion);
    odh::RandomState animation_random{999};std::uint64_t animation_tick=999;
    animation_candidate->restore_execution(animation_vm,animation_controllers,animation_playback,animation_scheduler,
        [&](const auto&){return animation_clip;},animation_random,animation_tick);
    require(animation_tick==2&&animation_vm.status()==odh::VmStatus::waiting&&animation_playback.progress(),
        "Main-owned seated animation pending wait restores before execution");
    std::cout<<"seated continuation: roundtrip, exact resource closure and "<<rejections<<" adversarial value cases passed\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
