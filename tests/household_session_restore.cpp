#include "odh/household_session_restore.hpp"
#include "odh/single_resident_session_restore.hpp"
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
std::vector<odh::SessionGlobalValue> globals(const odh::LotContext& lot,const odh::Ts1BootstrapGlobals& bootstrap){
    std::vector<odh::SessionGlobalValue> result;
    for(std::int16_t i=0;i<38;++i){auto v=i==9?std::optional<std::int16_t>{21}:bootstrap.read(i);if(!v)v=lot.read(i);
        if(v)result.push_back({static_cast<std::uint16_t>(i),*v});}return result;
}
struct Execution {
    odh::EntityThreadControllers controllers;
    odh::Playback playback;
    odh::Vm vm;
    Execution(odh::World& world,std::int16_t id,odh::RandomState& rng,bool missing=false):
        controllers(world,id,[&rng]{rng.next(100);}),playback(world,{}),
        vm(odh::RoutineSet{},world.vm_memory(),{},controllers.sleep_primitive(),
            missing?odh::RoutineOwners{}:odh::RoutineOwners{{static_cast<odh::CodeOwner>(id),program()}}){
        require(controllers.bind(vm)&&playback.bind(vm),"bind resident graph");}
    void schedule(odh::EntityVmScheduler& scheduler,std::int16_t id){require(scheduler.add(id,vm,
        [&](auto tick){return controllers.advance(tick);},[&]{return controllers.observe();},[&]{return controllers.notify();}),"schedule resident");}
    odh::SessionRuntimeEntity snapshot(std::int16_t id){odh::PlaybackSnapshot p;require(playback.capture_snapshot(p),"capture playback");
        return {id,vm.snapshot(),controllers.snapshot(),p};}
};
struct Fixture {
    odh::World world;
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
        b.host.global_values=globals(lot,bootstrap);b.household_roster=roster;b.world=world.snapshot();b.relationships=relationships.snapshot();
        b.runtime_entities={first->snapshot(1),second->snapshot(2)};b.scheduler=scheduler.snapshot();return b;}
};
auto prepare(const std::vector<std::uint8_t>& bytes,Fixture& f){return odh::HouseholdSessionRestore::prepare(bytes,f.resources,f.selection,
    f.roster,f.baseline,f.lot,f.bootstrap,21);}
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
void tests(){Fixture f;f.start();f.step();auto saved=f.snapshot();const auto bytes=odh::encode_session_bundle(saved);
    auto candidate=prepare(bytes,f);for(;f.tick<6;)f.step();resume(*candidate,f.snapshot());
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
