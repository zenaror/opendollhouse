#include "odh/ts1_travel.hpp"
#include "odh/session_bundle.hpp"
#include "odh/original_companion_protocol.hpp"
#include <algorithm>
#include <set>
namespace odh {
namespace {
struct W {
 std::vector<std::uint8_t> b;
 void n(std::uint64_t v,unsigned count){for(unsigned i=0;i<count;++i)b.push_back(static_cast<std::uint8_t>(v>>(i*8)));}
 void bytes(const std::vector<std::uint8_t>& v){if(v.size()>64u*1024u*1024u)throw FormatError("travel extent exceeds limit");n(v.size(),4);b.insert(b.end(),v.begin(),v.end());}
 void text(const std::string& s){bytes({s.begin(),s.end()});}
};
struct R {
 const std::vector<std::uint8_t>& b;std::size_t p=0;
 std::uint64_t n(unsigned c){if(c>b.size()-p)throw FormatError("truncated travel state");std::uint64_t v=0;for(unsigned i=0;i<c;++i)v|=std::uint64_t(b[p++])<<(i*8);return v;}
 std::vector<std::uint8_t> bytes(){auto c=n(4);if(c>b.size()-p||c>64u*1024u*1024u)throw FormatError("invalid travel extent");std::vector<std::uint8_t> v(b.begin()+p,b.begin()+p+c);p+=c;return v;}
 std::string text(){auto v=bytes();if(v.size()>4096)throw FormatError("oversized travel resource path");return {v.begin(),v.end()};}
};
const VmSnapshot* owner_vm(const SessionBundle& b,WorldEntityReference owner){
 const auto live=std::find_if(b.world.entities.begin(),b.world.entities.end(),[&](const auto& e){return e.id==owner.id&&e.generation==owner.generation&&!e.dead;});
 if(live==b.world.entities.end())throw FormatError("travel request owner is not live");
 for(const auto& e:b.runtime_entities)if(e.id==owner.id)return &e.vm;
 for(const auto& e:b.npc_avatars)if(e.origin.id==owner.id&&e.origin.generation==owner.generation)return &e.runtime.vm;
 for(const auto& e:b.object_threads)if(e.id==owner.id&&e.generation==owner.generation)return &e.vm;
 throw FormatError("travel request has no actual owner VM");
}
void request_matches(const Ts1TravelRequest& r,const VmSnapshot& vm){
 if(!r.ticket||!r.thread_owner.generation||r.thread_owner.id<=0||vm.run_generation!=r.run_generation||vm.ticket_sequence!=r.ticket||vm.pending)throw FormatError("travel request does not match deferred VM boundary");
 if(r.source_instruction.opcode!=1||r.source_instruction.operands[0]!=17)throw FormatError("travel source is not Generic17");
 if(vm.stack.empty()){if((r.source_instruction.on_true==254&&vm.status==VmStatus::returned_true)||(r.source_instruction.on_true==255&&vm.status==VmStatus::returned_false))return;throw FormatError("travel terminal continuation differs from source branch");}
 if(vm.status!=VmStatus::next_tick)throw FormatError("travel continuation did not yield at next tick");
 const auto& f=vm.stack.back();
 if(r.source_instruction.opcode!=1||r.source_instruction.operands[0]!=17||!vm_context_equal(f.context,r.source_frame.context)||f.key.id!=r.source_frame.key.id||f.key.scope!=r.source_frame.key.scope||f.invocation_ticket!=r.source_frame.invocation_ticket||f.pc!=r.source_instruction.on_true)
 throw FormatError("travel request continuation changed source ownership or branch");
}
}
bool same_ts1_travel_member(const Ts1TravelMember& a,const Ts1TravelMember& b){return a.guid==b.guid&&a.neighbor_id==b.neighbor_id&&a.definition_chunk_id==b.definition_chunk_id&&a.character_asset==b.character_asset&&a.character_fingerprint==b.character_fingerprint;}
std::vector<Ts1TravelMember> ts1_travel_departure_members(const Ts1TravelState& s){
 if(!s.pending)throw FormatError("travel departure group requires a pending request");
 if(!s.pending->departure_transit)return s.travelers;
 std::vector<Ts1TravelMember> result;result.reserve(s.pending->departure_members.size());
 for(const auto& selected:s.pending->departure_members)result.push_back(selected.member);
 return result;
}
std::int16_t ts1_travel_departure_transit(const Ts1TravelState& s){
 if(!s.pending)throw FormatError("travel departure transit requires a pending request");
 return s.pending->departure_transit.value_or(s.transit_info);
}
const Entity& resolve_ts1_travel_departure_avatar(const SessionBundle& b,const Ts1TravelMember& identity){
 if(!b.travel||!b.travel->pending)throw FormatError("travel departure avatar requires a pending source bundle");
 const auto& s=*b.travel;const auto& request=*s.pending;
 if(std::none_of(s.permanent_roster.begin(),s.permanent_roster.end(),[&](const auto& m){return same_ts1_travel_member(m,identity);}))
  throw FormatError("travel departure resource is outside the permanent family");
 WorldEntityReference ref{};
 if(request.departure_transit){
  std::size_t count=0;
  for(const auto& selected:request.departure_members)if(same_ts1_travel_member(selected.member,identity)){ref=selected.avatar;++count;}
  if(count!=1)throw FormatError("travel departure resource lacks one exact selected body");
 }else{
  std::size_t count=0;
  for(const auto& m:b.household_roster)if(m.guid==identity.guid&&m.neighbor_id==identity.neighbor_id&&
      m.object_definition_id==identity.definition_chunk_id&&m.character_asset==identity.character_asset&&m.character_fingerprint==identity.character_fingerprint){
   ref={m.id,m.generation};++count;
  }
  if(count!=1)throw FormatError("legacy travel departure resource lacks one resident carrier");
 }
 const Entity* body=nullptr;std::size_t bodies=0;
 for(const auto& e:b.world.entities)if(e.id==ref.id&&e.generation==ref.generation){body=&e;++bodies;}
 if(bodies!=1||!body||body->dead||body->ui_ghost||body->kind!=EntityKind::avatar||!body->definition||
    body->definition->guid!=identity.guid||body->person_data.size()<=61||body->person_data[31]!=identity.neighbor_id||
    static_cast<std::uint16_t>(body->person_data[61])!=s.family_chunk_id||body->motives.size()!=16)
  throw FormatError("travel departure body differs from live permanent family identity");
 std::size_t carriers=0;
 for(const auto& m:b.household_roster)if(m.id==ref.id&&m.generation==ref.generation){
  if(m.guid!=identity.guid||m.neighbor_id!=identity.neighbor_id||m.object_definition_id!=identity.definition_chunk_id||
     m.character_asset!=identity.character_asset||m.character_fingerprint!=identity.character_fingerprint)
   throw FormatError("travel departure resident carrier resource mismatch");
  if(std::count_if(b.runtime_entities.begin(),b.runtime_entities.end(),[&](const auto& r){return r.id==ref.id;})!=1)
   throw FormatError("travel departure resident lacks its unique runtime");
  ++carriers;
 }
 for(const auto& npc:b.npc_avatars)if(npc.origin.id==ref.id&&npc.origin.generation==ref.generation){
  const auto& o=npc.origin;
  if(!request.departure_transit||npc.runtime.id!=ref.id||o.guid!=identity.guid||o.neighbor_id!=identity.neighbor_id||
     o.object_definition_id!=identity.definition_chunk_id||o.source!=identity.character_asset||!o.asset.empty()||
     o.character_fingerprint!=identity.character_fingerprint)
   throw FormatError("travel departure visitor carrier resource mismatch");
  ++carriers;
 }
 if(carriers!=1)throw FormatError("travel departure body lacks one authenticated resident or visitor carrier");
 return *body;
}
void validate_ts1_travel_state(const Ts1TravelState& s){
 if(s.protocol!=Ts1TravelProtocol::legacy_pin&&s.protocol!=Ts1TravelProtocol::original_6e598)throw FormatError("travel semantic protocol unsupported");
 const auto zero=[](const auto& a){return std::all_of(a.begin(),a.end(),[](auto v){return v==0;});};
 if(!s.home_mode||s.home_mode==6||s.home_mode>7||!s.current_mode||s.current_mode==6||s.current_mode>7||!s.home_house||s.home_house>98||!s.current_house||s.current_house>98||zero(s.neighborhood_identity)||zero(s.family_identity)||s.permanent_roster.empty()||s.permanent_roster.size()>8||s.travelers.empty()||s.travelers.size()>8)throw FormatError("invalid permanent travel family identity");
 std::set<std::uint32_t> guids;std::set<std::int16_t> nids;
 for(const auto& m:s.permanent_roster)if(!m.guid||m.neighbor_id<=0||m.character_asset.empty()||m.character_asset.size()>4096||zero(m.character_fingerprint)||!guids.insert(m.guid).second||!nids.insert(m.neighbor_id).second)throw FormatError("invalid permanent travel roster");
 guids.clear();bool caller=false;
 for(const auto& m:s.travelers){if(!guids.insert(m.guid).second||std::none_of(s.permanent_roster.begin(),s.permanent_roster.end(),[&](const auto& p){return same_ts1_travel_member(m,p);}))throw FormatError("travel roster differs from permanent family");if(m.guid==s.caller_guid&&m.neighbor_id==s.caller_neighbor_id)caller=true;}
 if(!caller)throw FormatError("originating travel caller absent");
 if(s.pending&&(s.pending->destination!=-1&&(s.pending->destination<1||s.pending->destination>98)))throw FormatError("invalid deferred travel destination");
 if(s.pending){
  const auto& r=*s.pending;
  if((s.protocol==Ts1TravelProtocol::original_6e598)!=r.original.has_value())throw FormatError("mixed original/legacy departure writer profile");
  if(r.original){const auto& capture=*r.original;
   if(!r.departure_transit||(*r.departure_transit!=0&&*r.departure_transit!=1)||capture.source_temps[2]!=*r.departure_transit||
      capture.effects.empty()||capture.effects.size()>9||capture.time_owner<=0||capture.source_hour<0||capture.source_hour>23||capture.source_minute<0||capture.source_minute>59)
    throw FormatError("original departure capture shape invalid");
   if(capture.companion&&(capture.companion->owner_neighbor<=0||capture.companion->target_neighbor<=0||!capture.companion->guid||
      (capture.companion->type!=1&&capture.companion->type!=3)||capture.companion->index>=32767))throw FormatError("invalid original companion marker receipt");
   for(const auto& e:capture.effects)if(e.source.avatar.id<=0||!e.source.avatar.generation||e.source.member.neighbor_id<=0||!e.source.member.guid||e.source.member.character_asset.empty()||e.source.member.character_asset.size()>4096||zero(e.source.member.character_fingerprint))throw FormatError("original source effect identity incomplete");
  }
  if(r.departure_transit.has_value()!=!r.departure_members.empty()||r.departure_members.size()>8)
   throw FormatError("travel departure group and transit must be captured together within family bounds");
  if(r.departure_transit){
   std::set<std::int16_t> ids;guids.clear();nids.clear();
   for(const auto& selected:r.departure_members){const auto& m=selected.member;
    if(selected.avatar.id<=0||!selected.avatar.generation||!ids.insert(selected.avatar.id).second||
       !guids.insert(m.guid).second||!nids.insert(m.neighbor_id).second||
       std::none_of(s.permanent_roster.begin(),s.permanent_roster.end(),[&](const auto& p){return same_ts1_travel_member(m,p);}))
     throw FormatError("travel departure group has duplicate or foreign family identities");
   }
   const auto& first=r.departure_members.front();const auto& source=r.source_frame.context;
   if(!source.entity_references_bound||(s.protocol==Ts1TravelProtocol::legacy_pin&&(first.avatar.id!=source.caller||first.avatar.generation!=source.caller_generation)))
    throw FormatError("travel departure first member is not the bound source caller");
  }
 }
}
void seal_ts1_travel_request(Ts1TravelState& s,const SessionBundle& b){validate_ts1_travel_state(s);if(!s.pending)throw FormatError("no deferred travel request to seal");const auto* vm=owner_vm(b,s.pending->thread_owner);request_matches(*s.pending,*vm);s.pending->continuation=*vm;}
void validate_ts1_travel_bundle(const SessionBundle& b){validate_ts1_travel_bundle(b,SessionBundleLimits{});}
void validate_ts1_travel_bundle(const SessionBundle& b,const SessionBundleLimits& limits){
 if(!b.travel){if(b.travel_visited_session||b.travel_home_session)throw FormatError("travel envelope lacks TRIP");return;}
 const auto& s=*b.travel;validate_ts1_travel_state(s);
 if(!b.neighbor_inventory||b.neighbor_inventory->version!=static_cast<std::uint32_t>(s.protocol))throw FormatError("TRIP semantic protocol differs from NINV producer/consumer profile");
 if(!b.neighbor_inventory||(b.family_lease?(!s.occupancy_epoch||b.family_lease->family_chunk_id!=s.family_chunk_id||b.family_lease->occupancy_epoch!=s.occupancy_epoch||b.family_lease->neighborhood_identity!=s.neighborhood_identity||b.family_lease->family_identity!=s.family_identity):s.occupancy_epoch!=0)||b.host.family_chunk_id!=s.family_chunk_id||b.host.family_number!=s.family_number)throw FormatError("travel state lacks matching inventory or home lease");
 const bool envelope=b.travel_visited_session.has_value();
 if(envelope!=b.travel_home_session.has_value()||b.host.house_number!=(envelope?s.home_house:s.current_house)||b.host.neighborhood_mode!=(envelope?s.home_mode:s.current_mode))throw FormatError("travel host lot differs from explicit home/visited relationship");
 const auto& roster=(envelope||s.current_house==s.home_house)?s.permanent_roster:s.travelers;
 if(b.household_roster.size()!=roster.size()){
  std::string actual,expected;
  for(const auto& m:b.household_roster)actual+=(actual.empty()?"":",")+std::to_string(m.guid)+"/"+std::to_string(m.neighbor_id);
  for(const auto& m:roster)expected+=(expected.empty()?"":",")+std::to_string(m.guid)+"/"+std::to_string(m.neighbor_id);
  throw FormatError("travel runtime roster count mismatch (actual="+std::to_string(b.household_roster.size())+
   " ["+actual+"], expected="+std::to_string(roster.size())+" ["+expected+"], envelope="+(envelope?"true":"false")+
   ", current="+std::to_string(s.current_house)+", home="+std::to_string(s.home_house)+
   ", transit="+std::to_string(s.transit_info)+", caller="+std::to_string(s.caller_guid)+"/"+std::to_string(s.caller_neighbor_id)+
   ", pending="+(s.pending?std::to_string(s.pending->destination):"none")+")");
 }
 for(const auto& m:b.household_roster)if(std::none_of(roster.begin(),roster.end(),[&](const auto& p){return p.guid==m.guid&&p.neighbor_id==m.neighbor_id&&p.definition_chunk_id==m.object_definition_id&&p.character_asset==m.character_asset&&p.character_fingerprint==m.character_fingerprint;}))throw FormatError("travel runtime roster resource mismatch");
 if(envelope){
  const auto reject_nested=[&](const std::vector<std::uint8_t>& bytes){const auto archive=decode_session_archive(bytes,limits.archive);for(const auto& sec:archive.sections)if(sec.id==std::array<char,4>{{'T','V','I','S'}}||sec.id==std::array<char,4>{{'T','H','O','M'}})throw FormatError("nested travel envelope");};
  reject_nested(*b.travel_visited_session);reject_nested(*b.travel_home_session);
  auto active=decode_session_bundle(*b.travel_visited_session,std::nullopt,limits);auto original=decode_session_bundle(*b.travel_home_session,std::nullopt,limits);
  if(s.protocol==Ts1TravelProtocol::original_6e598&&(!original.neighbor_inventory||original.neighbor_inventory->version!=2))throw FormatError("travel suspended HOME semantic profile changed");
  if(!active.travel||encode_ts1_travel_state(*active.travel)!=encode_ts1_travel_state(s)||active.host.house_number==s.home_house||active.host.family_budget!=b.host.family_budget||!active.neighbor_inventory||!b.neighbor_inventory)throw FormatError("travel envelope active state mismatch");
  if(original.family_spells&&!active.family_spells)throw FormatError("travel active state dropped its originating family spell carrier");
  if(original.personal_state&&!active.personal_state)throw FormatError("travel active state dropped personal records");
  if(original.personal_state&&active.personal_state)validate_native_personal_successor(*original.personal_state,*active.personal_state);
  if(original.type_attributes&&!active.type_attributes)throw FormatError("travel active state dropped its originating type attributes");
  if(original.family_spells&&active.family_spells&&
     (original.family_spells->loaded_presence!=active.family_spells->loaded_presence||
      original.family_spells->extension_dword!=active.family_spells->extension_dword))
   throw FormatError("travel family spell carrier changed immutable FAMI source metadata");
  NeighborInventory ai(Ngbh{}),bi(Ngbh{});ai.restore(*active.neighbor_inventory);bi.restore(*b.neighbor_inventory);
  // Compare through the existing canonical bundle codec, preserving unknown
  // inventory item order/duplicates rather than normalizing semantic keys.
  auto normalized=b;normalized.travel.reset();normalized.travel_visited_session.reset();normalized.travel_home_session.reset();normalized.host.family_budget=original.host.family_budget;normalized.neighbor_inventory=original.neighbor_inventory;normalized.family_spells=original.family_spells;normalized.type_attributes=original.type_attributes;normalized.personal_state=original.personal_state;
  auto active_check=active;active_check.travel.reset();active_check.neighbor_inventory=b.neighbor_inventory;active_check.family_spells=b.family_spells;active_check.type_attributes=b.type_attributes;active_check.personal_state=b.personal_state;
  auto active_original=active;active_original.travel.reset();
  original.travel.reset();
  if(original.host.house_number!=s.home_house||original.host.neighborhood_mode!=s.home_mode||encode_session_bundle(normalized,limits)!=encode_session_bundle(original,limits)||encode_session_bundle(active_check,limits)!=encode_session_bundle(active_original,limits))throw FormatError("suspended home bytes or authoritative family/inventory carrier changed");
 }
 if(s.pending&&!envelope){
 const auto& context=s.pending->source_frame.context;
 const auto caller=std::find_if(b.world.entities.begin(),b.world.entities.end(),[&](const auto& e){return e.id==context.caller&&e.generation==context.caller_generation&&!e.dead;});
 const auto modern=s.pending->departure_transit.has_value();
 const auto selected_caller=std::find_if(s.pending->departure_members.begin(),s.pending->departure_members.end(),[&](const auto& m){return m.avatar.id==context.caller&&m.avatar.generation==context.caller_generation;});
 if(modern&&selected_caller==s.pending->departure_members.end())throw FormatError("travel departure caller absent from source group");
 const auto caller_guid=modern?selected_caller->member.guid:s.caller_guid;
 const auto caller_nid=modern?selected_caller->member.neighbor_id:s.caller_neighbor_id;
 if(!context.entity_references_bound||caller==b.world.entities.end()||caller->kind!=EntityKind::avatar||!caller->definition||caller->definition->guid!=caller_guid||caller->person_data.size()<=61||caller->person_data[31]!=caller_nid||static_cast<std::uint16_t>(caller->person_data[61])!=s.family_chunk_id)throw FormatError("travel source caller does not match actual live originating family avatar");
 if(!s.pending->continuation)throw FormatError("unsealed travel request");const auto* vm=owner_vm(b,s.pending->thread_owner);request_matches(*s.pending,*vm);if(encode_vm_snapshot(*vm)!=encode_vm_snapshot(*s.pending->continuation))throw FormatError("travel request continuation differs from actual VM");}
 if(s.pending&&!envelope&&s.protocol==Ts1TravelProtocol::original_6e598){
  validate_original_travel_capture(b);
  const auto& request=*s.pending;
  if(*request.departure_transit==0&&request.departure_members.size()!=1)throw FormatError("original single travel has extra family members");
  if(*request.departure_transit==1){
   for(const auto& member:s.permanent_roster){
    const auto selected=std::find_if(request.departure_members.begin(),request.departure_members.end(),[&](const auto& m){return same_ts1_travel_member(m.member,member);});
    if(selected==request.departure_members.end())throw FormatError("original family departure omits a permanent source member");
    (void)resolve_ts1_travel_departure_avatar(b,member);
   }
  }
 }
 if(s.pending&&!envelope&&s.pending->departure_transit&&s.protocol==Ts1TravelProtocol::legacy_pin){
  const auto& request=*s.pending;
  if(request.continuation->temps[0]!=request.destination)
   throw FormatError("travel departure destination differs from the sealed source Temp0");
  std::optional<std::int16_t> source_transit;
  if(request.destination>=40&&request.destination<50)source_transit=1;
  else if(b.runtime_globals){
   if(b.runtime_globals->words.size()>34)source_transit=b.runtime_globals->words[34];
  }else for(const auto& global:b.host.global_values)if(global.index==34){
   if(source_transit)throw FormatError("travel departure has ambiguous source transit state");
   source_transit=global.value;
  }
  if(!source_transit||*source_transit!=*request.departure_transit)
   throw FormatError("travel departure transit differs from its actual source global");
  const auto& caller=resolve_ts1_travel_departure_avatar(b,request.departure_members.front().member);
  std::vector<WorldEntityReference> expected{{caller.id,caller.generation}};
  if(*source_transit>=1)for(const auto& e:b.world.entities)
   if(e.id!=caller.id&&!e.dead&&!e.ui_ghost&&e.kind==EntityKind::avatar&&e.person_data.size()>61&&
      static_cast<std::uint16_t>(e.person_data[61])==s.family_chunk_id)expected.push_back({e.id,e.generation});
  if(expected.size()!=request.departure_members.size())throw FormatError("travel departure group omits or adds source family avatars");
  NeighborInventory inventory(Ngbh{});inventory.restore(*b.neighbor_inventory);
  constexpr unsigned motives[7]={9,6,5,15,7,8,14};
  for(std::size_t i=0;i<expected.size();++i){
   const auto& selected=request.departure_members[i];
   if(expected[i].id!=selected.avatar.id||expected[i].generation!=selected.avatar.generation)
    throw FormatError("travel departure group order or exact source body changed");
   const auto& body=resolve_ts1_travel_departure_avatar(b,selected.member);
   for(unsigned key=0;key<7;++key){
    const auto effect=inventory.get_data(selected.member.neighbor_id,key);
    if(!effect||*effect!=static_cast<std::uint16_t>(body.motives[motives[key]]))
     throw FormatError("travel departure motive token differs from its source body");
   }
  }
 }
}
std::vector<std::uint8_t> encode_ts1_travel_state(const Ts1TravelState& s){
 validate_ts1_travel_state(s);W w;const bool modern=s.pending&&s.pending->departure_transit.has_value();w.n(s.protocol==Ts1TravelProtocol::original_6e598?3:modern?2:1,2);w.n(s.home_mode,1);w.n(s.current_mode,1);w.n(s.home_house,2);w.n(s.current_house,2);w.n(s.family_chunk_id,2);w.n(static_cast<std::uint32_t>(s.family_number),4);w.n(s.occupancy_epoch,8);for(auto x:s.neighborhood_identity)w.n(x,1);for(auto x:s.family_identity)w.n(x,1);
 const auto members=[&](const auto& list){w.n(list.size(),1);for(const auto& m:list){w.n(m.guid,4);w.n(static_cast<std::uint16_t>(m.neighbor_id),2);w.n(m.definition_chunk_id,2);w.text(m.character_asset);for(auto x:m.character_fingerprint)w.n(x,1);}};
 members(s.permanent_roster);members(s.travelers);w.n(s.caller_guid,4);w.n(static_cast<std::uint16_t>(s.caller_neighbor_id),2);w.n(static_cast<std::uint16_t>(s.transit_info),2);w.n(s.family_activated,1);w.n(s.pending.has_value(),1);
 if(s.pending){const auto& p=*s.pending;w.n(static_cast<std::uint16_t>(p.destination),2);w.n(static_cast<std::uint16_t>(p.thread_owner.id),2);w.n(p.thread_owner.generation,8);w.n(p.run_generation,8);w.n(p.ticket,8);VmSnapshot source;source.stack={p.source_frame};source.action_tree_known=true;w.bytes(encode_vm_snapshot(source));w.n(p.source_instruction.opcode,2);w.n(p.source_instruction.on_true,1);w.n(p.source_instruction.on_false,1);for(auto x:p.source_instruction.operands)w.n(x,1);w.n(p.continuation.has_value(),1);if(p.continuation)w.bytes(encode_vm_snapshot(*p.continuation));
  if(modern){w.n(static_cast<std::uint16_t>(*p.departure_transit),2);w.n(p.departure_members.size(),1);
   for(const auto& selected:p.departure_members){const auto& m=selected.member;
    w.n(m.guid,4);w.n(static_cast<std::uint16_t>(m.neighbor_id),2);w.n(m.definition_chunk_id,2);w.text(m.character_asset);for(auto x:m.character_fingerprint)w.n(x,1);
    w.n(static_cast<std::uint16_t>(selected.avatar.id),2);w.n(selected.avatar.generation,8);
   }
  }
  if(s.protocol==Ts1TravelProtocol::original_6e598){const auto& c=*p.original;
   for(auto v:c.source_temps)w.n(static_cast<std::uint16_t>(v),2);
   for(auto v:{c.source_hour,c.source_minute,c.time_owner,c.time_hour,c.time_minute})w.n(static_cast<std::uint16_t>(v),2);
   w.n(c.source_residential,1);w.n(c.companion.has_value(),1);
   if(c.companion){const auto& m=*c.companion;w.n(static_cast<std::uint16_t>(m.owner_neighbor),2);w.n(static_cast<std::uint16_t>(m.target_neighbor),2);w.n(static_cast<std::uint32_t>(m.type),4);w.n(m.guid,4);w.n(m.index,4);w.n(m.count,2);}
   w.n(c.effects.size(),1);for(const auto& effect:c.effects){const auto& m=effect.source.member;
    w.n(m.guid,4);w.n(static_cast<std::uint16_t>(m.neighbor_id),2);w.n(m.definition_chunk_id,2);w.text(m.character_asset);for(auto v:m.character_fingerprint)w.n(v,1);
    w.n(static_cast<std::uint16_t>(effect.source.avatar.id),2);w.n(effect.source.avatar.generation,8);w.n(effect.companion,1);for(auto v:effect.values)w.n(static_cast<std::uint16_t>(v),2);
   }
  }
 }
 return std::move(w.b);
}
Ts1TravelState decode_ts1_travel_state(const std::vector<std::uint8_t>& bytes){
 R r{bytes};const auto version=r.n(2);if(version!=1&&version!=2&&version!=3)throw FormatError("unsupported travel state version");Ts1TravelState s;s.protocol=version==3?Ts1TravelProtocol::original_6e598:Ts1TravelProtocol::legacy_pin;s.home_mode=r.n(1);s.current_mode=r.n(1);s.home_house=r.n(2);s.current_house=r.n(2);s.family_chunk_id=r.n(2);s.family_number=static_cast<std::int32_t>(r.n(4));s.occupancy_epoch=r.n(8);for(auto& x:s.neighborhood_identity)x=r.n(1);for(auto& x:s.family_identity)x=r.n(1);
 const auto members=[&](auto& list){auto n=r.n(1);if(n>8)throw FormatError("travel roster exceeds limit");for(unsigned i=0;i<n;++i){Ts1TravelMember m;m.guid=r.n(4);m.neighbor_id=static_cast<std::int16_t>(r.n(2));m.definition_chunk_id=r.n(2);m.character_asset=r.text();for(auto& x:m.character_fingerprint)x=r.n(1);list.push_back(std::move(m));}};
 members(s.permanent_roster);members(s.travelers);s.caller_guid=r.n(4);s.caller_neighbor_id=static_cast<std::int16_t>(r.n(2));s.transit_info=static_cast<std::int16_t>(r.n(2));auto activated=r.n(1);if(activated>1)throw FormatError("invalid travel activation flag");s.family_activated=activated!=0;auto flag=r.n(1);if(flag>1)throw FormatError("invalid travel request flag");
 if(flag){Ts1TravelRequest p;p.destination=static_cast<std::int16_t>(r.n(2));p.thread_owner.id=static_cast<std::int16_t>(r.n(2));p.thread_owner.generation=r.n(8);p.run_generation=r.n(8);p.ticket=r.n(8);auto source=decode_vm_snapshot(r.bytes());if(source.stack.size()!=1)throw FormatError("invalid travel source frame extent");p.source_frame=source.stack.front();p.source_instruction.opcode=r.n(2);p.source_instruction.on_true=r.n(1);p.source_instruction.on_false=r.n(1);for(auto& x:p.source_instruction.operands)x=r.n(1);auto seal=r.n(1);if(seal>1)throw FormatError("invalid travel seal flag");if(seal)p.continuation=decode_vm_snapshot(r.bytes());
  if(version>=2){p.departure_transit=static_cast<std::int16_t>(r.n(2));const auto count=r.n(1);
   if(!count||count>8)throw FormatError("travel departure group exceeds family bounds");
   for(unsigned i=0;i<count;++i){Ts1TravelDepartureMember selected;auto& m=selected.member;
    m.guid=r.n(4);m.neighbor_id=static_cast<std::int16_t>(r.n(2));m.definition_chunk_id=r.n(2);m.character_asset=r.text();for(auto& x:m.character_fingerprint)x=r.n(1);
    selected.avatar.id=static_cast<std::int16_t>(r.n(2));selected.avatar.generation=r.n(8);p.departure_members.push_back(std::move(selected));
   }
  }
  if(version==3){OriginalTravelCapture c;
   for(auto& v:c.source_temps)v=static_cast<std::int16_t>(r.n(2));
   c.source_hour=static_cast<std::int16_t>(r.n(2));c.source_minute=static_cast<std::int16_t>(r.n(2));c.time_owner=static_cast<std::int16_t>(r.n(2));c.time_hour=static_cast<std::int16_t>(r.n(2));c.time_minute=static_cast<std::int16_t>(r.n(2));
   const auto residential=r.n(1),marker=r.n(1);if(residential>1||marker>1)throw FormatError("original capture flag invalid");c.source_residential=residential!=0;
   if(marker){OriginalCompanionMarker m;m.owner_neighbor=static_cast<std::int16_t>(r.n(2));m.target_neighbor=static_cast<std::int16_t>(r.n(2));m.type=static_cast<std::int32_t>(r.n(4));m.guid=static_cast<std::uint32_t>(r.n(4));m.index=static_cast<std::uint32_t>(r.n(4));m.count=static_cast<std::uint16_t>(r.n(2));c.companion=m;}
   const auto count=r.n(1);if(!count||count>9)throw FormatError("original capture effect count exceeds bound");
   for(std::uint64_t i=0;i<count;++i){OriginalTravelEffectReceipt e;auto& m=e.source.member;
    m.guid=static_cast<std::uint32_t>(r.n(4));m.neighbor_id=static_cast<std::int16_t>(r.n(2));m.definition_chunk_id=static_cast<std::uint16_t>(r.n(2));m.character_asset=r.text();for(auto& v:m.character_fingerprint)v=static_cast<std::uint8_t>(r.n(1));
    e.source.avatar.id=static_cast<std::int16_t>(r.n(2));e.source.avatar.generation=r.n(8);const auto companion=r.n(1);if(companion>1)throw FormatError("invalid original effect companion flag");e.companion=companion!=0;for(auto& v:e.values)v=static_cast<std::int16_t>(r.n(2));c.effects.push_back(std::move(e));
   }
   p.original=std::move(c);
  }
  s.pending=std::move(p);
 }
 if(version==2&&!s.pending)throw FormatError("modern travel state requires a pending departure group");
 if(r.p!=bytes.size())throw FormatError("trailing travel state bytes");validate_ts1_travel_state(s);return s;
}
}
