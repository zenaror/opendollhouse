#include "odh/family_overlay.hpp"

#include <algorithm>
#include <limits>
#include <set>
#include <sstream>

namespace odh {

FamilyOverlayCreation make_simitone_family_creation(
        const std::vector<FamilyEntry>& source_families,const Nbrs& source_neighbors,
        const std::array<std::uint8_t,32>& neighborhood_identity,
        const FamilyOverlay* current_overlay,const std::string& family_name,
        const std::vector<FamilyCreationMember>& members,std::int32_t initial_budget) {
    if(family_name.empty()||family_name.size()>256||family_name.find('\0')!=std::string::npos)
        throw FormatError("family name must contain 1..256 non-NUL bytes");
    if(members.empty()||members.size()>8||initial_budget<0)
        throw FormatError("created family must have 1..8 members and a nonnegative budget");

    const auto effective_families=apply_family_overlay(source_families,neighborhood_identity,
        current_overlay?*current_overlay:FamilyOverlay{neighborhood_identity,{},{}});
    std::set<std::uint16_t> family_ids;
    std::int32_t max_family_number=-1;
    for(const auto& family:effective_families) {
        if(!family_ids.insert(family.chunk_id).second)
            throw FormatError("cannot create family in ambiguous FAMI snapshot");
        max_family_number=std::max(max_family_number,family.family.family_number);
    }
    // Removing a household hides it from the current view, but its historical
    // identifiers still belong to that household. Never allocate from only the
    // visible snapshot: old saves and references can retain these identities.
    std::set<std::uint32_t> reserved_guids;
    const auto reserve_family=[&](const FamilyEntry& family){
        family_ids.insert(family.chunk_id);
        max_family_number=std::max(max_family_number,family.family.family_number);
        reserved_guids.insert(family.family.member_guids.begin(),family.family.member_guids.end());
    };
    for(const auto& family:source_families)reserve_family(family);
    if(current_overlay){
        for(const auto& creation:current_overlay->creations){
            // apply_family_overlay above has already authenticated and bounded
            // this creation payload, including its exact Simitone FAMI profile.
            std::string raw(creation.fami_payload.begin(),creation.fami_payload.end());
            std::istringstream stream(raw);View view(stream,raw.size());
            reserve_family({creation.family_chunk_id,read_fami(view,FamiLimits{88,8})});
            for(const auto& member:creation.members)reserved_guids.insert(member.guid);
        }
        for(const auto& assignment:current_overlay->assignments)family_ids.insert(assignment.family_chunk_id);
        for(const auto& archive:current_overlay->archives)family_ids.insert(archive.family_chunk_id);
        for(const auto& deletion:current_overlay->deletions){
            family_ids.insert(deletion.family_chunk_id);
            for(const auto& member:deletion.members)reserved_guids.insert(member.guid);
        }
    }
    if(family_ids.empty()||max_family_number==std::numeric_limits<std::int32_t>::max())
        throw FormatError("cannot allocate the next family number");
    std::uint32_t chunk_id=0;
    while(chunk_id<=std::numeric_limits<std::uint16_t>::max()&&
          family_ids.count(static_cast<std::uint16_t>(chunk_id)))++chunk_id;
    if(chunk_id>std::numeric_limits<std::uint16_t>::max())
        throw FormatError("no free FAMI chunk ID remains");

    const auto effective_neighbors=current_overlay?
        apply_neighbor_overlay(source_neighbors,neighborhood_identity,*current_overlay):source_neighbors;
    std::set<std::int16_t> neighbor_ids;
    for(const auto& entry:effective_neighbors.entries) {
        if(!entry.record||entry.record->neighbor_id<=0)continue;
        if(!neighbor_ids.insert(entry.record->neighbor_id).second)
            throw FormatError("cannot create family in ambiguous neighbor snapshot");
    }
    // Reserve immutable and removed neighbors as well as the visible ones.
    for(const auto& entry:source_neighbors.entries)if(entry.record){
        if(entry.record->neighbor_id>0)neighbor_ids.insert(entry.record->neighbor_id);
        reserved_guids.insert(entry.record->guid);
    }
    if(current_overlay){
        for(const auto& creation:current_overlay->creations)for(const auto& member:creation.members){
            neighbor_ids.insert(member.neighbor_id);reserved_guids.insert(member.guid);
        }
        for(const auto& deletion:current_overlay->deletions)for(const auto& member:deletion.members){
            neighbor_ids.insert(member.neighbor_id);reserved_guids.insert(member.guid);
        }
    }
    std::vector<std::int16_t> allocated_ids;
    std::int32_t candidate_id=1;
    for(std::size_t i=0;i<members.size();++i) {
        while(candidate_id<=std::numeric_limits<std::int16_t>::max()&&
              neighbor_ids.count(static_cast<std::int16_t>(candidate_id)))++candidate_id;
        if(candidate_id>std::numeric_limits<std::int16_t>::max())
            throw FormatError("no free neighbor ID remains");
        allocated_ids.push_back(static_cast<std::int16_t>(candidate_id));
        neighbor_ids.insert(static_cast<std::int16_t>(candidate_id++));
    }

    Family family{};
    family.version=9;
    family.house_number=0;
    family.family_number=max_family_number+1;
    family.budget=initial_budget;
    family.unknown=24;
    std::set<std::uint32_t> member_guids;
    std::vector<NeighborRecord> records;
    records.reserve(members.size());
    const auto family_word=static_cast<std::int16_t>(chunk_id<0x8000U?
        static_cast<std::int32_t>(chunk_id):static_cast<std::int32_t>(chunk_id)-65536);
    for(std::size_t i=0;i<members.size();++i) {
        const auto& input=members[i];
        if(input.guid==0||reserved_guids.count(input.guid)||
           !member_guids.insert(input.guid).second||input.person_data.size()!=88)
            throw FormatError("CAS member GUID or 88-word person data is invalid");
        family.member_guids.push_back(input.guid);
        auto data=std::vector<std::int16_t>(256,0);
        std::copy(input.person_data.begin(),input.person_data.end(),data.begin());
        data[61]=family_word;
        NeighborRecord record{};
        record.version=10;record.unknown3=9;record.name="iffname";record.mystery_zero=0;
        record.person_mode=9;record.person_data=std::move(data);record.neighbor_id=allocated_ids[i];
        record.guid=input.guid;record.unknown_neg_one=-1;
        records.push_back(std::move(record));
    }

    FamilyOverlayCreation result;
    result.family_chunk_id=static_cast<std::uint16_t>(chunk_id);
    result.family_name=family_name;
    result.fami_payload=encode_simitone_create_family_fami(family,FamiLimits{88,8});
    result.members=std::move(records);
    (void)plan_family_creation_overlay(source_families,source_neighbors,neighborhood_identity,
        current_overlay,result);
    return result;
}

} // namespace odh
