#include "linux_private_family.hpp"
#include <set>

#include "linux_family_overlay_store.hpp"
#include "linux_text_codec.hpp"
#include "odh/character_catalog.hpp"
#include "odh/formats.hpp"

#include <algorithm>
#include <exception>
#include <fstream>
#include <limits>
#include <sstream>

namespace odh_host {
namespace {
struct NeighborhoodCreationSource {
    std::array<std::uint8_t,32> identity{};
    std::vector<odh::FamilyEntry> families;
    odh::Nbrs neighbors{};
};

NeighborhoodCreationSource read_creation_source(const std::filesystem::path& path) {
    const auto before=linux_neighborhood_identity(path);
    std::ifstream input(path,std::ios::binary);
    if(!input)throw std::runtime_error("cannot open Neighborhood.iff for CAS family creation");
    const auto size=std::filesystem::file_size(path);
    if(size>64ull*1024ull*1024ull)throw odh::FormatError("Neighborhood.iff exceeds creation scan limit");
    const odh::View view(input,size);
    NeighborhoodCreationSource result;result.identity=before;
    std::optional<odh::Nbrs> neighbors;
    odh::Limits limits;limits.iff_chunks=8192;
    odh::iff_chunks(view,[&](const odh::Chunk& chunk){
        if(chunk.type=="FAMI"){
            if(result.families.size()>=4096)throw odh::FormatError("too many FAMI records for CAS creation");
            result.families.push_back({chunk.id,odh::read_fami(chunk.payload)});
        } else if(chunk.type=="NBRS"&&chunk.id==1){
            if(neighbors)throw odh::FormatError("ambiguous NBRS1 for CAS family creation");
            neighbors=odh::read_nbrs(chunk.payload);
        }
    },limits);
    if(!neighbors)throw odh::FormatError("Neighborhood.iff has no NBRS1 for CAS family creation");
    result.neighbors=std::move(*neighbors);
    if(linux_neighborhood_identity(path)!=before)
        throw odh::FormatError("Neighborhood.iff changed during CAS family preparation");
    return result;
}

bool same_overlay(const std::optional<odh::FamilyOverlay>& a,
                  const std::optional<odh::FamilyOverlay>& b) {
    if(a.has_value()!=b.has_value())return false;
    return !a||odh::encode_family_overlay(*a)==odh::encode_family_overlay(*b);
}

void rollback(const std::vector<CreatedPrivateCharacter>& created) {
    bool all_removed=true;
    for(auto it=created.rbegin();it!=created.rend();++it)
        if(!remove_linux_private_character_if_unchanged(*it))all_removed=false;
    if(!all_removed)throw std::runtime_error("CAS family failed and at least one changed private character was preserved");
}
} // namespace

odh::FamilyOverlay rename_linux_private_family(
        const std::filesystem::path& neighborhood_iff,
        const std::filesystem::path& overlay_path,
        const std::array<std::uint8_t, 32>& expected_neighborhood_identity,
        const std::optional<odh::FamilyOverlay>& expected_overlay,
        std::uint16_t family_chunk_id, const std::string& family_name_utf8,
        std::uint8_t language) {
    if (overlay_path.empty() || overlay_path.filename().empty())
        throw std::invalid_argument("family overlay path must name a file");
    const auto name = utf8_to_legacy_text(family_name_utf8, language, 1024);
    if (legacy_text_to_utf8(name, language) != family_name_utf8)
        throw std::invalid_argument("family name cannot roundtrip exactly in the selected game language");
    const auto source = read_creation_source(neighborhood_iff);
    if (source.identity != expected_neighborhood_identity)
        throw odh::FormatError("Neighborhood.iff changed since the family editor was opened");
    auto replacement = odh::plan_family_name_overlay(source.families, source.identity,
        expected_overlay ? &*expected_overlay : nullptr, family_chunk_id, name, language);
    // Validate the complete shared sidecar, including retained source records.
    (void)odh::apply_neighbor_overlay(source.neighbors, source.identity, replacement);
    if (linux_neighborhood_identity(neighborhood_iff) != source.identity)
        throw odh::FormatError("Neighborhood.iff changed before family rename was saved");
    try {
        compare_exchange_linux_family_overlay_atomic(overlay_path, expected_overlay, replacement);
    } catch (const std::runtime_error&) {
        // A post-rename durability error may have published this exact result.
        // Do not retry against or overwrite a newer player's update.
        if (!same_overlay(read_linux_family_overlay(overlay_path), replacement)) throw;
    }
    return replacement;
}

CreatedLinuxPrivateFamily create_linux_private_family(
        const std::vector<std::uint8_t>& template_iff,
        const std::filesystem::path& installed_characters,
        const std::filesystem::path& private_characters,
        const std::filesystem::path& neighborhood_iff,
        const std::filesystem::path& overlay_path,
        const std::string& family_name_utf8,
        const std::vector<LinuxPrivateFamilyMemberDraft>& members,
        std::int32_t initial_budget,CharacterGuidSource guid_source,const std::function<bool(std::uint32_t)>& guid_available) {
    if(members.empty()||members.size()>8||initial_budget<0)
        throw std::invalid_argument("CAS family must have 1..8 members and a nonnegative budget");
    if(overlay_path.empty()||overlay_path.filename().empty())
        throw std::invalid_argument("CAS family overlay path must name a file");
    const auto language=members.front().language;
    if(language<1||language>20)throw std::invalid_argument("CAS language code is outside 1..20");
    for(const auto& member:members){
        if(member.language!=language)throw std::invalid_argument("all family members must use the selected game language");
        if(member.name_utf8.empty()||member.name_utf8.size()>1024||member.biography_utf8.size()>4096||
           member.person_data.size()!=88)
            throw std::invalid_argument("CAS member text or 88-word person data is outside bounds");
    }
    const auto family_name=utf8_to_legacy_text(family_name_utf8,language,1024);
    const auto source=read_creation_source(neighborhood_iff);
    if(source.families.empty())throw odh::FormatError("cannot create a family without source FAMI records");
    auto initial_overlay=read_linux_family_overlay(overlay_path);
    if(initial_overlay&&initial_overlay->neighborhood_identity!=source.identity)
        throw odh::FormatError("family overlay belongs to different Neighborhood.iff data");
    // Validate the current sidecar and both source snapshots before creating any files.
    if(initial_overlay){
        (void)odh::apply_family_overlay(source.families,source.identity,*initial_overlay);
        (void)odh::apply_neighbor_overlay(source.neighbors,source.identity,*initial_overlay);
    }

    std::set<std::uint32_t> reserved;
    for(const auto& entry:source.neighbors.entries)if(entry.record)reserved.insert(entry.record->guid);
    for(const auto& family:source.families)reserved.insert(family.family.member_guids.begin(),family.family.member_guids.end());
    if(initial_overlay){
        for(const auto& c:initial_overlay->creations)for(const auto& n:c.members)reserved.insert(n.guid);
        if(initial_overlay->character_family)for(const auto& c:initial_overlay->character_family->characters)reserved.insert(c.identity.guid);
        if(initial_overlay->personal_state&&initial_overlay->personal_state->catalog)
            for(const auto& d:initial_overlay->personal_state->catalog->definitions)reserved.insert(d.guid);
        for(const auto& n:odh::retired_family_members(*initial_overlay))reserved.insert(n.guid);
    }
    const auto available=[&](std::uint32_t guid){return !reserved.count(guid)&&(!guid_available||guid_available(guid));};
    std::vector<CreatedPrivateCharacter> created;
    bool committed=false;
    try {
        std::vector<odh::FamilyCreationMember> family_members;
        family_members.reserve(members.size());
        for(const auto& member:members){
            odh::TemplatePersonRequest request{};
            request.language=language;
            request.name=utf8_to_legacy_text(member.name_utf8,language,4096);
            request.biography=utf8_to_legacy_text(member.biography_utf8,language,16384);
            request.body_strings=member.body_strings;
            const auto character=create_linux_private_character(template_iff,installed_characters,
                private_characters,request,guid_source,available);
            created.push_back(character);reserved.insert(character.guid);
            family_members.push_back({character.guid,member.person_data});
        }

        auto creation=odh::make_simitone_family_creation(source.families,source.neighbors,
            source.identity,initial_overlay?&*initial_overlay:nullptr,family_name,family_members,initial_budget);
        auto replacement=odh::plan_family_creation_overlay(source.families,source.neighbors,
            source.identity,initial_overlay?&*initial_overlay:nullptr,creation);
        bool saved=false;
        for(unsigned attempt=0;attempt<3&&!saved;++attempt){
            if(linux_neighborhood_identity(neighborhood_iff)!=source.identity)
                throw odh::FormatError("Neighborhood.iff changed before family creation was saved");
            try {
                compare_exchange_linux_family_overlay_atomic(overlay_path,initial_overlay,replacement);
                saved=true;
            } catch(const std::runtime_error&) {
                const auto current=read_linux_family_overlay(overlay_path);
                if(same_overlay(current,replacement)){saved=true;break;} // A post-rename durability error is ambiguous.
                if(same_overlay(current,initial_overlay))throw;
                initial_overlay=current;
                creation=odh::make_simitone_family_creation(source.families,
                    source.neighbors,source.identity,initial_overlay?&*initial_overlay:nullptr,
                    family_name,family_members,initial_budget);
                replacement=odh::plan_family_creation_overlay(source.families,source.neighbors,
                    source.identity,initial_overlay?&*initial_overlay:nullptr,creation);
            }
        }
        if(!saved)throw std::runtime_error("family overlay kept changing during CAS creation; retry later");
        committed=true;
        std::string raw_fami(creation.fami_payload.begin(),creation.fami_payload.end());
        std::istringstream fami_stream(raw_fami,std::ios::in|std::ios::binary);
        const auto family_number=odh::read_fami(odh::View(fami_stream,raw_fami.size())).family_number;
        return {creation.family_chunk_id,family_number,std::move(created)};
    } catch(...) {
        const auto original=std::current_exception();
        if(!committed)try { rollback(created); } catch(...) { throw; }
        std::rethrow_exception(original);
    }
}
} // namespace odh_host
