// SPDX-License-Identifier: MPL-2.0
#include "odh/native_personal_session.hpp"
#include <algorithm>
#include <filesystem>
namespace odh {
namespace {
NativePersonalIdentity identity(const SessionHouseholdMember& m){return {m.neighbor_id,m.guid,m.object_definition_id,m.character_asset,m.character_fingerprint};}
NativePersonalIdentity identity(const SessionNpcAvatarOrigin& m){
 return {m.neighbor_id,m.guid,m.object_definition_id,
   native_personal_resource_key(m.source,m.asset),m.character_fingerprint};
}
}
void project_native_personal_session(SessionBundle& destination,const NativePersonalSnapshot& shared){
 validate_native_personal_snapshot(shared);
 if(destination.family_lease&&destination.family_lease->neighborhood_identity!=shared.neighborhood_identity)
  throw FormatError("personal projection destination neighborhood differs");
 if(destination.travel&&destination.travel->neighborhood_identity!=shared.neighborhood_identity)
  throw FormatError("personal projection travel neighborhood differs");
 if(destination.personal_state)validate_native_personal_successor(*destination.personal_state,shared);
 auto candidate=destination;
 for(const auto& record:shared.records){
  if(destination.personal_state){const auto& before=destination.personal_state->records;
   const auto old=std::find_if(before.begin(),before.end(),[&](const auto& r){return r.identity.neighbor_id==record.identity.neighbor_id;});
   if(old!=before.end()&&old->revision==record.revision)continue;
  }
  std::optional<WorldEntityReference> target;
  const auto admit=[&](const NativePersonalIdentity& actual,WorldEntityReference body){
   if(actual.neighbor_id!=record.identity.neighbor_id)return;
   if(!same_native_personal_identity(actual,record.identity))throw FormatError("personal projection character provenance differs");
   if(target)throw FormatError("personal projection has duplicate live stable identity");target=body;
  };
  for(const auto& m:candidate.household_roster)if(m.neighbor_id==record.identity.neighbor_id)admit(identity(m),{m.id,m.generation});
  for(const auto& m:candidate.npc_avatars)if(m.origin.neighbor_id==record.identity.neighbor_id)admit(identity(m.origin),{m.origin.id,m.origin.generation});
  if(!target)continue;
  const auto found=std::find_if(candidate.world.entities.begin(),candidate.world.entities.end(),[&](const auto& e){return e.id==target->id&&e.generation==target->generation;});
  if(found==candidate.world.entities.end()||found->kind!=EntityKind::avatar||!found->definition||
     found->definition->guid!=record.identity.guid||found->person_data.size()<=82||found->person_data[31]!=record.identity.neighbor_id)
   throw FormatError("personal projection live body differs from resource owner");
  // Family binding belongs to family membership authority. This selective
  // writer cannot silently turn an NPC into a resident or change FAMI/HHRD.
  if(record.projection.present[61]&&record.projection.values[61]!=found->person_data[61])
   throw FormatError("personal projection family binding requires a family transaction");
  apply_native_personal_projection(record.projection,found->person_data);
 }
 candidate.personal_state=shared;destination=std::move(candidate);
}
bool publish_native_personal_session(World& world,const SessionBundle& restored,const NativePersonalSnapshot& shared){
 auto candidate=restored;candidate.world=world.snapshot();project_native_personal_session(candidate,shared);
 std::vector<AvatarPersonalStateChange> changes;
 for(const auto& after:candidate.world.entities){const auto* before=world.find(after.id);
  if(!before||before->generation!=after.generation)return false;
  if(before->person_data==after.person_data)continue;
  changes.push_back({{before->id,before->generation},{before->person_data,before->motives,before->avatar_appearance},
      {after.person_data,before->motives,before->avatar_appearance},before->position,before->attachment});
 }
 return changes.empty()||world.publish_avatar_personal_states(changes);
}
} // namespace odh
