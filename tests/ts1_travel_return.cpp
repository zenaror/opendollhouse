#include "travel_session_fixture.hpp"
#include "odh/ts1_travel_return.hpp"
#include "odh/sha256.hpp"
#include <iostream>
#include <algorithm>
#include <sstream>
using travel_test::require;
template<class F>void rejects(F f,const char* why){try{f();}catch(const odh::FormatError&){return;}throw std::runtime_error(why);}
namespace {
odh::Nbrs inheritance_neighbors(){
 odh::Nbrs neighbors{};neighbors.version=62;
 for(std::int16_t id:{41,42,43}){
  odh::NeighborRecord record{};record.version=4;record.name="Synthetic";record.person_mode=5;
  record.person_data=std::vector<std::int16_t>(80,0);record.neighbor_id=id;record.guid=static_cast<std::uint32_t>(123415+id);
  record.unknown_neg_one=-1;neighbors.entries.push_back({1,std::move(record)});
 }
 return neighbors;
}
std::string inheritance_iff(const odh::Nbrs& neighbors,bool duplicate=false){
 const auto payload=odh::encode_nbrs(neighbors);
 std::string bytes="IFF FILE 2.5:TYPE FOLLOWED BY SIZE JAMIE DOORNBOS & MAXIS 1";bytes.resize(64,'\0');
 const auto chunk=[&]{
  bytes+="NBRS";const auto n=static_cast<std::uint32_t>(76+payload.size());
  for(int shift:{24,16,8,0})bytes.push_back(static_cast<char>(n>>shift));
  bytes.append("\0\1\0\0",4);bytes.append(64,'\0');bytes.append(reinterpret_cast<const char*>(payload.data()),payload.size());
 };
 chunk();if(duplicate)chunk();return bytes;
}
odh::Ts1TravelPersonInheritanceSource inheritance_source(const std::string& bytes){
 std::istringstream input(bytes,std::ios::binary);return odh::read_ts1_travel_person_inheritance(odh::View(input,bytes.size()));
}
void bind_inheritance_identity(odh::SessionBundle& envelope,const odh::Ts1TravelPersonInheritanceSource& source){
 auto home=odh::decode_session_bundle(*envelope.travel_home_session);
 auto active=odh::decode_session_bundle(*envelope.travel_visited_session);
 home.family_lease->neighborhood_identity=source.neighborhood_identity();
 active.family_lease->neighborhood_identity=source.neighborhood_identity();active.travel->neighborhood_identity=source.neighborhood_identity();
 envelope.family_lease->neighborhood_identity=source.neighborhood_identity();envelope.travel=active.travel;
 envelope.travel_home_session=odh::encode_session_bundle(home);envelope.travel_visited_session=odh::encode_session_bundle(active);
}
odh::SessionBundle return_envelope(bool personal_change=false,bool runtime_change=false,bool fame_change=false){
 auto home=travel_test::managed_home();
 home.relationships.sources={{odh::RelationshipMatrix::neighbor,41,0,{{44,{1}}}},{odh::RelationshipMatrix::object,1,home.world.entities.front().generation,{{1,{9}}}}};home.relationships.pair_count=2;home.relationships.value_count=2;
 odh::World world;require(world.create_saved_entity(9,odh::EntityKind::avatar,123456,{},std::vector<std::int16_t>(fame_change?88:80),std::vector<std::int16_t>(16),{},{},0),"create visited caller with foreign World ID");require(world.set_person_data(9,31,41)&&world.set_person_data(9,61,21),"bind visited caller stable NID");if(personal_change)require(world.set_person_data(9,10,150),"actual skill delta retained for original-rules resolution");
 if(runtime_change)for(const auto index:{27,33,37,41,42,43,44,45,62,71})require(world.set_person_data(9,static_cast<std::int16_t>(index),static_cast<std::int16_t>(index+1)),"actual lot-local counter/queue/route/head-seek difference");
 if(fame_change)require(world.set_person_data(9,80,500),"actual TS1 fame score progress");
 constexpr unsigned motive_keys[7]={9,6,5,15,7,8,14};for(unsigned i=0;i<7;++i)require(world.set_motive(9,motive_keys[i],static_cast<std::int16_t>(-70+int(i))),"set actual visited needs");
 auto state=travel_test::travel_state(home);state.current_house=23;state.current_mode=2;state.family_activated=true;
 odh::NeighborInventory inventory(odh::Ngbh{});inventory.set_data(41,7,22);inventory.set_data(41,8,43);inventory.set_data(41,10,44);
 odh::Bhav program{0x8002,{{1,1,255,{17,0,0,0,0,0,0,0}},{0,254,255,{}}},{},odh::Bhav::Metadata{0,4,4,0}};
 odh::Vm vm({{odh::routine_key(4096),program}},world.vm_memory(),{},[&](const auto& frame,const auto& instruction,const auto&,const auto& dispatch){
  odh::Ts1TravelRequest request;request.destination=-1;request.thread_owner={9,world.find(9)->generation};request.run_generation=dispatch.origin->run_generation();request.ticket=dispatch.ticket;request.source_frame=frame;request.source_instruction=instruction;state.pending=request;
  for(unsigned key=0;key<7;++key)inventory.set_data(41,key,static_cast<std::uint16_t>(world.find(9)->motives[motive_keys[key]]));
  return odh::PrimitiveOutcome{odh::PrimitiveDisposition::goto_true_next_tick,{}};
 });
 require(vm.start(odh::routine_key(4096),{9,9,9,0})==odh::VmStatus::running&&vm.run(1)==odh::VmStatus::next_tick,"real Generic17 return request captures needs and defers");
 odh::EntityThreadControllers controllers(world,9);require(controllers.bind(vm),"bind visited actual generation");odh::EntityVmScheduler scheduler(4);require(scheduler.add(9,vm,[](auto){return true;},[]{return true;},[]{return true;}),"schedule visited actual VM");
 auto active=home;active.host.house_number=23;active.host.neighborhood_mode=2;active.host.runtime_avatar_id=9;active.host.family_budget=6765;active.host.random_seed=98765;active.world=world.snapshot();active.household_roster.front().id=9;active.household_roster.front().generation=world.find(9)->generation;
 odh::SessionRuntimeEntity runtime;runtime.id=9;runtime.vm=vm.snapshot();runtime.controllers=controllers.snapshot();active.runtime_entities={runtime};active.scheduler=scheduler.snapshot();active.simulation->avatars.front().id=9;active.simulation->avatars.front().decay.fractions.fill(99);
 active.relationships.sources={{odh::RelationshipMatrix::neighbor,41,0,{{44,{25}}}},{odh::RelationshipMatrix::neighbor,44,0,{{41,{67}}}},{odh::RelationshipMatrix::object,9,world.find(9)->generation,{{9,{88}}}}};active.relationships.pair_count=3;active.relationships.value_count=3;
 active.neighbor_inventory=inventory.snapshot();active.travel=state;odh::seal_ts1_travel_request(*active.travel,active);
 auto envelope=home;envelope.travel=active.travel;envelope.neighbor_inventory=active.neighbor_inventory;envelope.host.family_budget=active.host.family_budget;envelope.travel_home_session=odh::encode_session_bundle(home);envelope.travel_visited_session=odh::encode_session_bundle(active);(void)odh::encode_session_bundle(envelope);return envelope;
}
}
int main(){try{
 const auto envelope=return_envelope();const auto original=odh::decode_session_bundle(*envelope.travel_home_session);const auto before=odh::encode_session_bundle(envelope);
 const auto returned=odh::prepare_ts1_travel_return(envelope);const auto& home=returned.home();require(!returned.requires_original_personal_persistence_resolution(),"unchanged personal bank needs no invented field rule");
 require(returned.source_checkpoint_sha256()==odh::sha256(before.data(),before.size())&&returned.suspended_home_sha256()==odh::sha256(envelope.travel_home_session->data(),envelope.travel_home_session->size()),"return receipt authenticates exact checkpoint and suspended home");
 require(odh::encode_world_snapshot(home.world)==odh::encode_world_snapshot(original.world)&&odh::encode_vm_snapshot(home.runtime_entities.front().vm)==odh::encode_vm_snapshot(original.runtime_entities.front().vm)&&home.host.random_seed==original.host.random_seed&&home.host.tick==original.host.tick,"foreign World IDs, VM continuation and random/tick state never replace home");
 require(home.simulation->avatars.front().decay.fractions==original.simulation->avatars.front().decay.fractions&&home.simulation->clock.ticks==original.simulation->clock.ticks&&home.simulation->clock.minute==original.simulation->clock.minute,"home clock and fractional decay remain source SavedLot values");
 require(home.host.house_number==7&&home.household_roster.front().id==1&&home.host.family_budget==6765&&!home.travel,"return preserves permanent residence and imports actual shared family budget once");
 odh::NeighborInventory inventory(odh::Ngbh{});inventory.restore(*home.neighbor_inventory);require(inventory.get_data(41,0)==static_cast<std::uint16_t>(-70)&&inventory.get_data(41,7)==22&&inventory.get_data(41,10)==44,"authored Generic21 effects and time/companion tokens remain unconsumed");
 require(home.relationships.sources.size()==3&&home.relationships.sources[0].pairs.front().values.front()==25&&home.relationships.sources[1].id==44&&home.relationships.sources[2].matrix==odh::RelationshipMatrix::object&&home.relationships.sources[2].id==1&&home.relationships.sources[2].pairs.front().values.front()==9,"shared NID relations persist while foreign object relations never enter home");
 require(odh::encode_session_bundle(envelope)==before,"pure return preparation does not mutate durable recovery state");
 const auto changed=return_envelope(true);const auto unresolved=odh::prepare_ts1_travel_return(changed);require(unresolved.requires_original_personal_persistence_resolution()&&unresolved.unresolved_personal_deltas().size()==1,"skill progress is retained as authenticated unresolved delta");const auto& delta=unresolved.unresolved_personal_deltas().front();require(delta.member.guid==123456&&delta.member.neighbor_id==41&&delta.index==10&&delta.home_value==0&&delta.visited_value==150,"personal delta records exact stable identity/index/old/new");rejects([&]{(void)unresolved.home();},"unresolved original persistence cannot be silently published");
 const auto transient=odh::prepare_ts1_travel_return(return_envelope(false,true));require(!transient.requires_original_personal_persistence_resolution()&&transient.retained_runtime_deltas().size()==10,"proven lot-local differences are diagnostic rather than blocking persistence");require(odh::encode_world_snapshot(transient.home().world)==odh::encode_world_snapshot(original.world),"gameplay tick/priority/route/head-seek differences preserve exact home-owned values");
 const auto fame=odh::prepare_ts1_travel_return(return_envelope(false,true,true));require(fame.requires_original_personal_persistence_resolution()&&fame.retained_runtime_deltas().size()==10,"runtime classification cannot conceal TS1 fame progression");require(std::any_of(fame.unresolved_personal_deltas().begin(),fame.unresolved_personal_deltas().end(),[](const auto& value){return value.index==80&&value.visited_value==500;}),"TS1 fame alias remains authenticated unresolved personal delta");rejects([&]{(void)fame.home();},"fame change cannot silently disappear through transient classification");
 auto stale=envelope;stale.travel->pending->ticket++;rejects([&]{odh::prepare_ts1_travel_return(stale);},"stale sealed VM return rejected");
 auto bad=envelope;auto active=odh::decode_session_bundle(*bad.travel_visited_session);active.neighbor_inventory->inventory_by_id[41].front().count=23; // Time only remains independent from motive tokens.
 active.neighbor_inventory->inventory_by_id[41].back().count++;bad.neighbor_inventory=active.neighbor_inventory;bad.travel_visited_session=odh::encode_session_bundle(active);rejects([&]{odh::prepare_ts1_travel_return(bad);},"motive effect substitution rejected before original home changes");
 rejects([&]{odh::prepare_ts1_travel_return(home);},"completed home cannot replay the old return projection");
 {
  auto spells_envelope=return_envelope();
  auto source_home=odh::decode_session_bundle(*spells_envelope.travel_home_session);
  auto visited=odh::decode_session_bundle(*spells_envelope.travel_visited_session);
  odh::FamilySpellsSnapshot source_spells;source_spells.family_chunk_id=source_home.host.family_chunk_id;
  source_spells.family_number=source_home.host.family_number;source_spells.loaded_presence=true;source_spells.extension_dword=0xaabbccddU;
  source_spells.words={-32768,2,-3,100,-5,7};source_home.family_spells=source_spells;source_home.type_attributes=odh::TypeAttributesSnapshot{};source_home.type_attributes->by_guid[777]={2,-3};
  source_home.host.global_values={{9,21},{10,7}};
  odh::World old_world;require(old_world.restore_snapshot(source_home.world)&&old_world.create(4,odh::EntityKind::object,{6})&&
      old_world.define_type(4,0xb6c90029U),"restore real HOME and its spell controller metadata");
  odh::VmFrame controller;controller.context.stack_object=4;
  for(std::size_t i=0;i<6;++i)require(old_world.write(controller,1,static_cast<std::int16_t>(i),source_spells.words[i]),"set old HOME controller spell attributes");
  source_home.world=old_world.snapshot();
  visited.type_attributes=source_home.type_attributes;visited.type_attributes->by_guid[777][1]=91;
  visited.family_spells=source_spells;visited.family_spells->words={5,1,-2,80,-4,9};
  spells_envelope=source_home;spells_envelope.travel=visited.travel;spells_envelope.neighbor_inventory=visited.neighbor_inventory;
  spells_envelope.type_attributes=visited.type_attributes;
  spells_envelope.host.family_budget=visited.host.family_budget;spells_envelope.family_spells=visited.family_spells;
  spells_envelope.travel_home_session=odh::encode_session_bundle(source_home);spells_envelope.travel_visited_session=odh::encode_session_bundle(visited);
  const auto receipt=odh::prepare_ts1_travel_return(spells_envelope);auto published_home=receipt.home();
  const std::array<std::int16_t,6> expected{5,2,-2,100,-4,9};
  require(published_home.family_spells&&published_home.family_spells->words==expected,
      "typed return merges every spell word with signed max rather than reverting HOME or visited progress");
  require(published_home.type_attributes&&published_home.type_attributes->by_guid.at(777)[1]==91,"return imports shared type attributes from the active destination");
  odh::Family resource_family;resource_family.version=9;resource_family.family_number=published_home.host.family_number;
  resource_family.remaining_bytes=16;resource_family.tail_bytes.resize(16);
  resource_family.original_v9_extension=odh::FamilyV9Extension{source_spells.extension_dword,source_spells.words};
  for(unsigned n=0;n<4;++n)resource_family.tail_bytes[n]=static_cast<std::uint8_t>(source_spells.extension_dword>>(8*n));
  for(std::size_t n=0;n<6;++n){const auto raw=static_cast<std::uint16_t>(source_spells.words[n]);
      resource_family.tail_bytes[4+2*n]=static_cast<std::uint8_t>(raw);resource_family.tail_bytes[5+2*n]=static_cast<std::uint8_t>(raw>>8);}
  odh::FamilySpells carrier(odh::FamilyEntry{published_home.host.family_chunk_id,resource_family});carrier.restore(*published_home.family_spells);
  odh::World restored;require(restored.restore_snapshot(published_home.world),"prepare restored HOME graph before first tick");
  const odh::LotZoning rows{{7,0}};
  const auto globals=[&](std::int16_t index)->std::optional<std::int16_t>{
      const auto found=std::find_if(published_home.host.global_values.begin(),published_home.host.global_values.end(),
          [&](const auto& entry){return entry.index==index;});
      return found==published_home.host.global_values.end()?std::nullopt:std::optional<std::int16_t>{found->value};};
  const auto hydrated=odh::apply_family_spells(restored,carrier,&rows,globals);
  require(hydrated.supported&&hydrated.changed,"typed-return durable hydration uses guarded source43 operation without rerunning Init/VM");
  const auto captured=odh::capture_family_spells(restored,carrier,&rows,globals);require(captured.supported,"mandatory reverse capture after HOME hydration");
  published_home.world=restored.snapshot();published_home.family_spells=carrier.snapshot();
  const auto durable=odh::decode_session_bundle(odh::encode_session_bundle(published_home));
  const auto found=std::find_if(durable.world.entities.begin(),durable.world.entities.end(),[](const auto& entity){return entity.id==4;});
  require(found!=durable.world.entities.end()&&found->attributes==std::vector<std::int16_t>(expected.begin(),expected.end())&&
      durable.family_spells->words==expected&&durable.family_spells->extension_dword==source_spells.extension_dword&&
      durable.host.tick==receipt.home().host.tick,
      "published/reopened HOME contains synchronized controller attributes and exact carrier before any tick");
  auto dropped=visited;dropped.family_spells.reset();auto drop_envelope=spells_envelope;drop_envelope.travel_visited_session=odh::encode_session_bundle(dropped);
  rejects([&]{odh::prepare_ts1_travel_return(drop_envelope);},"return rejects a visited snapshot that drops originating family spell authority");
 }
 {
  const auto party=travel_test::departure_party_envelope();const auto durable=odh::encode_session_bundle(party);
  const auto original_home=odh::decode_session_bundle(*party.travel_home_session);
  const auto source=odh::decode_session_bundle(*party.travel_visited_session);
  const auto prepared=odh::prepare_ts1_travel_return(party);const auto& projected=prepared.home();
  require(!prepared.requires_original_personal_persistence_resolution()&&projected.household_roster.size()==3&&
      projected.runtime_entities.size()==3&&projected.npc_avatars.empty()&&!projected.travel,
      "one admitted Sim and genuine visiting relative return without importing visitor execution or changing HOME roster");
  require(odh::encode_world_snapshot(projected.world)==odh::encode_world_snapshot(original_home.world)&&
      projected.host.tick==original_home.host.tick&&projected.host.random_seed==original_home.host.random_seed&&
      projected.simulation->clock.ticks==original_home.simulation->clock.ticks&&
      projected.host.family_budget==source.host.family_budget,
      "two-person return keeps HOME bodies, clock, RNG and shared family budget semantics");
  for(std::size_t i=0;i<original_home.runtime_entities.size();++i)
   require(odh::encode_vm_snapshot(projected.runtime_entities[i].vm)==odh::encode_vm_snapshot(original_home.runtime_entities[i].vm),
       "visiting relative VM never replaces its permanent HOME thread");
  odh::NeighborInventory imported(odh::Ngbh{});imported.restore(*projected.neighbor_inventory);
  require(imported.get_data(41,0)==static_cast<std::uint16_t>(-71)&&imported.get_data(42,0)==static_cast<std::uint16_t>(-70)&&
      !imported.get_data(43,0),"both actual source travelers retain motive effects; absent third family member gains no invented transfer");
  require(odh::encode_session_bundle(odh::decode_session_bundle(odh::encode_session_bundle(projected)))==odh::encode_session_bundle(projected)&&
      odh::encode_session_bundle(party)==durable,"two-person HOME result reopens exactly without mutating durable source checkpoint");
  auto changed=source;changed.world.entities.back().person_data[10]=150;
  auto changed_envelope=party;changed_envelope.travel_visited_session=odh::encode_session_bundle(changed);
  const auto unresolved=odh::prepare_ts1_travel_return(changed_envelope);
  require(unresolved.requires_original_personal_persistence_resolution()&&unresolved.unresolved_personal_deltas().size()==1&&
      unresolved.unresolved_personal_deltas().front().member.neighbor_id==42&&unresolved.unresolved_personal_deltas().front().index==10,
      "visitor personal progress remains authenticated unresolved work rather than silently discarded");
  rejects([&]{(void)unresolved.home();},"unresolved visiting-relative skill progress still blocks publication");
 }
 {
  auto neighbors=inheritance_neighbors();const auto raw=inheritance_iff(neighbors);const auto source=inheritance_source(raw);
  require(source.neighborhood_identity()==odh::sha256(raw.data(),raw.size()),"sealed inheritance digest derives from exact parsed bytes");
  auto party=travel_test::departure_party_envelope();bind_inheritance_identity(party,source);
  auto home=odh::decode_session_bundle(*party.travel_home_session);
  home.world.entities[1].person_data[15]=178;home.world.entities[1].person_data[46]=7;
  home.world.entities[1].person_data[69]=15;home.world.entities[1].person_data[70]=4;
  home.world.entities[1].person_data[80]=500; // Proven zero extension of PRESENT old80-word NBRS bank.
  party.world=home.world;party.travel_home_session=odh::encode_session_bundle(home);
  const auto original_world=odh::encode_world_snapshot(home.world);
  const auto strict=odh::prepare_ts1_travel_return(party);
  require(strict.unresolved_personal_deltas().size()==5,"omitting inheritance source preserves strict personal-change policy");
  const auto retained=odh::prepare_ts1_travel_return(party,{},&source);
  require(!retained.requires_original_personal_persistence_resolution()&&retained.unchanged_inherited_personal_deltas().size()==5&&
      retained.retained_runtime_deltas().empty()&&odh::encode_world_snapshot(retained.home().world)==original_world,
      "unchanged inherited skill, interests, career, zodiac and old-bank extension preserve prior HOME progress in a separate category");
  const auto visited=odh::decode_session_bundle(*party.travel_visited_session);
  for(const auto index:{15,46,69,70,80}){
   auto changed=visited;changed.world.entities.back().person_data[static_cast<std::size_t>(index)]=50;
   auto new_party=party;new_party.travel_visited_session=odh::encode_session_bundle(changed);
   const auto progress=odh::prepare_ts1_travel_return(new_party,{},&source);
   require(progress.unresolved_personal_deltas().size()==1&&progress.unresolved_personal_deltas().front().index==static_cast<std::size_t>(index),
       "genuine skill, interest, career, zodiac and fame changes stay unresolved against authenticated initial state");
   rejects([&]{(void)progress.home();},"genuine visited progress cannot be silently discarded");
  }
  // The reference constructor retains/rebinds these fields, so raw NBRS must
  // never authorize their equality. The visit-local32/34 contract is separate.
  for(const auto index:{35,65}){
   auto changed_home=home;changed_home.world.entities[1].person_data[static_cast<std::size_t>(index)]=50;
   auto new_party=party;new_party.world=changed_home.world;new_party.travel_home_session=odh::encode_session_bundle(changed_home);
   const auto unknown=odh::prepare_ts1_travel_return(new_party,{},&source);
   require(unknown.unresolved_personal_deltas().size()==1&&unknown.unresolved_personal_deltas().front().index==static_cast<std::size_t>(index),
       "constructor-preserved schedule/gender are not raw inherited values");
  }
  auto roles=visited;roles.world.entities.back().person_data[32]=1;roles.world.entities.back().person_data[34]=1;
  auto role_party=party;role_party.travel_visited_session=odh::encode_session_bundle(roles);
  const auto local=odh::prepare_ts1_travel_return(role_party,{},&source);
  require(local.retained_runtime_deltas().size()==2&&local.unchanged_inherited_personal_deltas().size()==5&&
      !local.requires_original_personal_persistence_resolution(),"source visitor role and greeting use explicit local contract, not raw NBRS equality");
  auto scratch=visited;scratch.world.entities.back().person_data[25]=1;auto scratch_party=party;
  scratch_party.travel_visited_session=odh::encode_session_bundle(scratch);
  require(odh::prepare_ts1_travel_return(scratch_party,{},&source).unresolved_personal_deltas().size()==1,
      "unproved authored scratch bits remain unresolved");
  for(const std::int16_t old_flag:{0,64,1,65}){
   auto flag_home=home;flag_home.world.entities[1].person_data[25]=old_flag;
   auto flag_visit=visited;flag_visit.world.entities.back().person_data[25]=static_cast<std::int16_t>(old_flag^64);
   auto flag_party=party;flag_party.world=flag_home.world;flag_party.travel_home_session=odh::encode_session_bundle(flag_home);
   flag_party.travel_visited_session=odh::encode_session_bundle(flag_visit);
   const auto flag_result=odh::prepare_ts1_travel_return(flag_party,{},&source);
   require(flag_result.retained_runtime_deltas().size()==1&&flag_result.retained_runtime_deltas().front().index==25&&
       flag_result.home().world.entities[1].person_data[25]==old_flag,
       "only TreeUseDataField bit0x40 toggles are retained as local action state, preserving other HOME bits");
  }
  const auto reject_source=[&](odh::Nbrs bad,const char* why){rejects([&]{
   const auto bad_source=inheritance_source(inheritance_iff(bad));auto bad_party=party;bind_inheritance_identity(bad_party,bad_source);
   (void)odh::prepare_ts1_travel_return(bad_party,{},&bad_source);},why);};
  auto wrong=neighbors;wrong.entries[1].record->guid++;reject_source(wrong,"mismatched neighbor GUID cannot supply initial values");
  wrong=neighbors;wrong.entries[1].record->neighbor_id=44;reject_source(wrong,"missing requested neighbor ID cannot supply initial values");
  wrong=neighbors;wrong.entries.push_back(wrong.entries[1]);reject_source(wrong,"ambiguous duplicate neighbor identity rejected");
  auto repeated_guid=neighbors;auto alias=repeated_guid.entries[1];alias.record->neighbor_id=44;(*alias.record->person_data)[15]=99;repeated_guid.entries.push_back(alias);
  const auto alias_source=inheritance_source(inheritance_iff(repeated_guid));auto alias_party=party;bind_inheritance_identity(alias_party,alias_source);
  require(!odh::prepare_ts1_travel_return(alias_party,{},&alias_source).requires_original_personal_persistence_resolution(),
      "distinct neighbor IDs may share a GUID without changing the actual requested NID inheritance");
  wrong=neighbors;wrong.entries[1].record->person_mode=0;wrong.entries[1].record->person_data.reset();
  const auto missing=inheritance_source(inheritance_iff(wrong));auto missing_party=party;bind_inheritance_identity(missing_party,missing);
  require(odh::prepare_ts1_travel_return(missing_party,{},&missing).unresolved_personal_deltas().size()==5,
      "missing neighbor person bank remains unknown rather than a zero inheritance");
  auto wrong_bytes=raw;wrong_bytes[60]=1;const auto other_source=inheritance_source(wrong_bytes);
  rejects([&]{odh::prepare_ts1_travel_return(party,{},&other_source);},"valid parsed source with a different full IFF fingerprint rejected");
  rejects([&]{inheritance_source(inheritance_iff(neighbors,true));},"duplicate NBRS1 source rejected");
  auto malformed=raw;malformed.pop_back();rejects([&]{inheritance_source(malformed);},"truncated neighbor source rejected");
  malformed=raw;malformed[64]='X';rejects([&]{inheritance_source(malformed);},"source without NBRS1 rejected");
  malformed=raw;malformed[64+76+4]=99;rejects([&]{inheritance_source(malformed);},"unsupported NBRS source layout rejected");
  std::istringstream oversized_input;const odh::View oversized(oversized_input,64u*1024u*1024u+1u);
  rejects([&]{odh::read_ts1_travel_person_inheritance(oversized);},"oversized source rejected before allocation or stream read");
 }
 std::cout<<"TS1 travel return projection tests passed\n";return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
