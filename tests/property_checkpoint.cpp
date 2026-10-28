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
    result.object_history=odh::SessionObjectHistory{true,{}, {100}};
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
    PropertyCheckpointBaseline property_base{bundle.world,groups,{100},{}};property_base.installed_house_sha256.fill(0x61);
    const auto initial=plan_family_managed_move_in_overlay(baseline.families,baseline.identity,nullptr,21,7,0);
    const auto encode=[&](const SessionBundle& value){return encode_session_bundle(value);};
    const auto commit=[&](const FamilyOverlay& prior,const std::vector<std::uint8_t>& bytes,std::int32_t budget=20000){
        return plan_family_checkpoint_overlay(baseline.families,baseline.identity,&prior,21,7,1,budget,sha256(bytes.data(),bytes.size()));};
    auto bytes=encode(bundle);auto overlay=commit(initial,bytes);
    const auto prepare=[&](const FamilyOverlay& expected,const std::vector<std::uint8_t>& encoded){
        return prepare_property_checkpoint(baseline,expected,21,encoded,bundle.resources,definitions,property_base);};
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
    rejects([&]{prepare_property_checkpoint(baseline,overlay,21,bytes,resources,definitions,property_base);},"wrong resources accepted");
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
    rejects([&]{prepare_property_checkpoint(baseline,overlay,21,bytes,bundle.resources,wrong_policy,property_base);},
        "nonresident OBJD actor silently authorized");
    SavedWorldLimits small;small.world.entities=1;
    rejects([&]{prepare_property_checkpoint(baseline,overlay,21,bytes,bundle.resources,definitions,property_base,small);},
        "oversized current checkpoint hidden by eviction");
    check(result.removed_original_objects().size()==2 && result.removed_original_objects()[0].id==2 &&
        result.removed_original_objects()[1].id==3 && result.retired_entities().size()==3,
        "avatar retirement is separate from original object removal provenance");
    check(result.world().reserved_entity_ids()==std::vector<std::int16_t>({1,2,3,100}) &&
        result.installed_house_sha256()==property_base.installed_house_sha256,"baseline reserves and house identity pinned");
    auto bad_base=property_base;bad_base.groups.pop_back();
    rejects([&]{prepare_property_checkpoint(baseline,overlay,21,bytes,bundle.resources,definitions,bad_base);},
        "incomplete baseline group inventory accepted");
    bad_base=property_base;bad_base.installed_house_sha256.fill(0);
    rejects([&]{prepare_property_checkpoint(baseline,overlay,21,bytes,bundle.resources,definitions,bad_base);},
        "missing installed house identity accepted");
    bad_base=property_base;bad_base.reserved_ids={100,100};
    rejects([&]{prepare_property_checkpoint(baseline,overlay,21,bytes,bundle.resources,definitions,bad_base);},
        "duplicate baseline reservations accepted");
    auto disappeared=bundle;
    World changed;check(changed.restore_snapshot(bundle.world) && changed.remove(4),"fixture prior removal");
    disappeared.world=changed.snapshot();
    auto gone_bytes=encode(disappeared);auto gone_overlay=commit(initial,gone_bytes);
    rejects([&]{prepare(gone_overlay,gone_bytes);},"unrecorded original removal accepted");
    disappeared.removed_original_objects={{4,bundle.world.entities[3].generation,40}};
    disappeared.object_history->reserved_entity_ids={4,100};
    gone_bytes=encode(disappeared);gone_overlay=commit(initial,gone_bytes);
    const auto gone=prepare(gone_overlay,gone_bytes);
    check(gone.world().snapshot().entities.empty() && gone.removed_original_objects().size()==3 &&
        gone.world().reserved_entity_ids()==std::vector<std::int16_t>({1,2,3,4,100}),
        "prior object removal combined with eviction and reserved");
    auto forged=disappeared;forged.removed_original_objects[0].guid=999;
    auto forged_bytes=encode(forged);auto forged_overlay=commit(initial,forged_bytes);
    rejects([&]{prepare(forged_overlay,forged_bytes);},"forged prior-removal identity accepted");
    World split;check(split.restore_snapshot(bundle.world) && split.remove(3),"fixture split group");
    auto partial=bundle;partial.world=split.snapshot();partial.removed_original_objects={{3,bundle.world.entities[2].generation,11}};
    partial.object_history->reserved_entity_ids={3,100};
    const auto partial_bytes=encode(partial);const auto partial_overlay=commit(initial,partial_bytes);
    rejects([&]{prepare(partial_overlay,partial_bytes);},"checkpoint split a baseline group");
    auto altered=bundle;altered.world.entities[3].saved_object_banks->object_data[12]=99;
    auto altered_bytes=encode(altered);auto altered_overlay=commit(initial,altered_bytes);
    rejects([&]{prepare(altered_overlay,altered_bytes);},"immutable original banks changed");
    altered=bundle;altered.world.entities[3].unattached_slot=99;
    altered_bytes=encode(altered);altered_overlay=commit(initial,altered_bytes);
    rejects([&]{prepare(altered_overlay,altered_bytes);},"unattached slot baseline changed");
    World creation;check(creation.restore_snapshot(bundle.world) && creation.create_runtime_object(5,40,{},{}),"fixture runtime creation");
    check(creation.declare_object_word(5,43,16) && creation.declare_object_word(5,41,888),"fixture created durable words");
    auto added=bundle;added.world=creation.snapshot();
    const SessionCreatedObject origin{5,creation.find(5)->generation,40,"objects.far","objects.iff",40};
    added.created_objects={origin};const auto added_bytes=encode(added);const auto added_overlay=commit(initial,added_bytes);
    const auto resolver=[](const SessionCreatedObject& value){
        World metadata;check(metadata.create_runtime_object(value.id,value.guid,{},{}),"fixture resource reconstruction");
        auto entity=*metadata.find(value.id);entity.generation=value.generation;return entity;};
    rejects([&]{prepare(added_overlay,added_bytes);},"created origin accepted without resource reconstruction");
    const auto added_result=prepare_property_checkpoint(baseline,added_overlay,21,added_bytes,bundle.resources,
        definitions,property_base,{}, {},resolver);
    check(added_result.created_objects().size()==1 && added_result.created_objects()[0].id==5 &&
        added_result.groups().size()==2 && added_result.world().find(5)->object_words.at(41)==888,
        "created singleton resource origin and current state survive");
    const auto wrong_constructor=[&](const SessionCreatedObject& value){auto entity=resolver(value);entity.slots.push_back(0);return entity;};
    rejects([&]{prepare_property_checkpoint(baseline,added_overlay,21,added_bytes,bundle.resources,definitions,
        property_base,{}, {},wrong_constructor);},"created resource layout mismatch accepted");
    auto no_origin=added;no_origin.created_objects.clear();
    auto no_origin_bytes=encode(no_origin);auto no_origin_overlay=commit(initial,no_origin_bytes);
    rejects([&]{prepare(no_origin_overlay,no_origin_bytes);},"new object without creation provenance accepted");
    auto wrong_origin=added;wrong_origin.created_objects[0].asset="other.iff";
    const auto wrong_origin_bytes=encode(wrong_origin);const auto wrong_origin_overlay=commit(initial,wrong_origin_bytes);
    rejects([&]{prepare_property_checkpoint(baseline,wrong_origin_overlay,21,wrong_origin_bytes,bundle.resources,
        definitions,property_base,{}, {},resolver);},"wrong created resource source accepted");
    World reuse;check(reuse.restore_snapshot(bundle.world) && reuse.remove(4) && reuse.create_runtime_object(4,40,{},{}),"fixture reused baseline ID");
    check(reuse.declare_object_word(4,43,16),"fixture reused flags");
    auto reused=bundle;reused.world=reuse.snapshot();reused.created_objects={{4,reuse.find(4)->generation,40,"objects.far","objects.iff",40}};
    reused.removed_original_objects={{4,bundle.world.entities[3].generation,40}};
    reused.object_history.reset(); // Old GEXP allowed numeric reuse, OHST does not.
    const auto reused_bytes=encode(reused);const auto reused_overlay=commit(initial,reused_bytes);
    rejects([&]{prepare_property_checkpoint(baseline,reused_overlay,21,reused_bytes,bundle.resources,
        definitions,property_base,{}, {},resolver);},"reused numeric baseline ID silently lost old-reference protection");
    auto sell_created=added;sell_created.world.entities.back().object_words[43]=0;
    const auto sell_bytes=encode(sell_created);const auto sell_overlay=commit(initial,sell_bytes);
    const DefinitionIndex buy_created({definition(30,2,0),definition(10,4,0,7,0),definition(11,4,0,7,1),
        definition(12,4,0,7,-1),definition(40,4,0)});
    const auto sold=prepare_property_checkpoint(baseline,sell_overlay,21,sell_bytes,bundle.resources,
        buy_created,property_base,{}, {},resolver);
    check(sold.created_objects().empty() && sold.retired_created_objects().size()==1 &&
        sold.retired_created_objects()[0].id==5 && sold.retired_created_objects()[0].asset=="objects.iff" &&
        sold.retired_entities().size()==4 && !sold.world().entity_id_available(5),
        "newly removed creation retains its exact origin and reserves numeric ID");
    World removed_group;check(removed_group.restore_snapshot(bundle.world) && removed_group.remove(2) && removed_group.remove(3),
        "fixture entire group already removed");
    auto already_removed=bundle;already_removed.world=removed_group.snapshot();
    already_removed.removed_original_objects={{2,bundle.world.entities[1].generation,10},{3,bundle.world.entities[2].generation,11}};
    already_removed.object_history->reserved_entity_ids={2,3,100};
    const auto group_bytes=encode(already_removed);const auto group_overlay=commit(initial,group_bytes);
    const DefinitionIndex missing_old_definition({definition(30,2,0),definition(11,4,0,7,1),definition(12,4,0,7,-1),definition(40,4,3)});
    rejects([&]{prepare_property_checkpoint(baseline,group_overlay,21,group_bytes,bundle.resources,
        missing_old_definition,property_base);},"fully removed baseline group hid missing definition");
    auto legacy=bundle;legacy.object_history.reset();
    const auto legacy_bytes=encode(legacy);const auto legacy_overlay=commit(initial,legacy_bytes);
    rejects([&]{prepare(legacy_overlay,legacy_bytes);},"legacy missing lifetime history silently promoted to complete");
    legacy.object_history=SessionObjectHistory{false,{}, {100}};
    const auto incomplete_bytes=encode(legacy);const auto incomplete_overlay=commit(initial,incomplete_bytes);
    rejects([&]{prepare(incomplete_overlay,incomplete_bytes);},"explicit incomplete history accepted for neutral property");
    auto history=bundle;history.world.generation_sequence+=1;
    history.object_history->retired_created_objects={{5,history.world.generation_sequence,40,"objects.far","objects.iff",40}};
    history.object_history->reserved_entity_ids={5,100};
    const auto history_bytes=encode(history);const auto history_overlay=commit(initial,history_bytes);
    const auto historical=prepare(history_overlay,history_bytes);
    check(historical.retired_created_objects().size()==1 && historical.retired_created_objects()[0].id==5 &&
        !historical.world().entity_id_available(5),"created-then-deleted history preserved across checkpoint preparation");
    check(encode(bundle)==bytes && encode_family_overlay(overlay)==encode_family_overlay(commit(initial,bytes)),
        "checkpoint and expected overlay untouched");
    std::cout<<"property checkpoint tests passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
