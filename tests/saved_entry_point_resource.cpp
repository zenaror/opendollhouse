#include "saved_wall_appearance.hpp"
#include "saved_portal.hpp"

#include <chrono>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace {
unsigned checks=0;
void check(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
void put(std::string& bytes,std::size_t at,std::uint32_t value,unsigned count,bool big=false){
    for(unsigned i=0;i<count;++i)bytes.at(at+i)=static_cast<char>((value>>((big?count-1-i:i)*8))&255U);
}
std::string chunk(const char* type,std::uint16_t id,const std::string& payload){
    std::string bytes(76,'\0');bytes.replace(0,4,type);
    put(bytes,4,static_cast<std::uint32_t>(bytes.size()+payload.size()),4,true);put(bytes,8,id,2,true);
    return bytes+payload;
}
std::string iff(std::string payload){
    std::string bytes="IFF FILE 2.5:TYPE FOLLOWED BY SIZE JAMIE DOORNBOS & MAXIS 1";bytes.resize(64,'\0');
    return bytes+payload;
}
void write(const std::filesystem::path& path,const std::string& bytes){
    std::ofstream out(path,std::ios::binary);out.write(bytes.data(),static_cast<std::streamsize>(bytes.size()));
    if(!out)throw std::runtime_error("fixture write failed");
}
std::string objd(std::uint16_t uses_table){
    std::string bytes(162,'\0');put(bytes,0,138,4);put(bytes,28,0x12345678,4);put(bytes,44,uses_table,2);
    put(bytes,10,4097,2); // generated EP1 / Main
    put(bytes,42,4101,2); // generated EP5
    put(bytes,64,4126,2); // generated EP26
    return bytes;
}
std::string objf(std::uint16_t portal_condition=0){
    std::string bytes(16,'\0');put(bytes,4,1,4);bytes.replace(8,4,"fJBO");put(bytes,12,27,4);
    for(unsigned index=0;index<27;++index){
        const std::size_t at=bytes.size();bytes.resize(at+4,'\0');
        put(bytes,at,index==15?portal_condition:0,2);
        put(bytes,at+2,index==1?5001:index==5?5005:index==15?5015:index==26?5026:0,2);
    }
    return bytes;
}
std::string portal_bhav(std::uint16_t opcode=5016){
    std::string bytes(24,'\0');put(bytes,0,0x8002,2);put(bytes,2,1,2);put(bytes,5,4,1);
    put(bytes,12,opcode,2);put(bytes,14,254,1);put(bytes,15,255,1);
    return bytes;
}
}

int main(){try{
    const auto path=std::filesystem::temp_directory_path()/("odh-entry-point-resource-"+
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".iff");
    odh::DefinitionCandidate candidate{path.string(),"",7,{138,0x12345678,0,0,0,false,0,0,0}};
    write(path,iff(chunk("OBJD",7,objd(0))+chunk("BHAV",4101,std::string{})+chunk("BHAV",4126,std::string{})));
    const auto generated=saved_entry_point_resource(candidate);
    check(generated.functions.uses_function_table==0&&generated.table.source==odh::EntryPointSource::objd_legacy,
        "generated OBJD function table selected");
    check(generated.table.entries.size()==33&&generated.table.entries[1].action==4097&&
        generated.table.entries[5].action==4101&&generated.table.entries[26].action==4126,
        "generated Main, EP5 and EP26 identities decoded");
    check(generated.bhavs.count(4101)&&generated.bhavs.count(4126)&&!generated.bhavs.count(4097),
        "private BHAV presence inventory is scoped to exact asset");
    bool zero_portal=false;
    try{(void)saved_portal_action(candidate);}catch(const odh::FormatError&){zero_portal=true;}
    check(zero_portal,"zero Portal action is rejected instead of creating a call");

    write(path,iff(chunk("OBJD",7,objd(1))+chunk("OBJf",7,objf())+chunk("BHAV",5005,std::string{})+
                    chunk("BHAV",5015,portal_bhav())+chunk("BHAV",5016,portal_bhav(301))));
    const auto tabled=saved_entry_point_resource(candidate);
    check(tabled.functions.uses_function_table==1&&tabled.table.source==odh::EntryPointSource::objf&&
        tabled.table.entries.size()==27,"selected OBJf function table decoded");
    check(tabled.table.entries[1].action==5001&&tabled.table.entries[5].action==5005&&
        tabled.table.entries[15].action==5015&&tabled.table.entries[26].action==5026&&
        tabled.bhavs.count(5005)&&tabled.bhavs.count(5015),"OBJf entries and matching asset BHAV reported");
    const auto portal=saved_portal_function(candidate);
    check(portal.action==5015&&portal.action_bhav_in_asset&&portal.source==odh::EntryPointSource::objf&&
        portal.resource_source==candidate.source&&portal.resource_asset.empty()&&portal.chunk_id==candidate.chunk_id,
        "Portal entry preserves exact definition resource provenance separately from runtime entity IDs");
    check(portal.condition==0,"Portal EntryPoint condition is retained explicitly");
    const auto portal_action=saved_portal_action(candidate);
    check(portal_action.function.action==5015&&portal_action.routine.instructions.size()==1,
        "Portal EntryPoint resolves and parses its private BHAV from the selected definition asset");
    odh::World portal_world;
    check(portal_world.create(12,odh::EntityKind::avatar)&&portal_world.create(37,odh::EntityKind::object),
        "portal call caller and target entities created");
    check(portal_world.define_type(37,candidate.definition.guid)&&
        portal_world.define_entry_points(37,tabled.table.entries),"portal resource and EntryPoint table bound to live entity");
    odh::SavedWorldEntity portal_record{};portal_record.saved.object_id=37;portal_record.saved.guid=candidate.definition.guid;
    portal_record.definition=candidate.definition;portal_record.serialized_object_type=8;
    odh::DefinitionIndex portal_definitions({candidate});
    const auto call_plan=saved_portal_vm_call_plan(portal_record,portal_world,portal_definitions,12,37,0x80000037U);
    check(call_plan.call.key.scope==odh::RoutineScope::private_object&&call_plan.call.key.id==5015&&
        call_plan.call.context.caller==12&&
        call_plan.call.context.callee==37&&call_plan.call.context.stack_object==37&&
        call_plan.call.context.code_owner==0x80000037U,
        "portal call preserves avatar/portal context and independent resource CodeOwner");
    check(call_plan.call.arguments.size()==4&&std::all_of(call_plan.call.arguments.begin(),call_plan.call.arguments.end(),
        [](auto value){return value==0;}),"portal call supplies zeroed arguments sized by BHAV metadata");
    check(call_plan.owner_routines.size()==2&&call_plan.owner_routines.count(odh::routine_key(5016))&&
        call_plan.global_dependencies==std::vector<std::uint16_t>{301},
        "portal plan resolves exact private closure and reports shared-bank dependencies");
    auto bad_portal_record=portal_record;bad_portal_record.serialized_object_type=7;
    bool wrong_portal_type=false;try{(void)saved_portal_vm_call_plan(bad_portal_record,portal_world,portal_definitions,12,37,0x80000037U);}
    catch(const odh::FormatError&){wrong_portal_type=true;}
    check(wrong_portal_type,"non-portal saved World record cannot produce a portal VM call");
    odh::DefinitionIndex ambiguous_portal_definitions({candidate,candidate});
    bool ambiguous_portal=false;try{(void)saved_portal_vm_call_plan(portal_record,portal_world,ambiguous_portal_definitions,12,37,0x80000037U);}
    catch(const odh::FormatError&){ambiguous_portal=true;}
    check(ambiguous_portal,"ambiguous portal definition fails closed before allocating a VM call");
    bool zero_code_owner=false;try{(void)saved_portal_vm_call_plan(portal_record,portal_world,portal_definitions,12,37,0);}
    catch(const odh::FormatError&){zero_code_owner=true;}
    check(zero_code_owner,"portal call requires a host-allocated non-default resource CodeOwner");

    bool missing_objf=false;write(path,iff(chunk("OBJD",7,objd(1))));
    try{(void)saved_entry_point_resource(candidate);}catch(const odh::FormatError&){missing_objf=true;}
    check(missing_objf,"missing selected OBJf fails closed");
    bool duplicate_objd=false;write(path,iff(chunk("OBJD",7,objd(0))+chunk("OBJD",7,objd(0))));
    try{(void)saved_entry_point_resource(candidate);}catch(const odh::FormatError&){duplicate_objd=true;}
    check(duplicate_objd,"ambiguous selected OBJD fails closed");
    bool missing_portal_bhav=false;
    write(path,iff(chunk("OBJD",7,objd(1))+chunk("OBJf",7,objf())));
    try{(void)saved_portal_action(candidate);}catch(const odh::FormatError&){missing_portal_bhav=true;}
    check(missing_portal_bhav,"authored Portal action without same-asset private BHAV fails closed");
    write(path,iff(chunk("OBJD",7,objd(1))+chunk("OBJf",7,objf(5006))+chunk("BHAV",5005,std::string{})+
                    chunk("BHAV",5015,portal_bhav())+chunk("BHAV",5006,portal_bhav(2))));
    const auto condition_resource=saved_entry_point_resource(candidate);
    odh::World condition_world;
    check(condition_world.create(12,odh::EntityKind::avatar)&&condition_world.create(37,odh::EntityKind::object)&&
        condition_world.define_type(37,candidate.definition.guid)&&
        condition_world.define_entry_points(37,condition_resource.table.entries),"condition-bearing portal World initialized");
    auto condition_record=portal_record;
    bool portal_condition_unsupported=false;
    try{(void)saved_portal_vm_call_plan(condition_record,condition_world,odh::DefinitionIndex({candidate}),12,37,0x80000038U);}
    catch(const odh::FormatError&){portal_condition_unsupported=true;}
    check(portal_condition_unsupported,"portal condition is not silently skipped without an evaluator");
    write(path,iff(chunk("OBJD",7,objd(1))+chunk("OBJf",7,objf())+chunk("BHAV",5005,std::string{})+
                    chunk("BHAV",5015,portal_bhav(8192))));
    bool portal_semiglobal_unsupported=false;
    try{(void)saved_portal_vm_call_plan(portal_record,portal_world,portal_definitions,12,37,0x80000039U);}
    catch(const odh::FormatError&){portal_semiglobal_unsupported=true;}
    check(portal_semiglobal_unsupported,"semiglobal call dependency is rejected without its explicit bank");
    std::error_code ignored;std::filesystem::remove(path,ignored);
    std::cout<<"saved entry-point resource checks: "<<checks<<"\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
