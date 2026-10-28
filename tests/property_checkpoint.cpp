#include "odh/property_checkpoint.hpp"
#include "odh/sha256.hpp"
#include <iostream>
#include <stdexcept>

namespace {
void check(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
template<class F> void rejects(F call,const char* text) {
    try{call();}catch(const odh::FormatError&){return;}
    throw std::runtime_error(text);
}
odh::DefinitionCandidate definition(std::uint32_t guid,std::uint16_t type,std::uint16_t build,
    std::uint16_t master=0,std::int16_t sub=0) {
    odh::ObjectDefinition d{};d.version=138;d.guid=guid;d.object_type=type;d.build_mode_type=build;
    d.master_id=master;d.sub_index=sub;d.is_multitile=master!=0;
    return {"objects.far","objects.iff",static_cast<std::uint16_t>(guid),d};
}
odh::NeighborhoodFamilySource source() {
    odh::NeighborhoodFamilySource result;result.identity.fill(1);
    odh::Family family;family.family_number=900;family.budget=20000;family.member_guids={30};
    result.families.push_back({21,family});
    family.family_number=901;family.member_guids={31};result.families.push_back({22,family});return result;
}
odh::SessionBundle fixture() {
    odh::World world;
    std::vector<std::int16_t> person(88),motives(16);person[61]=21;person[31]=11;
    check(world.create_saved_entity(1,odh::EntityKind::avatar,30,{},person,motives,{}, {},0),"fixture resident");
    std::array<std::int16_t,68> words{};words[43]=16;words[41]=120;
    check(world.create_saved_entity(2,odh::EntityKind::object,10,{},{},{},words,{},0,12),"fixture retained root");
    words[43]=0;
    check(world.create_saved_entity(3,odh::EntityKind::object,11,{},{},{},words,{},0,12),"fixture qualifying secondary");
    words[43]=16;words[41]=456;
    check(world.create_saved_entity(4,odh::EntityKind::object,40,{7},{},{},words,{},0),"fixture survivor");
    odh::Vm vm(odh::RoutineSet{});odh::EntityThreadControllers controllers(world,1);
    check(controllers.bind(vm),"fixture VM bind");
    odh::EntityVmScheduler scheduler(4);
    check(scheduler.add(1,vm,[](std::uint64_t){return true;},[]{return true;},[]{return true;}),"fixture scheduler");
    odh::SessionBundle result;
    result.generic_execution=true;
    result.resources.resource_fingerprint.fill(0x5a);result.host.neighborhood_mode=1;
    result.host.house_number=7;result.host.family_chunk_id=21;result.host.family_number=900;
    result.host.resident_guid=30;result.host.resident_neighbor_id=11;result.host.runtime_avatar_id=1;
    result.host.family_budget=20000;result.world=world.snapshot();result.scheduler=scheduler.snapshot();
    odh::SessionRuntimeEntity runtime;runtime.id=1;runtime.vm=vm.snapshot();runtime.controllers=controllers.snapshot();
    result.runtime_entities.push_back(runtime);
    odh::SessionHouseholdMember member{1,world.find(1)->generation,30,11,128,"UserData/Characters/User00001.iff",{}};
    member.character_fingerprint.fill(0x31);result.household_roster.push_back(member);
    const auto baseline=source();
    result.family_lease=odh::SessionFamilyLease{baseline.identity,odh::family_overlay_identity(baseline.families[0]),1,21};
    return result;
}
}
int main(){try {
    using namespace odh;
    const auto baseline=source();auto bundle=fixture();
    const DefinitionIndex definitions({definition(30,2,0),definition(10,4,0,7,0),definition(11,4,0,7,1),
        definition(12,4,0,7,-1),definition(40,4,3)});
    const std::vector<SavedMultitileGroup> groups{{1,{}, {1}}, {2,12,{2,3}}, {4,{}, {4}}};
    const auto initial=plan_family_managed_move_in_overlay(baseline.families,baseline.identity,nullptr,21,7,0);
    const auto encode=[&](const SessionBundle& value){return encode_session_bundle(value);};
    const auto commit=[&](const FamilyOverlay& prior,const std::vector<std::uint8_t>& bytes,std::int32_t budget=20000){
        return plan_family_checkpoint_overlay(baseline.families,baseline.identity,&prior,21,7,1,budget,sha256(bytes.data(),bytes.size()));};
    auto bytes=encode(bundle);auto overlay=commit(initial,bytes);
    const auto prepare=[&](const FamilyOverlay& expected,const std::vector<std::uint8_t>& encoded){
        return prepare_property_checkpoint(baseline,expected,21,encoded,bundle.resources,definitions,groups,true);};
    auto result=prepare(overlay,bytes);
    check(result.removed_ids()==std::vector<std::int16_t>({1,2,3}),"per-part predicate removes complete group despite root retention");
    check(result.world().snapshot().entities.size()==1 && result.world().find(4)->object_words.at(41)==456 &&
        result.world().find(4)->attributes[0]==7,"current checkpoint survivor state preserved");
    check(result.budget()==20000 && result.house_number()==7 && result.checkpoint_sha256()==sha256(bytes.data(),bytes.size()) &&
        result.lease().family_identity==bundle.family_lease->family_identity,"current checkpoint pins retained");
    result.verify_overlay(overlay);
    const auto other=plan_family_managed_move_in_overlay(baseline.families,baseline.identity,&overlay,22,8,0);
    rejects([&]{result.verify_overlay(other);},"other family overlay change was ignored");
    auto newer=bundle;newer.world.entities.back().object_words[41]=789;
    const auto newer_bytes=encode(newer);const auto newer_overlay=commit(overlay,newer_bytes);
    rejects([&]{prepare(newer_overlay,bytes);},"older checkpoint accepted against new head");
    rejects([&]{result.verify_overlay(newer_overlay);},"new checkpoint not detected");
    const auto updated=prepare(newer_overlay,newer_bytes);
    check(updated.world().find(4)->object_words.at(41)==789,"latest live words used instead of stale saved-bank value456");
    auto corrupt=bytes;corrupt.back()^=1;
    rejects([&]{prepare(overlay,corrupt);},"corrupt checkpoint accepted");
    auto resources=bundle.resources;resources.resource_fingerprint[0]^=1;
    rejects([&]{prepare_property_checkpoint(baseline,overlay,21,bytes,resources,definitions,groups,true);},"wrong resources accepted");
    rejects([&]{prepare(initial,bytes);},"uncommitted archive accepted");
    const auto released=plan_family_managed_move_out_overlay(baseline.families,baseline.identity,&overlay,21);
    rejects([&]{prepare(released,bytes);},"released historical checkpoint accepted");
    const auto reentered=plan_family_managed_move_in_overlay(baseline.families,baseline.identity,&released,21,7,0);
    rejects([&]{prepare(reentered,bytes);},"prior occupancy checkpoint accepted after reentry");
    auto bad=bundle;bad.family_lease->occupancy_epoch=2;
    auto bad_bytes=encode(bad);auto bad_overlay=commit(initial,bad_bytes);
    rejects([&]{prepare(bad_overlay,bad_bytes);},"archive lease not checked after valid digest");
    bad=bundle;bad.host.family_budget=19999;bad_bytes=encode(bad);bad_overlay=commit(initial,bad_bytes);
    rejects([&]{prepare(bad_overlay,bad_bytes);},"archive budget not checked after valid digest");
    bad=bundle;bad.host.house_number=8;bad_bytes=encode(bad);bad_overlay=commit(initial,bad_bytes);
    rejects([&]{prepare(bad_overlay,bad_bytes);},"wrong house accepted after valid digest");
    bad=bundle;bad.host.family_number=901;bad_bytes=encode(bad);bad_overlay=commit(initial,bad_bytes);
    rejects([&]{prepare(bad_overlay,bad_bytes);},"wrong family number accepted after valid digest");
    bad=bundle;bad.host.resident_guid=31;bad.household_roster[0].guid=31;bad.world.entities[0].definition->guid=31;
    bad_bytes=encode(bad);bad_overlay=commit(initial,bad_bytes);
    rejects([&]{prepare(bad_overlay,bad_bytes);},"different authenticated roster accepted against family's checkpoint head");
    auto retained=bundle;retained.world.entities[2].object_words[43]=16;
    const auto retained_bytes=encode(retained);const auto retained_overlay=commit(initial,retained_bytes);
    const auto kept=prepare(retained_overlay,retained_bytes);
    check(kept.removed_ids()==std::vector<std::int16_t>({1}) && kept.world().find(2) && kept.world().find(3),
        "current retention flags replace stale source saved-bank flags");
    const DefinitionIndex wrong_policy({definition(30,2,0),definition(10,4,0,7,0),definition(11,4,0,7,1),
        definition(12,4,0,7,-1),definition(40,2,3)});
    rejects([&]{prepare_property_checkpoint(baseline,overlay,21,bytes,bundle.resources,wrong_policy,groups,true);},
        "nonresident OBJD actor silently authorized");
    SavedWorldLimits small;small.world.entities=1;
    rejects([&]{prepare_property_checkpoint(baseline,overlay,21,bytes,bundle.resources,definitions,groups,true,small);},
        "oversized current checkpoint hidden by eviction");
    check(encode(bundle)==bytes && encode_family_overlay(overlay)==encode_family_overlay(commit(initial,bytes)),
        "checkpoint and expected overlay untouched");
    std::cout<<"property checkpoint tests passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
