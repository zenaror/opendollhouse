// SPDX-License-Identifier: MPL-2.0
#include "odh/original_companion_protocol.hpp"
#include "odh/session_bundle.hpp"
#include <algorithm>
#include <set>
#include <filesystem>
namespace odh {
namespace {
constexpr std::array<unsigned,11> keys{0,1,2,3,4,5,6,9,10,11,12};
constexpr std::array<unsigned,7> motives{9,6,5,15,7,8,14};
std::int16_t word(std::uint32_t value){const auto bits=value&65535U;return static_cast<std::int16_t>(bits<32768?static_cast<std::int32_t>(bits):static_cast<std::int32_t>(bits)-65536);}
void require_original(const NeighborInventory& inventory){if(inventory.protocol()!=Ts1TravelProtocol::original_6e598)throw FormatError("original companion operation requires NINV2");}
const Entity& body(const World& world,WorldEntityReference ref){const auto* e=world.find(ref.id);
 if(!e||e->generation!=ref.generation||e->dead||e->ui_ghost||e->kind!=EntityKind::avatar||!e->definition||e->person_data.size()<=82||e->person_data[31]<=0||e->motives.size()!=16)throw FormatError("original travel source/target is not a complete exact avatar");return *e;}
Ts1TravelMember authenticated(const World& world,WorldEntityReference ref,const OriginalTravelIdentityResolver& resolve){
 const auto& e=body(world,ref);if(!resolve)throw FormatError("original travel resource authority unavailable");const auto id=resolve(ref);
 if(id.guid!=e.definition->guid||id.neighbor_id!=e.person_data[31]||id.character_asset.empty()||
    std::all_of(id.character_fingerprint.begin(),id.character_fingerprint.end(),[](auto c){return c==0;}))throw FormatError("original travel body/resource mismatch");return id;
}
bool item_matches(const InventoryItem& i,const OriginalCompanionMarker& m){return i.type==m.type&&i.guid==m.guid&&i.count==m.count;}
void append_effect(std::vector<InventoryItem>& items,std::uint32_t key,std::int16_t count){if(count)items.push_back({2,key,static_cast<std::uint16_t>(count)});}
std::string npc_asset(const SessionNpcAvatarOrigin& o){
 const std::filesystem::path p(o.source);if(!o.asset.empty()||p.parent_path().filename()!="Characters")throw FormatError("original travel NPC carrier is not a character IFF");
 return std::string(o.source.rfind("PlayerData/",0)==0?"PlayerData/Characters/":"UserData/Characters/")+p.filename().string();
}
}
std::array<std::int16_t,11> original_travel_values(const Entity& e){
 if(e.person_data.size()<=82||e.motives.size()!=16)throw FormatError("original travel capture lacks personal/motive banks");
 std::array<std::int16_t,11> out{};for(std::size_t n=0;n<7;++n)out[n]=e.motives[motives[n]];
 out[7]=e.person_data[8];out[8]=e.person_data[80];out[9]=e.person_data[82];out[10]=e.person_data[81];return out;
}
std::optional<OriginalCompanionMarker> select_original_companion(const NeighborInventory& inventory,
 const NeighborContext& neighbors,std::int16_t owner,std::int32_t type){
 require_original(inventory);if(type!=1&&type!=3)throw FormatError("original companion direction is not type1/3");
 const auto* list=inventory.items(owner);if(!list)return {};
 for(std::size_t n=0;n<list->size();++n){const auto& item=(*list)[n];if(item.type!=type)continue;
  std::optional<std::int16_t> target;
  for(const auto& row:neighbors.source().entries)if(row.record&&row.record->guid==item.guid&&neighbors.by_id(row.record->neighbor_id)){
   if(target)throw FormatError("original companion GUID has ambiguous neighbor identity");target=row.record->neighbor_id;
  }
  if(!target||*target<=0)throw FormatError("original companion GUID has no authenticated neighbor");
  return OriginalCompanionMarker{owner,*target,type,item.guid,static_cast<std::uint32_t>(n),item.count};
 }
 return {};
}
std::optional<WorldEntityReference> lookup_original_companion(const World& world,const OriginalCompanionMarker& marker,
 const OriginalTravelIdentityResolver& resolve){
 std::optional<WorldEntityReference> found;
 for(const auto& e:world.snapshot().entities){if(e.dead||e.ui_ghost||e.kind!=EntityKind::avatar)continue;
  const bool nid=e.person_data.size()>31&&e.person_data[31]==marker.target_neighbor;
  const bool guid=e.definition&&e.definition->guid==marker.guid;
  if(!nid&&!guid)continue;
  if(!nid||!guid||found)throw FormatError("original companion live identity is conflicting or duplicated");
  const WorldEntityReference ref{e.id,e.generation};const auto id=authenticated(world,ref,resolve);
  if(id.neighbor_id!=marker.target_neighbor||id.guid!=marker.guid)throw FormatError("original companion resolved another stable identity");found=ref;
 }
 return found;
}
PreparedNeighborInventory prepare_original_companion_consumption(const NeighborInventory& inventory,const OriginalCompanionMarker& marker){
 require_original(inventory);auto next=inventory.snapshot();const auto row=next.inventory_by_id.find(marker.owner_neighbor);
 if(row==next.inventory_by_id.end()||marker.index>=row->second.size()||!item_matches(row->second[marker.index],marker))throw FormatError("original companion marker changed before consumption");
 for(std::size_t n=0;n<marker.index;++n)if(row->second[n].type==marker.type)throw FormatError("original companion marker is no longer the first direction token");
 auto& items=row->second;auto at=items.begin()+marker.index;
 if(word(at->count)<=1)items.erase(at);else --at->count;
 return inventory.prepare_replacement(next);
}
std::pair<std::int16_t,std::int16_t> original_travel_time(std::int16_t destination,std::int16_t hour,std::int16_t minute,
 std::int16_t add_hour,std::int16_t add_minute,bool residential){
 if(residential&&destination>=40&&destination<50)return {add_hour,add_minute};
 auto m=word(static_cast<std::uint32_t>(static_cast<std::int32_t>(minute)+add_minute));
 std::int32_t h=hour;if(m>=60){++h;m=static_cast<std::int16_t>(m%60);}
 auto result=word(static_cast<std::uint32_t>(h+add_hour));if(result>=24)result=static_cast<std::int16_t>(result%24);return {result,m};
}
PreparedOriginalTravelCapture prepare_original_travel_capture(const World& world,const NeighborInventory& inventory,
 const NeighborContext& neighbors,WorldEntityReference caller,const std::vector<Ts1TravelDepartureMember>& family,
 const Ts1Temps& temps,std::int16_t hour,std::int16_t minute,bool residential,const OriginalTravelIdentityResolver& resolve){
 require_original(inventory);if(temps[3]!=0&&temps[3]!=1)throw FormatError("original travel Temp3 must select single0/family1");
 PreparedOriginalTravelCapture out;auto& receipt=out.receipt;receipt.source_temps={temps[1],temps[2],temps[3]};
 receipt.source_hour=hour;receipt.source_minute=minute;receipt.source_residential=residential;
 const auto caller_identity=authenticated(world,caller,resolve);receipt.time_owner=caller_identity.neighbor_id;
 auto next=inventory.snapshot();std::set<std::int16_t> captured;
 const auto capture=[&](WorldEntityReference ref,bool companion){
  const auto identity=authenticated(world,ref,resolve);if(!captured.insert(identity.neighbor_id).second)throw FormatError("original capture repeats a stable source identity");
  OriginalTravelEffectReceipt row{{identity,ref},original_travel_values(body(world,ref)),companion};
  auto& items=next.inventory_by_id[identity.neighbor_id];
  // Original0045a399 explicitly clears ALL type2 at this authored boundary.
  items.erase(std::remove_if(items.begin(),items.end(),[](const auto& i){return i.type==2;}),items.end());
  for(std::size_t n=0;n<keys.size();++n)append_effect(items,keys[n],row.values[n]);receipt.effects.push_back(std::move(row));
 };
 if(temps[3]==0){
  receipt.companion=select_original_companion(inventory,neighbors,caller_identity.neighbor_id,temps[0]==-1?3:1);
  if(receipt.companion){const auto selected=lookup_original_companion(world,*receipt.companion,resolve);
   if(selected&&*selected!=caller)capture(*selected,true); // Self marker's source is the caller, captured once.
  }
  capture(caller,false);
 }else{
  if(family.empty()||family.size()>8)throw FormatError("original family departure needs its explicit bounded FAMI list");
  bool caller_present=false;
  for(std::size_t n=0;n<family.size();++n){const auto& selected=family[n];const auto actual=authenticated(world,selected.avatar,resolve);
   if(!same_ts1_travel_member(actual,selected.member))throw FormatError("original family source resource differs from FAMI list");
   caller_present=caller_present||selected.avatar==caller;capture(selected.avatar,false);if(n==0)receipt.time_owner=actual.neighbor_id;
  }
  if(!caller_present)throw FormatError("original family capture omitted its caller");
 }
 const auto time=original_travel_time(temps[0],hour,minute,temps[1],temps[2],residential);receipt.time_hour=time.first;receipt.time_minute=time.second;
 auto& items=next.inventory_by_id[receipt.time_owner];append_effect(items,7,time.first);append_effect(items,8,time.second);
 if(receipt.companion){const auto& list=next.inventory_by_id.at(receipt.companion->owner_neighbor);const auto found=std::find_if(list.begin(),list.end(),[&](const auto& i){return i.type==receipt.companion->type;});
  if(found==list.end()||!item_matches(*found,*receipt.companion))throw FormatError("original capture displaced selected companion marker");receipt.companion->index=static_cast<std::uint32_t>(found-list.begin());}
 out.inventory=inventory.prepare_replacement(next);return out;
}
void apply_original_travel_effects(World& world,NeighborInventory& inventory,WorldEntityReference target,
 const OriginalTravelOutfitResolver& outfit){
 require_original(inventory);const auto& live=body(world,target);const auto before=live;
 auto after=AvatarPersonalState{before.person_data,before.motives,before.avatar_appearance};auto next=inventory.snapshot();
 const auto row=next.inventory_by_id.find(before.person_data[31]);if(row==next.inventory_by_id.end())return;auto& items=row->second;
 bool changed=false;for(;;){const auto it=std::find_if(items.begin(),items.end(),[](const auto& i){return i.type==2;});if(it==items.end())break;
  const auto key=it->guid;const auto value=word(it->count);
  if(key<=6)after.motives[motives[key]]=value; // Original0045d29f stores directly, without native SetMotive clamping.
  else if(key==9){
   std::optional<AvatarOutfitReference> selected;
   if(!outfit||!world.evaluate_appearance_lookup([&]{selected=outfit(world,target.id,static_cast<std::uint16_t>(value));})||!selected)
    throw FormatError("original effect outfit lacks a read-only authenticated suit resolution");
   after.person_data[8]=value;if(!after.appearance)after.appearance.emplace();after.appearance->body=*selected;
  }else if(key==10)after.person_data[80]=value;
  else if(key==11)after.person_data[82]=value;
  else if(key==12)after.person_data[81]=value;
  else break; // Source repeatedly sees the same unremoved first type2 entry.
  items.erase(it);changed=true;
 }
 if(!changed)return;auto prepared=inventory.prepare_replacement(next);
 if(!inventory.replacement_current(prepared)||!world.publish_avatar_personal_states({{target,
    {before.person_data,before.motives,before.avatar_appearance},std::move(after),before.position,before.attachment}}))
  throw FormatError("original effect source changed before atomic publication");
 if(!inventory.commit_replacement(std::move(prepared)))throw FormatError("original effect inventory lost its exclusive publication boundary");
}
PrimitiveOutcome consume_original_travel_time(NeighborInventory& inventory,std::int16_t caller){
 require_original(inventory);auto next=inventory.snapshot();const auto row=next.inventory_by_id.find(caller);
 PrimitiveOutcome result{PrimitiveDisposition::goto_true,{}};if(row==next.inventory_by_id.end())return result;
 auto& items=row->second;auto found=std::find_if(items.begin(),items.end(),[](const auto& i){return i.type==2&&i.guid==7;});if(found==items.end())return result;
 result.temp_write=VmTempWrite{0,word(found->count)};items.erase(found);
 found=std::find_if(items.begin(),items.end(),[](const auto& i){return i.type==2&&i.guid==8;});
 if(found!=items.end()){result.second_temp_write=VmTempWrite{1,word(found->count)};items.erase(found);}
 auto prepared=inventory.prepare_replacement(next);if(!inventory.commit_replacement(std::move(prepared)))throw FormatError("original time inventory changed");return result;
}
void validate_original_travel_capture(const SessionBundle& b){
 if(!b.travel||b.travel->protocol!=Ts1TravelProtocol::original_6e598||!b.travel->pending||!b.travel->pending->original||!b.neighbor_inventory||b.neighbor_inventory->version!=2)throw FormatError("original travel capture profile incomplete");
 const auto& request=*b.travel->pending;const auto& capture=*request.original;
 if(!request.continuation||request.continuation->temps[0]!=request.destination||
    request.continuation->temps[1]!=capture.source_temps[0]||request.continuation->temps[2]!=capture.source_temps[1]||request.continuation->temps[3]!=capture.source_temps[2])throw FormatError("original departure Temps differ from sealed source VM");
 if(!request.departure_transit||*request.departure_transit!=capture.source_temps[2])throw FormatError("original family selection differs from Temp3");
 const auto time=original_travel_time(request.destination,capture.source_hour,capture.source_minute,capture.source_temps[0],capture.source_temps[1],capture.source_residential);
 if(time.first!=capture.time_hour||time.second!=capture.time_minute)throw FormatError("original source time recipe changed");
 std::set<std::int16_t> seen;std::size_t companion_count=0;bool time_owner=false,caller=false;
 for(const auto& receipt:capture.effects){const auto& selected=receipt.source;const auto& id=selected.member;
  if(!seen.insert(id.neighbor_id).second)throw FormatError("duplicate original effect recipient");
  const Entity* source=nullptr;for(const auto& e:b.world.entities)if(e.id==selected.avatar.id&&e.generation==selected.avatar.generation)source=&e;
  if(!source||source->dead||source->ui_ghost||source->kind!=EntityKind::avatar||!source->definition||source->definition->guid!=id.guid||source->person_data.size()<=82||source->person_data[31]!=id.neighbor_id||original_travel_values(*source)!=receipt.values)throw FormatError("original effect receipt differs from exact source body");
  unsigned carriers=0;
  for(const auto& m:b.household_roster)if(m.id==selected.avatar.id&&m.generation==selected.avatar.generation){
   if(m.guid!=id.guid||m.neighbor_id!=id.neighbor_id||m.object_definition_id!=id.definition_chunk_id||m.character_asset!=id.character_asset||m.character_fingerprint!=id.character_fingerprint)throw FormatError("original effect HHRD provenance differs");++carriers;}
  for(const auto& n:b.npc_avatars)if(n.origin.id==selected.avatar.id&&n.origin.generation==selected.avatar.generation){const auto& m=n.origin;
   if(m.guid!=id.guid||m.neighbor_id!=id.neighbor_id||m.object_definition_id!=id.definition_chunk_id||npc_asset(m)!=id.character_asset||m.character_fingerprint!=id.character_fingerprint)throw FormatError("original effect NPC provenance differs");++carriers;}
  if(carriers!=1)throw FormatError("original effect source lacks unique resource ownership");
  if(receipt.companion){++companion_count;if(!capture.companion||capture.companion->target_neighbor!=id.neighbor_id||capture.companion->guid!=id.guid||capture.source_temps[2]!=0)throw FormatError("original companion effects lack selected marker");}
  else {const auto selected_member=std::find_if(request.departure_members.begin(),request.departure_members.end(),[&](const auto& m){return m.avatar==selected.avatar&&same_ts1_travel_member(m.member,id);});if(selected_member==request.departure_members.end())throw FormatError("original caller/family effect lacks departure membership");}
  caller=caller||(selected.avatar.id==request.source_frame.context.caller&&selected.avatar.generation==request.source_frame.context.caller_generation&&!receipt.companion);
  time_owner=time_owner||id.neighbor_id==capture.time_owner;
  std::vector<InventoryItem> expected;for(std::size_t i=0;i<keys.size();++i)append_effect(expected,keys[i],receipt.values[i]);
  if(id.neighbor_id==capture.time_owner){append_effect(expected,7,capture.time_hour);append_effect(expected,8,capture.time_minute);}
  const auto inventory=b.neighbor_inventory->inventory_by_id.find(id.neighbor_id);if(inventory==b.neighbor_inventory->inventory_by_id.end())throw FormatError("original effect inventory absent");
  std::vector<InventoryItem> actual;for(const auto& item:inventory->second)if(item.type==2)actual.push_back(item);
  NeighborInventorySnapshot x{2,{{id.neighbor_id,expected}}},y{2,{{id.neighbor_id,actual}}};if(!same_neighbor_inventory_snapshot(x,y))throw FormatError("original effect inventory differs from literal source writer including zero absence");
 }
 if(capture.source_temps[2]==1){
  if(request.departure_members.size()!=b.travel->permanent_roster.size()||capture.time_owner!=b.travel->permanent_roster.front().neighbor_id)throw FormatError("original family time owner differs from FAMI index0");
  for(std::size_t i=0;i<request.departure_members.size();++i)
   if(!same_ts1_travel_member(request.departure_members[i].member,b.travel->permanent_roster[i]))throw FormatError("original family source order differs from FAMI");
 }else if(capture.time_owner!=request.departure_members.front().member.neighbor_id)throw FormatError("original single time owner differs from caller");
 if(!caller||!time_owner||companion_count>1||capture.effects.size()!=request.departure_members.size()+companion_count)throw FormatError("original capture source cohort incomplete");
 const auto caller_nid=request.departure_members.empty()?0:request.departure_members.front().member.neighbor_id;
 if(capture.source_temps[2]==0){
  const auto owner=b.neighbor_inventory->inventory_by_id.find(caller_nid);
  bool marker=false;if(owner!=b.neighbor_inventory->inventory_by_id.end())for(const auto& item:owner->second)if(item.type==(request.destination==-1?3:1)){marker=true;break;}
  if(marker!=capture.companion.has_value())throw FormatError("original selected companion marker omitted from receipt");
 }
 if(capture.companion){const auto& m=*capture.companion;
  std::size_t live=0;for(const auto& e:b.world.entities){if(e.dead||e.ui_ghost||e.kind!=EntityKind::avatar)continue;
   const bool nid=e.person_data.size()>31&&e.person_data[31]==m.target_neighbor;const bool guid=e.definition&&e.definition->guid==m.guid;
   if(nid!=guid)throw FormatError("original selected companion live stable identity conflicts");
   if(nid){++live;if(!seen.count(m.target_neighbor))throw FormatError("original selected live companion effects omitted");}
  }
  if(live>1)throw FormatError("original selected live companion body duplicated");const auto row=b.neighbor_inventory->inventory_by_id.find(m.owner_neighbor);
  if(capture.source_temps[2]!=0||m.type!=(request.destination==-1?3:1)||m.owner_neighbor!=request.departure_members.front().member.neighbor_id||
     row==b.neighbor_inventory->inventory_by_id.end()||m.index>=row->second.size()||!item_matches(row->second[m.index],m))throw FormatError("original sealed marker changed");
  for(std::size_t n=0;n<m.index;++n)if(row->second[n].type==m.type)throw FormatError("original sealed marker is not first");
 }
}
} // namespace odh
