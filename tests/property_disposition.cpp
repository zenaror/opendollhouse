#include "odh/property_disposition.hpp"
#include <iostream>
#include <stdexcept>

namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
odh::DefinitionCandidate definition(std::uint32_t guid,std::uint16_t type,std::optional<std::uint16_t> build) {
    odh::ObjectDefinition d{};d.version=138;d.guid=guid;d.object_type=type;d.build_mode_type=build;
    return {"objects.far","objects.iff",static_cast<std::uint16_t>(guid),d};
}
}
int main() {
    try {
        using D=odh::PropertyDisposition;using R=odh::PropertyDispositionReason;
        // A portal/build object, ordinary furniture, food, actor and retained
        // actor distinguish all predicate branches; permission bit8 is not bit16.
        const odh::DefinitionIndex index({definition(1,4,0),definition(2,8,3),definition(3,2,7),
            definition(4,34,0),definition(5,4,{})});
        for(const auto flags:{std::uint16_t{0},std::uint16_t{8},std::uint16_t{0x8008}}) {
            check(odh::inspect_property_disposition(index,1,4,flags).disposition==D::remove,"ordinary furniture predicate");
            check(odh::inspect_property_disposition(index,2,8,flags).disposition==D::retain,"build portal predicate");
            check(odh::inspect_property_disposition(index,3,2,flags).reason==R::actor,"actor checked before build category");
            check(odh::inspect_property_disposition(index,4,34,flags).disposition==D::remove,"non-normal buy definition is not accidentally kept");
        }
        for(const auto flags:{std::uint16_t{16},std::uint16_t{24},std::uint16_t{0xffff}})
            check(odh::inspect_property_disposition(index,3,2,flags).reason==R::retained_flag,"retention precedes actor removal");
        check(odh::inspect_property_disposition(index,5,4,0).reason==R::missing_build_mode,"absent build mode is not zero");
        check(odh::inspect_property_disposition(index,1,4,{}).reason==R::missing_flags,"missing flags fail closed");
        check(odh::inspect_property_disposition(index,1,2,0).reason==R::type_mismatch,"mismatched serialized type rejected");
        check(odh::inspect_property_disposition(index,99,4,0).reason==R::missing_definition,"absent definition unresolved");
        const odh::DefinitionIndex ambiguous({definition(1,4,0),definition(1,4,3)});
        check(odh::inspect_property_disposition(ambiguous,1,4,0).reason==R::ambiguous_definition,"ambiguous override is not selected");
        auto part=definition(10,4,0),master=definition(11,4,5);
        part.definition.master_id=7;part.definition.sub_index=0;part.definition.is_multitile=true;
        master.definition.master_id=7;master.definition.sub_index=-1;master.definition.is_multitile=true;
        auto inspect=[&](const std::vector<odh::DefinitionCandidate>& source){return odh::inspect_property_disposition(odh::DefinitionIndex(source),10,4,0);};
        auto result=inspect({part,master});
        check(result.disposition==D::retain&&result.effective_guid==11&&result.effective_build_mode==5,"master build category overrides buy member");
        part.definition.build_mode_type=9;master.definition.build_mode_type=0;
        check(inspect({part,master}).disposition==D::remove,"buy master overrides build member");
        check(inspect({part}).reason==R::missing_master,"missing master not replaced by member");
        master.asset="other.iff";
        check(inspect({part,master}).reason==R::missing_master,"master outside resource not used");
        master.asset=part.asset;auto duplicate=master;duplicate.definition.guid=12;
        check(inspect({part,master,duplicate}).reason==R::missing_master,"multiple masters reject");
        part.definition.is_multitile=false;
        check(inspect({part,master}).reason==R::invalid_definition,"inconsistent member cannot bypass master lookup");
        part.definition.is_multitile=true;master.definition.is_multitile=false;
        check(inspect({part,master}).reason==R::invalid_definition,"inconsistent master rejected");
        master.definition.is_multitile=true;master.definition.object_type=2;
        check(inspect({part,master}).reason==R::type_mismatch,"unproven differing master type is unresolved");
        part.definition.object_type=2;master.definition.object_type=4;
        const odh::DefinitionIndex actor_member({part,master});
        for(const auto flags:{std::uint16_t{0},std::uint16_t{16}})
            check(odh::inspect_property_disposition(actor_member,10,2,flags).reason==R::type_mismatch,
                "actor and retained branches cannot hide an unproven master type");
        std::cout<<"property disposition predicate, precedence and resource identity checks passed\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
