// SPDX-License-Identifier: MPL-2.0
#include "residential_property_scene.hpp"
#include "odh/property_disposition.hpp"
#include "odh/sha256.hpp"
#include <algorithm>
namespace odh_host {
namespace {
void need(bool v,const char* why){if(!v)throw odh::FormatError(why);}
bool same_npc(const odh::SessionNpcAvatarOrigin&a,const odh::SessionNpcAvatarOrigin&b){
    return a.id==b.id&&a.generation==b.generation&&a.guid==b.guid&&a.neighbor_id==b.neighbor_id&&
        a.source==b.source&&a.asset==b.asset&&a.object_definition_id==b.object_definition_id&&
        a.character_fingerprint==b.character_fingerprint;
}
std::array<std::uint8_t,32> digest(const std::vector<std::uint8_t>& bytes){return odh::sha256(bytes.data(),bytes.size());}
constexpr odh::SessionArchiveLimits limits{32*1024*1024,16*1024*1024,3};
}
bool ResidentialPropertyRetirementProof::matches(const odh::ActorlessObjectTradeState& state)const{
    return digest(odh::encode_actorless_object_trade_state(state))==state_sha256_;
}
bool ResidentialPropertyRetirementProof::retired_original_avatar(const odh::Entity& e)const{
    return e.kind==odh::EntityKind::avatar&&e.definition&&std::any_of(actors_.begin(),actors_.end(),[&](const auto& a){
        return a.id==e.id&&a.generation==e.generation&&a.guid==e.definition->guid;});
}
bool ResidentialPropertyRetirementProof::retired_npc(const odh::SessionNpcAvatarOrigin& origin)const{
    return std::any_of(npcs_.begin(),npcs_.end(),[&](const auto& value){return same_npc(value,origin);});
}
bool ResidentialPropertyRetirementProof::retired_household(const odh::SessionRetiredHouseholdAvatar& origin)const{
    return std::any_of(households_.begin(),households_.end(),[&](const auto& value){return odh::same_session_retired_household_avatar(value,origin);});
}
ResidentialPropertyRetirementProof prepare_residential_property_retirement(const odh::NeighborhoodFamilySource& source,
        const odh::FamilyOverlay& overlay,std::uint16_t family,const std::vector<std::uint8_t>& bytes,
        const CommunityLotDocument& document,const odh::DefinitionIndex& definitions,const odh::SessionNpcAvatarResolver& resolver,
        const odh::SessionRetiredHouseholdAvatarResolver& households){
    need(document.objects.has_value(),"residential property requires its complete object document");
    const auto& objects=*document.objects;odh::validate_actorless_object_trade_state(objects);
    const auto replay=odh::replay_family_overlay(source.families,source.identity,overlay);
    const auto state=std::find_if(replay.states.begin(),replay.states.end(),[&](const auto& s){return s.family_chunk_id==family;});
    const auto entry=std::find_if(replay.families.begin(),replay.families.end(),[&](const auto& s){return s.chunk_id==family;});
    need(state!=replay.states.end()&&entry!=replay.families.end()&&state->managed&&state->checkpoint_sha256&&
        state->house_number==document.identity.house&&document.identity.neighborhood_sha256==source.identity&&
        state->checkpoint_house==state->house_number&&state->checkpoint_epoch==state->occupancy_epoch&&
        state->checkpoint_budget==state->budget&&
        digest(bytes)==*state->checkpoint_sha256,"residential retirement source is not its occupied head");
    const auto saved=odh::decode_session_bundle(bytes,objects.resources);
    need(saved.family_lease&&saved.family_lease->neighborhood_identity==source.identity&&
        saved.family_lease->family_identity==state->family_identity&&saved.family_lease->family_chunk_id==family&&
        saved.family_lease->occupancy_epoch==state->occupancy_epoch&&saved.host.house_number==state->house_number&&
        saved.host.family_budget==state->checkpoint_budget&&saved.host.family_chunk_id==family&&
        saved.host.family_number==entry->family.family_number&&objects.tick==saved.host.tick,
        "residential retirement source lease, clock or finances differ");
    need(std::none_of(objects.world.entities.begin(),objects.world.entities.end(),[](const auto& e){
        return e.kind==odh::EntityKind::avatar;}),"retained live actors require an executable avatar-state property profile");
    std::vector<std::uint32_t> members;for(const auto& member:saved.household_roster)members.push_back(member.guid);
    auto expected=entry->family.member_guids;std::sort(members.begin(),members.end());std::sort(expected.begin(),expected.end());
    need(!members.empty()&&members==expected,"residential retirement source household is unauthenticated");
    ResidentialPropertyRetirementProof proof;proof.state_sha256_=digest(odh::encode_actorless_object_trade_state(objects));
    for(const auto& before:saved.world.entities){
        if(before.kind!=odh::EntityKind::avatar)continue;
        const auto live=std::find_if(objects.world.entities.begin(),objects.world.entities.end(),[&](const auto& e){return e.id==before.id&&e.generation==before.generation;});
        if(live!=objects.world.entities.end())continue;
        need(before.definition.has_value(),"retired source avatar has no exact resource identity");
        const auto* own=definitions.unique(before.definition->guid);const auto flags=before.object_words.find(43);
        need(own&&flags!=before.object_words.end(),"retired source avatar has unknown source disposition");
        const auto disposition=odh::inspect_property_disposition(definitions,before.definition->guid,own->definition.object_type,
            static_cast<std::uint16_t>(flags->second));
        need(disposition.disposition==odh::PropertyDisposition::remove,
            "retained or unknown source avatar cannot disappear through unproved property filtering");
        const auto member=std::find_if(saved.household_roster.begin(),saved.household_roster.end(),[&](const auto& value){
            return value.id==before.id&&value.generation==before.generation;});
        if(member!=saved.household_roster.end()){
            odh::SessionRetiredHouseholdAvatar expected{*member,*saved.family_lease,digest(bytes)};
            need(std::any_of(objects.history.retired_household_avatars.begin(),objects.history.retired_household_avatars.end(),[&](const auto& value){
                return odh::same_session_retired_household_avatar(expected,value);}),
                "retired household avatar lacks its distinct sealed source provenance");
        }
        proof.actors_.push_back({before.id,before.generation,before.definition->guid});
    }
    for(const auto& origin:objects.history.retired_npc_avatars){
        bool known=false;
        for(const auto& npc:saved.npc_avatars)known=known||same_npc(origin,npc.origin);
        if(saved.object_history)for(const auto& old:saved.object_history->retired_npc_avatars)known=known||same_npc(origin,old);
        if(!known){
            need(resolver&&origin.generation>saved.world.generation_sequence,
                "new retired visitor lacks actual verified constructor provenance");
            const auto metadata=resolver(origin);
            need(metadata.id==origin.id&&metadata.generation==origin.generation&&metadata.kind==odh::EntityKind::avatar&&
                metadata.definition&&metadata.definition->guid==origin.guid&&!metadata.position&&!metadata.attachment&&!metadata.ui_ghost,
                "retired visitor resource reconstruction differs from exact origin");
        }
        proof.npcs_.push_back(origin);
    }
    if(saved.object_history)for(const auto& previous:saved.object_history->retired_household_avatars)
        need(std::any_of(objects.history.retired_household_avatars.begin(),objects.history.retired_household_avatars.end(),[&](const auto& value){
            return odh::same_session_retired_household_avatar(previous,value);}),
            "retirement export omitted authenticated historical household provenance");
    for(const auto& origin:objects.history.retired_household_avatars){
        if(origin.source_checkpoint_sha256==digest(bytes))odh::validate_retired_household_avatar_source(origin,bytes);
        else{
            need(saved.object_history&&std::any_of(saved.object_history->retired_household_avatars.begin(),
                saved.object_history->retired_household_avatars.end(),[&](const auto& value){return odh::same_session_retired_household_avatar(value,origin);}),
                "retired household history was not present in the authenticated source");
            need(static_cast<bool>(households),"historical retired household resource owner is unavailable");
            const auto metadata=households(origin);const auto& member=origin.member;
            need(metadata.id==member.id&&metadata.generation==member.generation&&metadata.kind==odh::EntityKind::avatar&&
                metadata.definition&&metadata.definition->guid==member.guid&&!metadata.position&&!metadata.attachment&&!metadata.ui_ghost,
                "historical retired household resource differs from exact provenance");
        }
        proof.households_.push_back(origin);
    }
    return proof;
}
ResidentialPropertyRetirementProof restore_residential_property_retirement(const odh::NeighborhoodFamilySource& source,
        const odh::FamilyOverlay& committed,const odh::FamilyPropertyRelease& release,const std::vector<std::uint8_t>& bytes,
        const CommunityLotDocument& document,const odh::DefinitionIndex& definitions,const odh::SessionNpcAvatarResolver& resolver,
        const odh::SessionRetiredHouseholdAvatarResolver& households){
    const auto replay=odh::replay_family_overlay(source.families,source.identity,committed);
    const auto found=std::find_if(committed.property_releases.begin(),committed.property_releases.end(),[&](const auto& value){
        return value.assignment_index==release.assignment_index&&value.head==release.head&&
            value.source_checkpoint_sha256==release.source_checkpoint_sha256&&value.family_identity==release.family_identity&&
            value.occupancy_epoch==release.occupancy_epoch&&value.family_chunk_id==release.family_chunk_id&&
            value.house==release.house&&value.installed_lot_sha256==release.installed_lot_sha256&&
            value.neighborhood==release.neighborhood&&value.mode==release.mode&&value.policy==release.policy&&
            value.vacant_purchase_value==release.vacant_purchase_value&&value.cash_after_cleanup==release.cash_after_cleanup&&
            value.expected_revision==release.expected_revision&&value.expected_head==release.expected_head;});
    need(found!=committed.property_releases.end(),"residential property reference is not a committed release");
    const auto current=std::find_if(replay.properties.begin(),replay.properties.end(),
        [&](const auto& value){return value.house==found->house;});
    need(current!=replay.properties.end()&&current->head==found->head&&
        current->revision==found->expected_revision+1&&current->installed_lot_sha256==found->installed_lot_sha256&&
        current->neighborhood==found->neighborhood&&current->mode==found->mode&&current->policy==found->policy&&
        current->vacant_purchase_value==found->vacant_purchase_value,
        "residential property activation requires the current committed house head");
    need(document.identity.house==found->house&&document.identity.mode==found->mode&&
        document.identity.neighborhood==found->neighborhood&&document.identity.lot_sha256==found->installed_lot_sha256&&
        digest(odh::encode_session_archive(encode_community_lot_document(document),limits))==found->head&&
        digest(bytes)==found->source_checkpoint_sha256,"residential property document/source differs from its committed head");
    auto prefix=committed;prefix.assignments.resize(found->assignment_index);
    prefix.property_releases.erase(std::remove_if(prefix.property_releases.begin(),prefix.property_releases.end(),
        [&](const auto& value){return value.assignment_index>=found->assignment_index;}),prefix.property_releases.end());
    prefix.archives.clear();prefix.deletions.clear();prefix.names.clear();
    return prepare_residential_property_retirement(source,prefix,found->family_chunk_id,bytes,document,definitions,resolver,households);
}
}
