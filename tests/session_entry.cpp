#include "odh/session_entry.hpp"

#include <iostream>
#include <optional>
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
void add_entity(odh::World& world, std::int16_t id, odh::EntityKind kind,
               std::uint32_t guid, std::optional<odh::EntityPosition> position = {}) {
    require(world.create(id,kind), "synthetic entity created");
    require(world.define_type(id,guid), "synthetic entity GUID defined");
    if (position) require(world.set_position(id,*position), "synthetic position set");
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

        constexpr std::uint32_t visitor_guid=0x11112222, object_only_guid=0x33334444;
        odh::World restored;
        const odh::EntityPosition saved_position{80,96,1,3};
        add_entity(restored,21,odh::EntityKind::avatar,a,saved_position);
        add_entity(restored,22,odh::EntityKind::avatar,b);
        add_entity(restored,23,odh::EntityKind::object,object_only_guid);
        add_entity(restored,24,odh::EntityKind::object,visitor_guid);
        add_entity(restored,25,odh::EntityKind::avatar,visitor_guid,
                   odh::EntityPosition{40,48,1,0});
        odh::SessionEntry household{1,7,"House07.iff",201,900,
            {{a,11,"characters","a.iff",128},
             {b,12,"characters","b.iff",128},
             {object_only_guid,13,"characters","c.iff",128}}};
        const auto reconciled=odh::reconcile_session_household(household,restored);
        require(reconciled.size()==3&&
                reconciled[0].presence==odh::HouseholdMemberPresence::present_with_position&&
                reconciled[0].object_id==21&&reconciled[0].position==saved_position,
                "saved household avatar reuses its restored ID and position");
        require(reconciled[1].presence==odh::HouseholdMemberPresence::present_without_position&&
                reconciled[1].object_id==22&&!reconciled[1].position,
                "present avatar without position is distinct from absence");
        require(reconciled[2].presence==odh::HouseholdMemberPresence::absent&&
                !reconciled[2].object_id&&!reconciled[2].position,
                "ordinary object GUID does not satisfy household avatar identity");
        require(restored.find(25)&&reconciled.size()==household.members.size(),
                "visitor remains in World and outside household result");
        const auto repeated=odh::reconcile_session_household(household,restored);
        require(repeated.size()==reconciled.size(),"reconciliation repeat has same member count");
        for (std::size_t i=0;i<reconciled.size();++i)
            require(repeated[i].guid==reconciled[i].guid&&
                    repeated[i].presence==reconciled[i].presence&&
                    repeated[i].object_id==reconciled[i].object_id&&
                    repeated[i].position==reconciled[i].position,
                    "reconciliation result is repeatable and idempotent");

        odh::World duplicate_avatar_world;
        add_entity(duplicate_avatar_world,31,odh::EntityKind::avatar,a);
        add_entity(duplicate_avatar_world,32,odh::EntityKind::avatar,a);
        rejects([&]{(void)odh::reconcile_session_household(household,duplicate_avatar_world);},
                "duplicate avatar GUID rejected");
        odh::World unidentified_avatar_world;
        require(unidentified_avatar_world.create(33,odh::EntityKind::avatar),
                "unidentified avatar fixture created");
        rejects([&]{(void)odh::reconcile_session_household(household,unidentified_avatar_world);},
                "avatar without exact identity cannot be mistaken for missing family member");
        odh::SessionEntry duplicate_household=household;
        duplicate_household.members.push_back(duplicate_household.members.front());
        rejects([&]{(void)odh::reconcile_session_household(duplicate_household,restored);},
                "duplicate household GUID rejected");

        std::cout<<"PASS session entry preserves house, family and ordered Sim resource identities\n";
        std::cout<<"PASS unsupported, missing, empty, mismatched and ambiguous input rejected\n";
        std::cout<<"PASS restored household reconciliation reuses avatars, distinguishes absence and position, and ignores visitors/objects\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr<<error.what()<<'\n'; return 1;
    }
}
