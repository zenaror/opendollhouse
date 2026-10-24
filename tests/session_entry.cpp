#include "odh/session_entry.hpp"
#include "odh/family_overlay.hpp"
#include "odh/sha256.hpp"
#ifdef __linux__
#include "linux_family_overlay_store.hpp"
#endif

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <stdexcept>
#ifdef __linux__
#include <unistd.h>
#endif

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
void le16(std::vector<std::uint8_t>& out, std::uint16_t value) {
    out.push_back(static_cast<std::uint8_t>(value));
    out.push_back(static_cast<std::uint8_t>(value >> 8));
}
void le32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    for (unsigned shift=0;shift<32;shift+=8)
        out.push_back(static_cast<std::uint8_t>(value >> shift));
}
void be16(std::vector<std::uint8_t>& out, std::uint16_t value) {
    out.push_back(static_cast<std::uint8_t>(value >> 8));
    out.push_back(static_cast<std::uint8_t>(value));
}
void be32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    for (int shift=24;shift>=0;shift-=8)
        out.push_back(static_cast<std::uint8_t>(value >> shift));
}
std::vector<std::uint8_t> iff(const std::vector<std::vector<std::uint8_t>>& chunks) {
    std::vector<std::uint8_t> out(64,0);
    constexpr char signature[]="IFF FILE 2.5:TYPE FOLLOWED BY SIZE JAMIE DOORNBOS & MAXIS 1";
    std::copy(signature,signature+sizeof(signature)-1,out.begin());
    for (const auto& chunk:chunks) out.insert(out.end(),chunk.begin(),chunk.end());
    return out;
}
std::vector<std::uint8_t> iff_chunk(const char* type, std::uint16_t id,
                                   const std::vector<std::uint8_t>& payload) {
    std::vector<std::uint8_t> out(type,type+4);
    be32(out,static_cast<std::uint32_t>(76+payload.size()));
    be16(out,id);be16(out,0);out.resize(out.size()+64,0);
    out.insert(out.end(),payload.begin(),payload.end());
    return out;
}
void write_bytes(const std::filesystem::path& path,const std::vector<std::uint8_t>& bytes) {
    std::ofstream stream(path,std::ios::binary);
    if (!stream) throw std::runtime_error("cannot create synthetic session IFF");
    stream.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    if (!stream) throw std::runtime_error("cannot write synthetic session IFF");
}
void test_created_family_disk_reload() {
    const auto root=std::filesystem::temp_directory_path()/(
        "odh-created-family-session-"+
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::error_code create_error;
    if (!std::filesystem::create_directory(root,create_error) || create_error)
        throw std::runtime_error("cannot create exclusive synthetic session directory");
    struct Cleanup { std::filesystem::path path; ~Cleanup() {
        std::error_code error;std::filesystem::remove_all(path,error);
    }} cleanup{root};
    if (!std::filesystem::create_directory(root/"Houses",create_error) || create_error ||
        !std::filesystem::create_directory(root/"Characters",create_error) || create_error)
        throw std::runtime_error("cannot create synthetic session subdirectories");

    std::vector<std::uint8_t> locations;
    le16(locations,0xfffd);le16(locations,1);locations.push_back(1);
    const std::string lot="7,10,20";
    locations.insert(locations.end(),lot.begin(),lot.end());
    locations.push_back(0);locations.push_back(0);
    std::vector<std::uint8_t> fami;
    le32(fami,0);le32(fami,9);le32(fami,0x46414d49);
    le32(fami,0);le32(fami,900);le32(fami,20000);le32(fami,0);
    le32(fami,0);le32(fami,0);le32(fami,0);
    odh::Nbrs source_neighbors{0,62,{},0};
    const auto neighborhood=iff({iff_chunk("FAMI",201,fami),
        iff_chunk("NBRS",1,odh::encode_nbrs(source_neighbors))});
    write_bytes(root/"LotLocations.iff",iff({iff_chunk("STR#",1,locations)}));
    const auto neighborhood_path=root/"Neighborhood.iff";
    write_bytes(neighborhood_path,neighborhood);
    std::vector<std::uint8_t> simi(12+32*2+34+6*4,0);
    simi[4]=0x33;simi[8]='I';simi[9]='M';simi[10]='I';simi[11]='S';
    write_bytes(root/"Houses/House07.iff",iff({iff_chunk("SIMI",1,simi)}));

    constexpr std::uint32_t created_guid=0x0badcafe;
    odh::Family vacant{};vacant.family_number=900;vacant.budget=20000;
    const std::vector<odh::FamilyEntry> source_families{{201,vacant}};
    odh::Family created{};created.version=9;created.unknown=24;
    created.family_number=901;created.budget=20000;created.member_guids={created_guid};
    odh::NeighborRecord member{};member.version=10;member.unknown3=9;
    member.name="iffname";member.person_mode=9;
    member.person_data=std::vector<std::int16_t>(256,0);
    (*member.person_data)[61]=202;member.neighbor_id=1;member.guid=created_guid;
    member.unknown_neg_one=-1;
    const auto identity=odh::sha256(neighborhood.data(),neighborhood.size());
    odh::FamilyOverlayCreation creation{202,"Created family",
        odh::encode_simitone_create_family_fami(created),{member}};
    auto overlay=odh::plan_family_creation_overlay(source_families,source_neighbors,
        identity,nullptr,creation);
    overlay=odh::plan_family_move_in_overlay(source_families,identity,&overlay,
        202,7,5000);
#ifdef __linux__
    struct ConfigHome {
        std::optional<std::string> previous;
        explicit ConfigHome(const std::filesystem::path& path) {
            if (const auto* value=std::getenv("XDG_CONFIG_HOME")) previous=value;
            if (::setenv("XDG_CONFIG_HOME",path.c_str(),1)!=0)
                throw std::runtime_error("cannot set temporary XDG_CONFIG_HOME");
        }
        ~ConfigHome() {
            if (previous) (void)::setenv("XDG_CONFIG_HOME",previous->c_str(),1);
            else (void)::unsetenv("XDG_CONFIG_HOME");
        }
    } config_home(root/"config");
    const auto overlay_path=odh_host::linux_family_overlay_path(identity);
    odh_host::write_linux_family_overlay_atomic(overlay_path,overlay);
    const auto reopened=odh_host::read_linux_family_overlay_for_neighborhood(neighborhood_path);
    require(reopened.has_value(),"created-family overlay is present after store reopen");
    overlay=*reopened;
#else
    overlay=odh::decode_family_overlay(odh::encode_family_overlay(overlay));
#endif

    std::vector<std::uint8_t> objd(32,0);
    objd[0]=138;objd[18]=2;
    for (unsigned i=0;i<4;++i)objd[28+i]=static_cast<std::uint8_t>(created_guid>>(8*i));
    write_bytes(root/"Characters/PlayerCreated.iff",iff({iff_chunk("OBJD",128,objd)}));

    const auto loaded=odh::load_session_entry(root,1,7,1,&overlay);
    require(loaded.family_chunk_id==202&&loaded.family_number==901&&
            loaded.members.size()==1&&loaded.members[0].guid==created_guid&&
            loaded.members[0].neighbor_id==1&&
            loaded.members[0].character_asset=="PlayerCreated.iff",
            "store reload replays saved created FAMI and NBRS before resolving its character IFF");
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

        odh::Family vacant_family{};
        vacant_family.house_number=0; vacant_family.family_number=900;
        vacant_family.budget=20000; vacant_family.member_guids={a,b};
        const odh::FamilyEntry vacant_entry{201,vacant_family};
        std::array<std::uint8_t,32> neighborhood_identity{};
        neighborhood_identity[0]=0x42;
        const odh::FamilyOverlay move_in{neighborhood_identity,
            {odh::plan_family_move_in(vacant_entry,7,5500)}};
        const auto effective_entries=odh::apply_family_overlay(
            {vacant_entry},neighborhood_identity,move_in);
        const odh::FamilyContext effective_families(effective_entries);
        const auto moved_entry=resolve(locations,effective_families,neighbors,characters);
        require(effective_families.by_house(7)!=nullptr && moved_entry.family_chunk_id==201 &&
                moved_entry.members.size()==2 && moved_entry.members[0].guid==a,
                "one effective family overlay makes the moved household visible to lot entry");

        constexpr std::uint32_t created_guid=0x0badcafe;
        odh::Family created_family{};
        created_family.version=9; created_family.unknown=24;
        created_family.family_number=901; created_family.budget=20000;
        created_family.member_guids={created_guid};
        odh::NeighborRecord created_neighbor{};
        created_neighbor.version=10; created_neighbor.unknown3=9;
        created_neighbor.name="iffname"; created_neighbor.person_mode=9;
        created_neighbor.person_data=std::vector<std::int16_t>(256,0);
        (*created_neighbor.person_data)[61]=202;
        created_neighbor.neighbor_id=13; created_neighbor.guid=created_guid;
        created_neighbor.unknown_neg_one=-1;
        odh::FamilyOverlayCreation creation{202,"New family",
            odh::encode_simitone_create_family_fami(created_family),{created_neighbor}};
        const odh::Nbrs source_neighbors{0,62,
            {neighbor(11,a),neighbor(12,b)},0};
        const auto created_overlay=odh::plan_family_creation_overlay(
            {vacant_entry},source_neighbors,neighborhood_identity,nullptr,creation);
        const auto moved_created_overlay=odh::plan_family_move_in_overlay(
            {vacant_entry},neighborhood_identity,&created_overlay,202,7,5000);
        const auto persisted_created_overlay=odh::decode_family_overlay(
            odh::encode_family_overlay(moved_created_overlay));
        const odh::FamilyContext created_families(odh::apply_family_overlay(
            {vacant_entry},neighborhood_identity,persisted_created_overlay));
        const odh::NeighborContext created_neighbors(odh::apply_neighbor_overlay(
            source_neighbors,neighborhood_identity,persisted_created_overlay));
        const odh::DefinitionIndex created_characters({character(a,"User00004.iff"),
            character(b,"User00009.iff"),character(created_guid,"PlayerCreated.iff")});
        const auto created_entry=resolve(locations,created_families,created_neighbors,
                                         created_characters);
        require(created_entry.family_chunk_id==202 && created_entry.family_number==901 &&
                created_entry.members.size()==1 && created_entry.members[0].guid==created_guid &&
                created_entry.members[0].neighbor_id==13 &&
                created_entry.members[0].character_asset=="PlayerCreated.iff",
                "created household FAMI and NBRS replay together into a resolvable lot entry");
        test_created_family_disk_reload();

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
        std::cout<<"PASS synthetic store reload replays saved created FAMI and NBRS with its character IFF\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr<<error.what()<<'\n'; return 1;
    }
}
