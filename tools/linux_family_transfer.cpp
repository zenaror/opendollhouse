#include "linux_family_transfer.hpp"
#include "linux_family_overlay_store.hpp"
#include "linux_session_archive_store.hpp"
#include "odh/sha256.hpp"
#include <algorithm>
#include <fstream>

namespace odh_host {
namespace {
std::array<std::uint8_t,32> fingerprint(const std::filesystem::path& path) {
    if(!std::filesystem::is_regular_file(path)||std::filesystem::file_size(path)>64u*1024u*1024u)
        throw odh::FormatError("family transfer character is outside the bounded file profile");
    std::ifstream input(path,std::ios::binary);
    if(!input)throw odh::FormatError("cannot read family transfer character");
    odh::Sha256 hash;std::array<char,65536> block{};
    while(input){input.read(block.data(),static_cast<std::streamsize>(block.size()));
        if(input.gcount()>0)hash.update(block.data(),static_cast<std::size_t>(input.gcount()));}
    if(!input.eof())throw odh::FormatError("cannot finish family transfer character fingerprint");
    return hash.finish();
}
std::string hex(const std::array<std::uint8_t,32>& bytes){
    constexpr char chars[]="0123456789abcdef";std::string out;
    for(auto byte:bytes){out+=chars[byte>>4];out+=chars[byte&15];}return out;
}
}
std::array<std::uint8_t,32> fingerprint_transfer_resource(const std::filesystem::path& path){
    return fingerprint(path);
}
std::vector<odh::HouseholdTransferIdentity> identify_transfer_household(
        const odh::SessionEntry& session,const std::filesystem::path& game,
        const std::filesystem::path& player_root,LinuxNeighborhoodStorage storage){
    std::vector<odh::HouseholdTransferIdentity> result;
    for(const auto& member:session.members){
        const auto filename=std::filesystem::path(member.character_asset);
        if(filename.empty()||filename.has_parent_path()||filename.is_absolute())
            throw odh::FormatError("family transfer character must be a flat catalog asset");
        std::filesystem::path root;
        if(member.character_source=="UserData/Characters")root=game/storage.user_data_name()/"Characters";
        else if(member.character_source=="PlayerData/Characters"&&!player_root.empty())root=player_root;
        else throw odh::FormatError("unknown family transfer character source");
        if(std::filesystem::is_symlink(std::filesystem::symlink_status(root)))
            throw odh::FormatError("family transfer character root cannot be a symbolic link");
        root=std::filesystem::canonical(root);
        const auto path=std::filesystem::canonical(root/filename);
        if(path.parent_path()!=root)
            throw odh::FormatError("family transfer character escapes its explicit catalog");
        result.push_back({member.guid,static_cast<std::int16_t>(member.neighbor_id),
            member.object_definition_id,member.character_source+"/"+member.character_asset,fingerprint(path)});
    }
    return result;
}
std::optional<odh::PreparedHouseholdTransfer> load_family_transfer(
        const odh::NeighborhoodFamilySource& source,const odh::FamilyOverlay& overlay,
        std::uint16_t id,const std::vector<odh::HouseholdTransferIdentity>& members,
        const std::filesystem::path& neighborhood_iff,LinuxNeighborhoodStorage storage){
    storage.verify_neighborhood_source(neighborhood_iff);
    const auto replay=odh::replay_family_overlay(source.families,source.identity,overlay);
    const auto state=std::find_if(replay.states.begin(),replay.states.end(),
        [&](const auto& s){return s.family_chunk_id==id;});
    const auto family=std::find_if(replay.families.begin(),replay.families.end(),
        [&](const auto& f){return f.chunk_id==id;});
    if(state==replay.states.end()||family==replay.families.end())
        throw odh::FormatError("family transfer family is absent from the pinned neighborhood");
    if(!state->checkpoint_sha256)return std::nullopt;
    if(!state->managed||!state->checkpoint_budget||state->checkpoint_house<1||state->checkpoint_house>98)
        throw odh::FormatError("family transfer has no complete historical checkpoint provenance");
    std::vector<std::uint32_t> actual,expected=family->family.member_guids;
    for(const auto& member:members)actual.push_back(member.guid);
    std::sort(actual.begin(),actual.end());std::sort(expected.begin(),expected.end());
    if(actual!=expected)throw odh::FormatError("family transfer roster differs from the selected family");
    odh::HouseholdTransferTicket ticket;
    ticket.source_lease={source.identity,state->family_identity,state->checkpoint_epoch,id};
    ticket.source_house=state->checkpoint_house;ticket.family_number=family->family.family_number;
    ticket.checkpoint_budget=*state->checkpoint_budget;ticket.archive_sha256=*state->checkpoint_sha256;
    const auto directory=linux_family_overlay_path(source.identity,storage).parent_path()/"sessions"/hex(source.identity);
    const auto archive=read_session_archive(directory/(hex(ticket.archive_sha256)+".odhs"));
    auto prepared=odh::prepare_household_transfer(odh::encode_session_archive(archive),ticket,members);
    // Prove compatibility with the fresh inheritance baseline before eviction,
    // rather than stranding unknown progress in an unhoused family.
    if(linux_neighborhood_identity(neighborhood_iff)!=source.identity)
        throw odh::FormatError("family transfer neighborhood changed before baseline preflight");
    std::optional<odh::Nbrs> neighbors;
    std::ifstream input(neighborhood_iff,std::ios::binary);
    if(!input)throw odh::FormatError("cannot read family transfer neighbor baseline");
    odh::iff_chunks(odh::View(input,std::filesystem::file_size(neighborhood_iff)),[&](const odh::Chunk& chunk){
        if(chunk.type!="NBRS"||chunk.id!=1)return;
        if(neighbors)throw odh::FormatError("ambiguous family transfer neighbor baseline");
        neighbors=odh::read_nbrs(chunk.payload);
    });
    if(!neighbors)throw odh::FormatError("family transfer has no neighbor baseline");
    odh::NeighborContext context(odh::apply_neighbor_overlay(*neighbors,source.identity,overlay));
    for(const auto& member:prepared.members()){
        const auto* neighbor=context.by_id(member.identity.neighbor_id);
        if(!neighbor||neighbor->guid!=member.identity.guid||!neighbor->person_data)
            throw odh::FormatError("family transfer requires a saved neighbor person baseline");
        for(unsigned index:{9U,16U,19U,20U,24U,29U,54U,55U,66U,67U,77U}){
            const auto baseline=index<neighbor->person_data->size()?(*neighbor->person_data)[index]:std::int16_t{0};
            if(member.person_data[index]!=baseline)
                throw odh::FormatError("family transfer unknown PersonData["+std::to_string(index)+"] changed since fresh inheritance");
        }
        // VMAvatar.Init/apply_avatar_body resets all motives to100 except sleep.
        for(unsigned index:{0U,1U,2U,4U,10U,12U})if(member.motives[index]!=100)
            throw odh::FormatError("family transfer unknown motive["+std::to_string(index)+"] changed since initialization");
    }
    return prepared;
}
}
