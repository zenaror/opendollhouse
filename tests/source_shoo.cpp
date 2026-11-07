// SPDX-License-Identifier: MPL-2.0
#include "odh/source_shoo.hpp"
#include <iostream>
#include <stdexcept>
namespace {
unsigned checks=0;
void require(bool value,const char* text){++checks;if(!value)throw std::runtime_error(text);}
odh::Bhav routine(unsigned arguments=4){return {0x8002,{{2,254,255,{}}},{},odh::Bhav::Metadata{0,static_cast<std::uint8_t>(arguments),0,0}};}
odh::Ttab table(){odh::Interaction other{};other.action=4112;other.test=4108;other.flags=0x601;other.label_index=5;
    odh::Interaction shoo=other;shoo.action=4107;shoo.flags=0x421;shoo.label_index=3;
    odh::Interaction first=other;first.action=4096;first.label_index=2;
    return {odh::TtabStatus::decoded,3,9,true,2,{first,shoo,other}};}
void plans(){
    odh::VmProgram owner({{odh::routine_key(4107),routine(6)},{odh::routine_key(4108),routine()},
        {odh::routine_key(4112),routine()}}),globals({});auto t=table();
    odh::SourceShooPlanInput input{odh::SourceShooMode::direct_route,7,20,0x80000079U,&owner,&globals,&t};
    auto result=odh::plan_source_shoo(input);require(result.status==odh::SourceShooPlanStatus::ready&&result.plan,"direct source plan ready");
    const auto& direct=std::get<odh::SourceShooDirectPlan>(*result.plan).call;
    require(direct.key.id==4107&&direct.key.scope==odh::RoutineScope::private_object&&direct.exact_arguments&&
        direct.return_mode==odh::VmCallReturnMode::ordinary&&direct.arguments==std::vector<std::int16_t>{0,0,0,0,-1,-1}&&
        direct.context.caller==7&&direct.context.callee==20&&direct.context.stack_object==20&&direct.context.code_owner==input.owner,
        "direct plan preserves actual owner, actor and exact declared defaults");
    input.mode=odh::SourceShooMode::queued_interaction;result=odh::plan_source_shoo(input);
    require(result.status==odh::SourceShooPlanStatus::ready&&result.plan,"queued source plan ready");
    const auto& queued=std::get<odh::SourceShooQueuedPlan>(*result.plan);
    require(queued.action.id==4107&&queued.check&&queued.check->id==4108&&queued.flags==0x421&&
        queued.priority==50&&queued.interaction_number==3&&!queued.source_arguments&&queued.context.caller==7&&
        queued.context.callee==20&&queued.context.stack_object==20&&queued.context.code_owner==input.owner,
        "semantic TTAB key3 selects second row, retains null args, flags and check instead of ordinal third row key5");
    for(unsigned args:{0U,4U,255U}){odh::VmProgram bank({{odh::routine_key(4107),routine(args)}});
        input.mode=odh::SourceShooMode::direct_route;input.owner_program=&bank;result=odh::plan_source_shoo(input);
        const auto& call=std::get<odh::SourceShooDirectPlan>(*result.plan).call;
        require(call.arguments.size()==(args<4?4:args)&&call.arguments[3]==0&&
            (args<=4||call.arguments.back()==-1),"direct defaults have source max4 extent including bounded maximum");}
    odh::VmProgram legacy({{odh::routine_key(4107),odh::Bhav{0x8001,{}, {},{}}}});
    input.owner_program=&legacy;result=odh::plan_source_shoo(input);
    require(result.status==odh::SourceShooPlanStatus::ready&&std::get<odh::SourceShooDirectPlan>(*result.plan).call.arguments.size()==4,
        "legacy routine source defaults remain four zeros");
}
void rejection(){
    odh::VmProgram bank({{odh::routine_key(4107),routine()},{odh::routine_key(4108),routine()}}),empty({});auto t=table();
    odh::SourceShooPlanInput input{odh::SourceShooMode::queued_interaction,7,20,9,&bank,&empty,&t};
    const auto expect=[&](odh::SourceShooPlanStatus status,const char* text){const auto result=odh::plan_source_shoo(input);require(result.status==status&&!result.plan&&!result.diagnostic.empty(),text);};
    input.interactions=nullptr;expect(odh::SourceShooPlanStatus::unsupported,"unknown table is not known empty");input.interactions=&t;
    t.status=odh::TtabStatus::legacy_unsupported;expect(odh::SourceShooPlanStatus::unsupported,"unsupported table does not authorize an action");t=table();
    t.interactions[1].label_index=4;expect(odh::SourceShooPlanStatus::no_action,"known missing key3 differs from unsupported");t=table();
    t.interactions[2].label_index=3;expect(odh::SourceShooPlanStatus::unsupported,"duplicate semantic key is ambiguous");t=table();
    ++t.declared_count;expect(odh::SourceShooPlanStatus::unsupported,"declared table extent must agree");t=table();
    t.status=odh::TtabStatus::empty;expect(odh::SourceShooPlanStatus::unsupported,"noncanonical empty table cannot hide action rows");
    t={odh::TtabStatus::empty,0,{},false,0,{}};expect(odh::SourceShooPlanStatus::no_action,"known empty table is source no action");t=table();
    input.owner_program=nullptr;expect(odh::SourceShooPlanStatus::unsupported,"missing owner proof is unknown");input.owner_program=&empty;
    expect(odh::SourceShooPlanStatus::no_action,"action absent from complete owner bank is source no action");
    odh::VmProgram missing_check({{odh::routine_key(4107),routine()}});input.owner_program=&missing_check;
    expect(odh::SourceShooPlanStatus::no_action,"source discards nonzero missing check");
    t.interactions[1].test=0;require(odh::plan_source_shoo(input).status==odh::SourceShooPlanStatus::ready,"explicit zero check needs no routine");
    t=table();t.interactions[1].test=265;input.global_program=nullptr;expect(odh::SourceShooPlanStatus::unsupported,"unknown global check bank differs from absent check");
    input.global_program=&empty;expect(odh::SourceShooPlanStatus::no_action,"private bank cannot substitute a global check");
    odh::VmProgram global({{odh::routine_key(265),routine()}});input.global_program=&global;
    require(odh::plan_source_shoo(input).status==odh::SourceShooPlanStatus::ready,"global check resolves in exact global bank");
    auto malformed=routine();malformed.metadata.reset();odh::VmProgram bad({{odh::routine_key(4107),malformed}});
    input.mode=odh::SourceShooMode::direct_route;input.owner_program=&bad;expect(odh::SourceShooPlanStatus::unsupported,"malformed current metadata is not padded");
    input.owner_program=&empty;expect(odh::SourceShooPlanStatus::unsupported,"missing direct4107 cannot fabricate a successful child");
    input.owner_program=&bank;input.actor=20;expect(odh::SourceShooPlanStatus::unsupported,"actor cannot alias newly created Destination");
    input.actor=7;input.owner=0;expect(odh::SourceShooPlanStatus::unsupported,"owner zero cannot borrow a resource");input.owner=9;
    input.mode=static_cast<odh::SourceShooMode>(255);expect(odh::SourceShooPlanStatus::unsupported,"unknown dispatch mode is rejected");
}
}
int main(){try{plans();rejection();std::cout<<checks<<" checks passed\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
