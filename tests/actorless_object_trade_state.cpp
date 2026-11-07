// SPDX-License-Identifier: MPL-2.0
#include "odh/actorless_object_trade.hpp"
#include "odh/seated_session_continuation.hpp"
#include <algorithm>
#include <iostream>
#include <memory>
#include <stdexcept>
namespace {
void need(bool value,const char* why){if(!value)throw std::runtime_error(why);}
template<class F>void rejects(F f){try{f();}catch(const std::exception&){return;}throw std::runtime_error("invalid actorless state accepted");}
odh::ActorlessObjectTradeState grouped_state(odh::ActorlessObjectTradeState state){
    using namespace odh;World world;need(world.restore_snapshot(state.world),"group fixture original World");
    EntityVmScheduler scheduler(4096);std::vector<std::unique_ptr<Vm>> threads;std::vector<std::unique_ptr<Sleep>> sleeps;
    const std::uint32_t master=0x8000;ObjectGroup physical{master,true,3,{}};
    SessionObjectValueGroup values;values.group_id=values.revision=1;values.multitile=true;values.initial_price=100;
    values.price_origin={SessionObjectPriceSource::purchase,master,"GameData/Objects.far","group.iff",128,{},{}};
    for(std::int16_t id:{2,3}){
        const auto guid=std::uint32_t(0x2000+id);const auto chunk=std::uint16_t(128+id);
        need(world.create_runtime_object(id,guid,{},std::vector<EntryPoint>(33),master),"materialize actual group part");
        const auto generation=world.find(id)->generation;SessionCreatedObject origin{id,generation,guid,"GameData/Objects.far","group.iff",chunk,master};
        state.created_objects.push_back(origin);physical.members.push_back({id,generation,std::int16_t(id==2?-16:0),0,0});
        SessionObjectValueMember member;member.object=origin;member.offset_x=id==2?-16:0;member.lead_object=id==3?1:0;
        member.wear_known=true;member.wear=std::uint16_t{0};member.disabled=std::uint8_t{0};values.members.push_back(member);
        auto vm=std::make_unique<Vm>(RoutineSet{},world.vm_memory(),VmLimits{},VmPrimitive{},RoutineOwners{{guid,{}}});
        auto sleep=std::make_unique<Sleep>(world);need(sleep->bind(*vm),"bind actual object-only Sleep");
        SleepSnapshot sleeping;sleeping.actor=SleepActorSnapshot{id,generation,vm->run_generation()};sleeping.clock=state.tick;sleeping.last_tick=state.tick;
        need(sleep->restore_snapshot(sleeping),"restore frozen object clock without inventing avatar authority");
        SessionResourceCodeOwner bank;bank.owner=guid;bank.resource_guid=guid;bank.source=origin.source;bank.asset=origin.asset;
        bank.definition_chunk_id=chunk;bank.root_routine=4096;bank.bank_kind=SessionCodeBankKind::complete_private;
        bank.identity=SessionCodeOwnerIdentity::resource_guid;bank.routine_fingerprint=session_loaded_bank_fingerprint({});
        SessionObjectThread thread;thread.id=id;thread.generation=generation;thread.guid=guid;thread.primary_owner=guid;
        thread.vm=vm->snapshot();thread.sleep=sleep->snapshot();thread.resource_owners.push_back(bank);state.object_threads.push_back(thread);
        need(scheduler.add(id,*vm,[](auto){return true;},{},[]{return true;}),"register actual object scheduler identity");threads.push_back(std::move(vm));sleeps.push_back(std::move(sleep));
    }
    values.base=ObjectTradeIdentity{3,world.find(3)->generation};state.trade.version=2;state.trade.revision=1;state.trade.groups={values};
    state.object_groups=ObjectGroupSnapshot{{physical}};state.world=world.snapshot();state.scheduler=scheduler.snapshot();return state;
}
}
int main(){try{
    odh::World world;need(world.create(1,odh::EntityKind::object)&&world.define_type(1,0x1234),"source world fixture");
    odh::ActorlessObjectTradeState state;state.resources.resource_fingerprint.fill(0x71);
    state.world=world.snapshot();state.relationships=odh::RelationshipState{}.snapshot();
    state.globals=odh::RuntimeGlobals::from_legacy({}).snapshot();state.globals.words[32]=1;
    state.scheduler=odh::EntityVmScheduler(4096).snapshot();state.history.complete=true;
    state.random_seed=0x123456789abcdef0ULL;
    const auto bytes=odh::encode_actorless_object_trade_state(state);
    const auto archive=odh::decode_session_archive(bytes);
    need(std::none_of(archive.sections.begin(),archive.sections.end(),[](const auto& s){return s.id==std::array<char,4>{'H','O','S','T'};}),"actorless archive never encodes fake household HOST");
    const auto reopened=odh::decode_actorless_object_trade_state(bytes,&state.resources);
    need(odh::encode_actorless_object_trade_state(reopened)==bytes&&reopened.random_seed==state.random_seed,
        "full actorless state and high RNG bits survive exact roundtrip");
    rejects([&]{(void)odh::decode_session_bundle(bytes);});
    auto expected=state.resources;expected.resource_fingerprint[0]^=1;
    rejects([&]{odh::decode_actorless_object_trade_state(bytes,&expected);});
    auto wrong=state;wrong.history.complete=false;rejects([&]{odh::encode_actorless_object_trade_state(wrong);});
    wrong=state;wrong.globals.words[32].reset();rejects([&]{odh::encode_actorless_object_trade_state(wrong);});
    auto scratch=odh::actorless_object_trade_components(state);scratch.host.family_budget=1000;
    rejects([&]{odh::actorless_object_trade_from_components(scratch);});
    scratch=odh::actorless_object_trade_components(state);scratch.host.runtime_avatar_id=1;scratch.host.resident_guid=0x1234;
    rejects([&]{odh::actorless_object_trade_from_components(scratch);});
    auto bad=archive;bad.sections.push_back(archive.sections.front());
    rejects([&]{odh::decode_actorless_object_trade_state(odh::encode_session_archive(bad));});
    bad=archive;bad.sections[0].id={'H','O','S','T'};
    rejects([&]{odh::decode_actorless_object_trade_state(odh::encode_session_archive(bad));});
    bad=archive;bad.sections[0].required=false;
    rejects([&]{odh::decode_actorless_object_trade_state(odh::encode_session_archive(bad));});
    scratch=odh::actorless_object_trade_components(state);
    scratch.house_layout_history=odh::new_house_layout_history(200);
    rejects([&]{odh::actorless_object_trade_from_components(scratch);});
    bad=archive;bad.sections.push_back({{'H','L','S','T'},true,{1,0}});
    rejects([&]{odh::decode_actorless_object_trade_state(odh::encode_session_archive(bad));});
    need(odh::encode_actorless_object_trade_state(odh::decode_actorless_object_trade_state(bytes))==bytes,
        "actorless format remains unchanged and cannot admit unbound household House history");
    auto grouped=grouped_state(state);const auto grouped_bytes=odh::encode_actorless_object_trade_state(grouped);
    const auto grouped_archive=odh::decode_session_archive(grouped_bytes);
    const auto group_section=std::find_if(grouped_archive.sections.begin(),grouped_archive.sections.end(),[](const auto& section){return section.id==std::array<char,4>{'O','G','R','P'};});
    need(group_section!=grouped_archive.sections.end()&&group_section->required,"object-only archive includes required physical group section");
    const auto group_reopened=odh::decode_actorless_object_trade_state(grouped_bytes,&grouped.resources);
    need(group_reopened.object_groups&&group_reopened.object_groups->groups.front().members.size()==2&&
        odh::encode_actorless_object_trade_state(group_reopened)==grouped_bytes,"physical/value group, threads, RNG and histories roundtrip together");
    need(std::none_of(grouped_archive.sections.begin(),grouped_archive.sections.end(),[](const auto& section){return section.id==std::array<char,4>{'H','O','S','T'};}),
        "multipart object-only archive still has no household HOST");
    const auto components=odh::actorless_object_trade_components(group_reopened);
    need(components.object_groups&&!components.host.family_budget&&!components.host.resident_guid&&!components.host.runtime_avatar_id&&components.runtime_entities.empty()&&
        odh::encode_actorless_object_trade_state(odh::actorless_object_trade_from_components(components))==grouped_bytes,
        "shared group carrier preserves all data without resident, family, budget or avatar runtime");
    {
        auto live=grouped;auto& physical=live.object_groups->groups.front();physical.kind=odh::ObjectGroupKind::live_association;physical.definition_guid=0;
        for(auto& body:physical.members){const auto origin=std::find_if(live.created_objects.begin(),live.created_objects.end(),[&](const auto& o){return o.id==body.id;});
            need(origin!=live.created_objects.end(),"live actorless member origin fixture");
            body.origin=odh::ObjectGroupMemberOrigin{body.id==2?odh::ObjectGroupMemberSource::original_import:odh::ObjectGroupMemberSource::native_created,
                origin->guid,origin->master_guid,origin->source,origin->asset,origin->definition_chunk_id};}
        // This fixture models an already authenticated original body and a
        // newly created body sharing one association; the codec is not source
        // authentication and never infers original status from numeric IDs.
        live.created_objects.erase(live.created_objects.begin());
        const auto live_bytes=odh::encode_actorless_object_trade_state(live);
        const auto copy=odh::decode_actorless_object_trade_state(live_bytes,&live.resources);
        need(copy.object_groups->groups.front().kind==odh::ObjectGroupKind::live_association&&
            copy.object_groups->groups.front().members[0].origin->source_kind==odh::ObjectGroupMemberSource::original_import&&
            odh::encode_actorless_object_trade_state(copy)==live_bytes,
            "object-only live association keeps mixed original/native identities without household authority");
        auto forged=live;forged.object_groups->groups.front().members[1].origin->source_kind=odh::ObjectGroupMemberSource::original_import;
        rejects([&]{odh::encode_actorless_object_trade_state(forged);});
        forged=live;forged.object_groups->groups.front().members[0].origin->definition_chunk_id++;
        rejects([&]{odh::encode_actorless_object_trade_state(forged);});
    }
    auto bad_group=grouped;bad_group.object_groups.reset();rejects([&]{odh::encode_actorless_object_trade_state(bad_group);});
    bad_group=grouped;bad_group.object_groups->groups.front().members.front().generation++;
    rejects([&]{odh::encode_actorless_object_trade_state(bad_group);});
    bad_group=grouped;bad_group.object_groups->groups.front().definition_guid++;
    rejects([&]{odh::encode_actorless_object_trade_state(bad_group);});
    bad_group=grouped;bad_group.object_groups->groups.front().members.front().offset_x++;
    rejects([&]{odh::encode_actorless_object_trade_state(bad_group);});
    bad_group=grouped;bad_group.created_objects.front().master_guid.reset();
    rejects([&]{odh::encode_actorless_object_trade_state(bad_group);});
    bad_group=grouped;bad_group.object_groups->groups.front().members.pop_back();bad_group.object_groups->groups.front().base_id=2;
    rejects([&]{odh::encode_actorless_object_trade_state(bad_group);});
    auto omitted=grouped_archive;omitted.sections.erase(std::remove_if(omitted.sections.begin(),omitted.sections.end(),[](const auto& section){return section.id==std::array<char,4>{'O','G','R','P'};}),omitted.sections.end());
    rejects([&]{odh::decode_actorless_object_trade_state(odh::encode_session_archive(omitted));});
    auto optional=grouped_archive;for(auto& section:optional.sections)if(section.id==std::array<char,4>{'O','G','R','P'})section.required=false;
    rejects([&]{odh::decode_actorless_object_trade_state(odh::encode_session_archive(optional));});
    auto group_limits=odh::actorless_object_trade_limits();group_limits.object_groups.members=1;
    rejects([&]{odh::encode_actorless_object_trade_state(grouped,group_limits);});
    rejects([&]{odh::decode_actorless_object_trade_state(grouped_bytes,nullptr,group_limits);});
    group_limits=odh::actorless_object_trade_limits();group_limits.archive.max_sections=grouped_archive.sections.size()-1;
    rejects([&]{odh::encode_actorless_object_trade_state(grouped,group_limits);});
    rejects([&]{odh::decode_actorless_object_trade_state(grouped_bytes,nullptr,group_limits);});
    need(odh::encode_actorless_object_trade_state(state)==bytes,"adding OGRP support leaves absent-group legacy bytes unchanged");
    std::cout<<"actorless object state: codec, authority and resource checks passed\n";
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
