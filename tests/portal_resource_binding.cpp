#include "../tools/portal_resource_binding.hpp"
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class F> void rejects(F&& operation,const char* message){
    try{operation();}catch(const odh::FormatError&){return;}
    throw std::runtime_error(message);
}
odh::RoutineSet bank(){
    return {{odh::routine_key(4105),odh::Bhav{0x8002,{{0,254,255,{}}},{},odh::Bhav::Metadata{0,4,4,0}}},
        {odh::routine_key(8200),odh::Bhav{0x8002,{{0,254,255,{}}},{},odh::Bhav::Metadata{0,4,4,0}}}};
}
struct Fixture {
    odh::DefinitionCandidate seed{},component{};
    odh::SessionResourceCodeOwner manifest;
    odh::SessionActionRoutineSource seed_bank{bank(),odh::SessionLinkedSemiglobal{"door","Global.far","door.iff"}};
    odh::SessionActionRoutineSource component_bank=seed_bank;
    odh::VmProgram process{bank()};
    odh::Vm resident{odh::RoutineSet{}, {}, {}, {}, {{17,bank()}}};
    Fixture(){
        seed.source="objects.far";seed.asset="door.iff";seed.chunk_id=12;
        seed.definition.guid=111;seed.definition.tree_table_id=100;
        component=seed;component.chunk_id=13;component.definition.guid=222;
        component.definition.tree_table_id=101;
        manifest.owner=17;manifest.resource_guid=seed.definition.guid;
        manifest.source=seed.source;manifest.asset=seed.asset;manifest.definition_chunk_id=seed.chunk_id;
        manifest.tree_table_id=seed.definition.tree_table_id;manifest.root_routine=4105;
        manifest.bank_kind=odh::SessionCodeBankKind::private_and_semiglobal;
        manifest.linked_semiglobal=seed_bank.linked_semiglobal;
        manifest.routine_fingerprint=odh::session_loaded_bank_fingerprint(seed_bank.routines);
    }
    void validate(std::uint16_t action=4105)const{
        odh_host::validate_portal_resource_binding(manifest,seed,component,4105,action,
            seed_bank,component_bank,process,resident);
    }
};
}
int main(){try{
    Fixture f;
    const auto manifest=f.manifest;
    f.validate();
    require(f.manifest.resource_guid==111&&f.component.definition.guid==222&&
        f.manifest.definition_chunk_id==12&&f.component.chunk_id==13,
        "two portal parts share an exact bank without rewriting immutable seed identity");
    f.component.source="other.far";rejects([&]{f.validate();},"different archive accepted");f.component=f.seed;
    f.component.asset="other.iff";rejects([&]{f.validate();},"different IFF accepted");f.component=f.seed;
    rejects([&]{f.validate(4106);},"different Portal15 action accepted");
    f.seed.definition.guid=333;rejects([&]{f.validate();},"wrong seed GUID accepted");f.seed.definition.guid=111;
    ++f.seed.chunk_id;rejects([&]{f.validate();},"wrong seed OBJD accepted");--f.seed.chunk_id;
    ++f.seed.definition.tree_table_id;rejects([&]{f.validate();},"wrong seed table accepted");--f.seed.definition.tree_table_id;
    for(int which=0;which<3;++which){
        auto& link=*f.component_bank.linked_semiglobal;
        if(which==0)link.name="other";else if(which==1)link.archive="other.far";else link.entry="other.iff";
        rejects([&]{f.validate();},"different GLOB provenance accepted");
        f.component_bank=f.seed_bank;
    }
    f.component_bank.routines.at(odh::routine_key(8200)).instructions[0].opcode=1;
    rejects([&]{f.validate();},"same IFF and action with different linked code accepted");f.component_bank=f.seed_bank;
    f.seed_bank.routines.at(odh::routine_key(4105)).instructions[0].opcode=1;
    rejects([&]{f.validate();},"changed independent seed bank accepted");f.seed_bank=f.component_bank;
    auto changed=bank();changed.at(odh::routine_key(4105)).instructions[0].opcode=1;
    odh::VmProgram wrong_process{changed};
    rejects([&]{odh_host::validate_portal_resource_binding(f.manifest,f.seed,f.component,4105,4105,
        f.seed_bank,f.component_bank,wrong_process,f.resident);},"changed process bank accepted");
    odh::Vm wrong_resident{odh::RoutineSet{}, {}, {}, {}, {{17,changed}}};
    rejects([&]{odh_host::validate_portal_resource_binding(f.manifest,f.seed,f.component,4105,4105,
        f.seed_bank,f.component_bank,f.process,wrong_resident);},"changed resident bank accepted");
    odh::Vm absent_resident{odh::RoutineSet{}};
    rejects([&]{odh_host::validate_portal_resource_binding(f.manifest,f.seed,f.component,4105,4105,
        f.seed_bank,f.component_bank,f.process,absent_resident);},"uninstalled resident owner accepted");
    f.component_bank.routines.erase(odh::routine_key(4105));
    rejects([&]{f.validate();},"missing Portal15 root accepted");f.component_bank=f.seed_bank;
    f.manifest.routine_fingerprint[0]^=1;
    rejects([&]{f.validate();},"changed manifest digest accepted");f.manifest=manifest;
    f.manifest.bank_kind=odh::SessionCodeBankKind::private_closure;
    rejects([&]{f.validate();},"closure falsely carrying linked bank accepted");
    f.manifest=manifest;f.validate();
    std::cout<<"portal_resource_binding: PASS\n";return 0;
}catch(const std::exception& error){std::cerr<<"portal_resource_binding: "<<error.what()<<'\n';return 1;}}
