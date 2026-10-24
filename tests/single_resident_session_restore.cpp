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
    odh::World world;
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
void unit_tests() {
    Fixture source;source.start();source.step();const auto saved=source.snapshot();
    const auto bytes=odh::encode_session_bundle(saved);
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
    changed=saved;changed.world.entities[0].attributes.push_back(0);++changed.world.word_count;
    rejects([&]{prepare(odh::encode_session_bundle(changed),source);},"modified attribute layout accepted");
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
