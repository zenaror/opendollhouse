#include "odh/session_entry.hpp"

#include <iostream>
#include <stdexcept>

namespace {
void require(bool value, const char* reason) {
    if (!value) throw std::runtime_error(reason);
}
template<class F> void rejects(F fn, const char* reason) {
    try { fn(); } catch (const odh::FormatError&) { return; }
    throw std::runtime_error(reason);
}
odh::NeighborEntry neighbor(std::int16_t id, std::uint32_t guid) {
    odh::NeighborRecord record{};
    record.version=4; record.name="Sim"; record.person_mode=1;
    record.person_data=std::vector<std::int16_t>(80);
    record.neighbor_id=id; record.guid=guid; record.unknown_neg_one=-1;
    return {1,record};
}
odh::DefinitionCandidate character(std::uint32_t guid, std::string path,
                                   std::uint16_t id=128, std::uint16_t type=2,
                                   std::uint32_t version=138) {
    odh::ObjectDefinition def{version,guid,type,0,0,false,32,0};
    return {"UserData/Characters",std::move(path),id,def};
}
odh::SessionEntry resolve(const odh::LotLocations& lots, const odh::FamilyContext& fam,
                          const odh::NeighborContext& nbr,
                          const odh::DefinitionIndex& chars, unsigned mode=1,
                          std::uint16_t house=7, std::string path="Houses/House07.iff") {
    return odh::resolve_session_entry(mode,house,std::move(path),lots,fam,nbr,chars);
}
}

int main() {
    try {
        constexpr std::uint32_t a=0x12345678, b=0x87654321;
        odh::Family family{}; family.house_number=7; family.family_number=900;
        family.member_guids={a,b};
        const odh::FamilyContext families({{201,family}});
        const odh::NeighborContext neighbors({0,62,
            {neighbor(11,a),neighbor(12,b)},0});
        const odh::LotLocations locations{{{7,10,20}},0};
        const odh::DefinitionIndex characters({
            character(a,"User00004.iff"),character(b,"User00009.iff")});
        const auto entry=resolve(locations,families,neighbors,characters);
        require(entry.neighborhood_mode==1&&entry.house_number==7&&
                entry.house_file=="Houses/House07.iff"&&entry.family_chunk_id==201&&
                entry.family_number==900,"selected house and FAMI identity retained");
        require(entry.members.size()==2&&entry.members[0].guid==a&&
                entry.members[0].neighbor_id==11&&entry.members[0].character_asset=="User00004.iff"&&
                entry.members[0].object_definition_id==128&&entry.members[1].guid==b&&
                entry.members[1].neighbor_id==12,"family order and Sim resource identities resolved");

        rejects([&]{(void)resolve(locations,families,neighbors,characters,6);},
                "unsupported neighborhood mode rejected");
        rejects([&]{(void)resolve(locations,families,neighbors,characters,1,8);},
                "house missing from selected map rejected");
        rejects([&]{(void)resolve(locations,families,neighbors,characters,1,99);},
                "house sentinel rejected");
        rejects([&]{(void)resolve(locations,families,neighbors,characters,1,7,"");},
                "missing house path rejected");

        odh::FamilyContext no_family({});
        rejects([&]{(void)resolve(locations,no_family,neighbors,characters);},
                "house without resident family rejected");
        odh::Family empty=family; empty.member_guids.clear();
        rejects([&]{(void)resolve(locations,odh::FamilyContext({{201,empty}}),neighbors,characters);},
                "empty resident household rejected for household entry");

        odh::Family missing=family; missing.member_guids={0xabcdef01};
        odh::FamilyContext missing_fami({{201,missing}});
        rejects([&]{(void)resolve(locations,missing_fami,neighbors,characters);},
                "family member without active neighbor rejected");
        const odh::DefinitionIndex missing_character({character(a,"User00004.iff")});
        rejects([&]{(void)resolve(locations,families,neighbors,missing_character);},
                "every family member requires its own character resource");

        const odh::DefinitionIndex wrong_type({character(a,"object.iff",128,1)});
        rejects([&]{(void)resolve(locations,families,neighbors,wrong_type);},
                "non-person OBJD cannot stand in for a Sim");
        const odh::DefinitionIndex ambiguous({character(a,"first.iff"),
                                               character(a,"second.iff"),
                                               character(b,"User00009.iff")});
        rejects([&]{(void)resolve(locations,families,neighbors,ambiguous);},
                "duplicate character GUID rejected rather than guessed");

        odh::Family duplicate=family; duplicate.member_guids={a,a};
        rejects([&]{(void)resolve(locations,odh::FamilyContext({{201,duplicate}}),neighbors,characters);},
                "duplicate family member rejected");

        std::cout<<"PASS session entry preserves house, family and ordered Sim resource identities\n";
        std::cout<<"PASS unsupported, missing, empty, mismatched and ambiguous input rejected\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr<<error.what()<<'\n'; return 1;
    }
}
