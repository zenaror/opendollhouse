// SPDX-License-Identifier: MPL-2.0
#include "odh/ts1_character_family.hpp"
#include "odh/neighbor_context.hpp"
#include "odh/family_overlay.hpp"
#include "odh/definition_index.hpp"
#include <iostream>
#include <stdexcept>
namespace {using namespace odh;
unsigned checks=0;
void require(bool v,const char* why){++checks;if(!v)throw std::runtime_error(why);}
template<class F>void rejects(F f,const char* why){bool caught=false;try{f();}catch(const FormatError&){caught=true;}require(caught,why);}
std::array<std::uint8_t,32> digest(unsigned n){std::array<std::uint8_t,32> v{};v[0]=static_cast<std::uint8_t>(n);return v;}
NativePersonalIdentity identity(std::int16_t nid,std::uint32_t guid,bool owned=false){return {nid,guid,128,(owned?"PlayerData/Characters/":"UserData/Characters/")+std::string("User")+std::to_string(nid)+".iff",digest(static_cast<unsigned>(nid))};}
NeighborRecord neighbor(std::int16_t nid,std::uint32_t guid,std::int16_t family){std::vector<std::int16_t> words(88);words[61]=family;words[58]=27;return make_simitone_character_neighbor(nid,guid,words);}
FamilyEntry family(std::uint16_t id,std::vector<std::uint32_t> members){Family f{};f.version=9;f.family_number=id+100;f.house_number=id==10?1:0;f.budget=id==10?1000:200;f.value_in_arch=id==10?100:50;f.unknown=24;f.member_guids=std::move(members);return {id,f,"family"};}
Ts1CreatedCharacter character(){return {identity(2,200,true),make_original_character_neighbor(2,200,"User2.iff")};}
struct Fixture {
 std::array<std::uint8_t,32> neighborhood=digest(99);
 Nbrs seed{0,73,{{1,neighbor(1,100,10)},{0,{}},{1,neighbor(3,300,11)}},0};
 NativePersonalState personal{seed,neighborhood};NeighborContext neighbors{seed};
 std::vector<FamilyEntry> source{family(10,{100}),family(11,{300})};World world;
 std::unique_ptr<Ts1CharacterFamilyState> state;Ts1FamilyConsumers consumers;
 std::vector<std::string> order;bool fail_runtime=false;std::vector<std::uint32_t> runtime_subset{100};
 Fixture(){
  require(world.create(1,EntityKind::avatar,{0,88})&&world.create(3,EntityKind::avatar,{0,88})&&world.set_person_data(1,61,10)&&world.set_person_data(3,61,11),"real live avatar family banks");
  neighbors.bind_personal_state(&personal);
  Ts1CharacterFamilySnapshot empty;empty.neighborhood_identity=neighborhood;
  state=std::make_unique<Ts1CharacterFamilyState>(empty,source,std::map<std::uint16_t,std::array<std::uint8_t,32>>{{10,family_overlay_identity(source[0])},{11,family_overlay_identity(source[1])}});
  consumers.neighbor_family=[&](auto id){const auto read=neighbors.person_data(id,61);return read?std::optional<std::int16_t>{read->value}:std::nullopt;};
  consumers.set_neighbor_family=[&](auto id,auto value,bool clear,auto&){order.push_back("neighbor");personal.set_family(identity(id,id==1?100:300),value,clear);return true;};
  consumers.set_runtime_family=[&](auto id,auto value,bool clear,auto& why){order.push_back("runtime");if(fail_runtime){why="injected genuine consumer failure";return false;}return world.set_person_data(id,61,value)&&(!clear||world.set_person_data(id,67,0));};
  consumers.publish_family=[&](const auto& f,bool visible,auto&){order.push_back(std::to_string(f.chunk_id)+(visible?"visible":"removed"));return true;};
  consumers.append_runtime_subset=[&](auto target,auto guid,auto&){order.push_back("subset");if(target==10)admit_ts1_runtime_subset_member(runtime_subset,guid);return true;};
  consumers.verify_family=[&](auto&){order.push_back("verify");return true;};
  consumers.refresh_person_types=[&](auto&){order.push_back("refresh");return true;};
  consumers.family_changed=[&](auto id,auto&){order.push_back("event"+std::to_string(id));return true;};
 }
};
void identity_and_cold(){
 Fixture f;const auto c=character();const auto* old=f.neighbors.by_id(1);
 require(allocate_ts1_neighbor_id(f.seed)==2&&allocate_ts1_neighbor_id(f.seed,{2})==4,"neighbor allocation sees all records and historical reservations");
 f.state->add_created(c);f.neighbors.admit_created_character(c);f.personal.admit_created_character(c);
 require(f.neighbors.by_id(1)==old&&f.neighbors.next(1)==2&&f.neighbors.next(2)==3&&f.neighbors.id_for_guid(200)==std::optional<std::int16_t>{2},"new neighbor visible without moving old source records");
 const auto initial_family=f.neighbors.person_data(2,61);
 require(f.neighbors.by_id(2)->guid==200&&!f.neighbors.by_id(2)->person_data&&initial_family&&initial_family->value==0&&initial_family->source==NeighborDataSource::absent_person_data_zero,"schema0 neighbor preserves absent serialized bank and existing zero-backed read semantics");
 require(!f.world.find(2),"neighbor allocation does not allocate a World avatar");
 f.personal.set_family(c.identity,10);require(f.neighbors.person_data(2,61)->value==10&&!f.neighbors.by_id(2)->person_data,"mutable family projection preserves exact immutable seed");
 const auto wire=encode_ts1_character_family_snapshot(f.state->snapshot());
 require(encode_ts1_character_family_snapshot(decode_ts1_character_family_snapshot(wire))==wire,"character/family owned snapshot roundtrips");
 for(std::size_t n=0;n<wire.size();++n)rejects([&]{decode_ts1_character_family_snapshot({wire.begin(),wire.begin()+n});},"every truncated character/family wire rejects");
 const auto personal=encode_native_personal_snapshot(f.personal.snapshot());require(personal[0]==4,"gameplay writers select explicit personal version4");
 auto raw=apply_ts1_character_neighbors(f.seed,f.state->snapshot());
 require(raw.entries.size()==3&&raw.entries[1].record&&raw.entries[1].record->neighbor_id==2&&f.seed.entries[1].marker==0,"creation fills the original last-null manager slot without changing immutable source extent");
 const auto overlay=plan_neighborhood_character_family_overlay(f.source,f.neighborhood,nullptr,f.state->snapshot());
 const auto reopened=decode_family_overlay(encode_family_overlay(overlay));
 const auto slot_replay=apply_ts1_character_neighbors(f.seed,*reopened.character_family);
 require(encode_nbrs(raw)==encode_nbrs(slot_replay)&&allocate_original_neighbor_id({1,2,3},slot_replay.entries.size(),{})==4,"hole creation overlay/cold replay preserves next original manager allocation");
 require(encode_nbrs(merge_owned_neighbor_seeds(slot_replay,{c.seed}))==encode_nbrs(raw),"cold hydration and overlay use the same idempotent exact-slot receipt owner");
 NativePersonalState cold(raw,f.neighborhood);cold.restore(decode_native_personal_snapshot(personal));NeighborContext view(raw);view.bind_personal_state(&cold);view.admit_created_character(c);
 require(view.by_id(2)&&view.person_data(2,61)->value==10&&encode_native_personal_snapshot(cold.snapshot())==personal,"cold owner binds new seed before applying personal records, never rerolls or invents a body");
 auto collision=f.state->snapshot();collision.characters[0].seed.neighbor_id=1;collision.characters[0].identity.neighbor_id=1;
 rejects([&]{apply_ts1_character_neighbors(f.seed,collision);},"created NID collision rejects against complete immutable source");
 auto changed=f.state->snapshot();changed.characters[0].seed.name="different";
 rejects([&]{validate_ts1_character_family_successor(f.state->snapshot(),changed);},"created seed cannot change after publication");
 DefinitionCandidate original{"source","original.iff",128,{138,100,2,0,0,false,32,0}};
 DefinitionIndex definitions({original});const auto* first=definitions.unique(100);
 auto created=original;created.source="private/User2.iff";created.asset.clear();created.definition.guid=200;
 definitions.append_created_character(created);
 require(definitions.unique(100)==first&&definitions.unique(200)&&definitions.provider_snapshot_candidates().back().definition.guid==200,"definition append preserves old pointers and provider order");
 rejects([&]{definitions.append_created_character(created);},"created definition never shadows an existing GUID");
}
void membership_source_order(){
 Fixture f;const auto* n=f.neighbors.by_id(3);
 require(f.state->add_to_family(10,*n,f.consumers,true)&&f.order==std::vector<std::string>{"runtime","neighbor","11visible","refresh","event11","10visible","refresh","runtime","neighbor","event10","subset"},"source add removes old FAMI before neighbor61, new FAMI, runtime61 and runtime subset");
 require(f.state->family(11)&&f.state->family(11)->family.member_guids.empty()&&f.state->family(10)->family.member_guids==std::vector<std::uint32_t>{100,300}&&f.world.find(3)->person_data[61]==10,"move-in shares exact family identity and source provider removal");
 f.order.clear();require(f.state->remove_from_family(*f.neighbors.by_id(3),f.consumers)&&f.order==std::vector<std::string>{"runtime","neighbor","10visible","refresh","event10"},"source remove writes person family before current FAMI");
 require(f.world.find(3)&&f.world.find(3)->person_data[61]==0&&f.personal.by_id(3),"family removal is not object retirement or Sim identity deletion");
 f.order.clear();require(f.state->remove_from_family(*f.neighbors.by_id(1),f.consumers)&&f.state->family(10)&&f.state->family(10)->family.member_guids.empty(),"last member removes GUID while preserving the original FAMI provider object");
 FamilyOverlay overlay;overlay.neighborhood_identity=f.neighborhood;overlay.character_family=f.state->snapshot();overlay.personal_state=f.personal.snapshot();
 const auto bytes=encode_family_overlay(overlay);require(bytes[4]==13&&encode_family_overlay(decode_family_overlay(bytes))==bytes,"v13 overlay owns roster and personal mutation in one canonical carrier");
 const auto replay=replay_family_overlay(f.source,f.neighborhood,overlay);require(replay.families.size()==2&&replay.states.size()==2&&replay.families.front().family.member_guids.empty(),"empty family current roster preserves FAMI identity and occupancy history");
}
void partial_failure_and_source33(){
 for(bool adding:{false,true}){Fixture f;f.fail_runtime=true;
  rejects([&]{if(adding)f.state->add_to_family(10,*f.neighbors.by_id(3),f.consumers,true);else f.state->remove_from_family(*f.neighbors.by_id(1),f.consumers);},"failed actual consumer propagates after prior source effects");
  if(adding)require(f.neighbors.person_data(3,61)->value==11&&f.state->family(11)->family.member_guids==std::vector<std::uint32_t>{300}&&f.state->family(10)->family.member_guids.size()==1&&f.runtime_subset.size()==1,"failed first live write leaves old neighbor/FAMI and later subset intact");
  else require(f.neighbors.person_data(1,61)->value==10&&f.state->family(10)->family.member_guids==std::vector<std::uint32_t>{100},"failed first live write preserves subsequent neighbor and FAMI source effects");
 }
 Fixture f;require(f.state->add_to_family(11,*f.neighbors.by_id(1),f.consumers,true)&&f.state->family(11)->family.member_guids==std::vector<std::uint32_t>({300,100})&&f.state->family(10)->family.member_guids.empty()&&f.runtime_subset==std::vector<std::uint32_t>{100},"Generic33 explicit Temp0 target receives shared addition while CurrentFamily subset is unchanged");
 Fixture absent;require(!absent.state->add_to_family({},*absent.neighbors.by_id(3),absent.consumers,true),"Generic4 absent CurrentFamily returns false");
 Fixture money;require(money.state->combine_assets(10,11,money.consumers)&&money.state->family(10)->family.budget==1250&&money.state->family(11)->family.budget==200,"Generic5 copies donor assets into current budget without inventing a transfer/removal");
 require(money.state->verify_current(10,money.consumers)&&money.order.back()=="verify","Generic32 delegates actual SelectWholeFamily/VerifyFamily service");
}
void same_family_and_foreign_cold(){
 for(bool foreign:{false,true}){
  Fixture f;require(f.state->add_to_family(foreign?11:10,*f.neighbors.by_id(1),f.consumers,true),"actual same-family/foreign destination addition succeeds");
  require(f.runtime_subset==std::vector<std::uint32_t>{100},"same-family4/foreign33 do not duplicate or redirect the current native Thread cohort");
  auto overlay=plan_neighborhood_character_family_overlay(f.source,f.neighborhood,nullptr,f.state->snapshot());
  overlay=plan_neighborhood_personal_overlay(f.neighborhood,&overlay,f.personal.snapshot());
  const auto cold_overlay=decode_family_overlay(encode_family_overlay(overlay));
  const auto replay=replay_family_overlay(f.source,f.neighborhood,cold_overlay);
  NativePersonalState cold(f.seed,f.neighborhood);cold.restore(*cold_overlay.personal_state);
  NeighborContext view(f.seed);view.bind_personal_state(&cold);
  Ts1CharacterFamilyState restored(*cold_overlay.character_family,replay.families,{{10,family_overlay_identity(f.source[0])},{11,family_overlay_identity(f.source[1])}});
  require(view.person_data(1,61)->value==(foreign?11:10)&&restored.family(foreign?11:10)->family.member_guids.back()==100,"save/cold preserves explicit destination roster and exact neighbor family field");
  const auto snapshot=encode_ts1_character_family_snapshot(restored.snapshot());
  require(snapshot==encode_ts1_character_family_snapshot(f.state->snapshot()),"same-family/foreign operation cold state does not rerun or reorder execution");
 }
 std::vector<std::uint32_t> subset{100,300};admit_ts1_runtime_subset_member(subset,100);
 require(subset==std::vector<std::uint32_t>({100,300}),"native subset keeps its own stable selected order when FAMI reorders");
 select_ts1_runtime_whole_family(subset,{300,100});
 require(subset==std::vector<std::uint32_t>({300,100}),"Generic32 replaces runtime subset with actual current FAMI order without removing bodies");
 select_ts1_runtime_whole_family(subset,{});require(subset.empty(),"empty CurrentFamily is a real empty selected cohort, not a missing source");
 rejects([&]{select_ts1_runtime_whole_family(subset,{100,100});},"duplicate native subset is rejected without inventing deduplication");require(subset.empty(),"invalid cohort does not partially replace selected native state");
}
void person_type_source_refresh(){
 for(bool class_one:{false,true}){
  World world;for(std::int16_t id=1;id<=4;++id){std::vector<std::int16_t> words(88);words[32]=(id%2)?0:3;words[61]=77;
   require(world.create_saved_entity(id,EntityKind::avatar,static_cast<std::uint32_t>(100+id),{},words,{},std::array<std::int16_t,68>{},std::array<std::int16_t,5>{},0),"refresh fixture has actual definition GUIDs and person banks");}
  refresh_ts1_family_person_types(world,{101,102},class_one);
  require(world.find(1)->person_data[32]==(class_one?0:1)&&world.find(2)->person_data[32]==(class_one?0:1)&&world.find(3)->person_data[32]==1&&world.find(4)->person_data[32]==3,"source refresh uses CurrentFamily GUID membership and exact context class, preserving other nonzero types");
  require(world.find(1)->person_data[61]==77&&world.find(3)->person_data[61]==77,"source refresh never infers membership from or mutates person61");
 }
}
void explicit_family_write_knownness(){
 auto legacy=neighbor(1,100,10);legacy.version=4;legacy.person_mode=1;legacy.unknown3.reset();legacy.person_data->resize(80);legacy.person_data->at(67)=23;
 NativePersonalState state(Nbrs{0,73,{{1,legacy}},0},digest(99));
 state.set_family(identity(1,100),11,true);
 const auto& record=state.snapshot().records.front();
 require(record.projection.schema_version==1&&record.projection.profile==2&&record.projection.present[67]&&record.projection.values[67]==0&&!record.projection.present[68],"family67 write becomes known without upgrading source schema or inventing other fields");
 require(record.seed_words->at(67)==23,"family lingering-house write preserves immutable source words");
 std::vector<std::int16_t> words(256,-9);apply_original_neighbor_personal(*state.by_id(1),words);
 require(words[61]==11&&words[67]==0&&words[68]==-9,"cold family loader applies explicit known writes and preserves other absent fields");
 const auto bytes=encode_native_personal_snapshot(state.snapshot());require(encode_native_personal_snapshot(decode_native_personal_snapshot(bytes))==bytes,"NPER4 explicit known family field cold codec roundtrip");
 auto forged=state.snapshot();forged.records.front().writer=NativePersonalWriteSource::frontend_save;
 rejects([&]{encode_native_personal_snapshot(forged);},"legacy/frontend writer cannot claim family-only extra known field");
 forged=state.snapshot();forged.records.front().projection.present[68]=1;
 rejects([&]{encode_native_personal_snapshot(forged);},"family profile does not widen unrelated fields beyond original schema");
}
void retained_provider_and_frontend_roster(){
 Fixture f;
 auto state=f.state->snapshot();state.revision=1;state.characters.push_back(character());
 state.rosters.push_back({11,family_overlay_identity(f.source[1]),1,{300,200}});
 auto overlay=plan_neighborhood_character_family_overlay(f.source,f.neighborhood,nullptr,state);
 const auto removed=plan_family_delete_overlay(f.source,f.seed,f.neighborhood,&overlay,11);
 require(removed.deletions.back().members.size()==2&&removed.deletions.back().members[1].neighbor_id==2,"frontend deletes the actual current roster including created child identity");
 const auto neighbors=apply_neighbor_overlay(f.seed,f.neighborhood,removed);
 require(neighbors.entries.size()==1&&neighbors.entries[0].record->neighbor_id==1,"frontend current-roster retirement preserves unrelated neighbor");
 require(f.seed.entries.size()==3&&!f.seed.entries[1].record&&f.source[1].family.member_guids==std::vector<std::uint32_t>{300},"frontend current roster never mutates commercial FAMI/NBRS inputs");
 const auto bytes=encode_family_overlay(removed);require(encode_family_overlay(decode_family_overlay(bytes))==bytes,"current-roster frontend retirement cold codec is exact");
}
void actual_dispatch(){
 for(bool query:{false,true}){
  Fixture f;Ts1FamilyPrimitiveServices services{f.state.get(),&f.neighbors,[]{return std::optional<std::uint16_t>{10};},f.consumers};const auto handler=ts1_family_primitive(services);
  Vm vm({{routine_key(4096),Bhav{0x8002,{{1,254,255,{4,0,0,0,0,0,0,0}}},{},Bhav::Metadata{0,4,0,0}}}},f.world.vm_memory(),{},handler);
  require(vm.start(routine_key(4096),{1,1,3})==VmStatus::running,"actual family primitive source starts");
  const auto result=query?vm.run_synchronous_check(16,handler).status:vm.run(16);
  require(result==(query?VmStatus::fault:VmStatus::returned_true)&&f.state->family(10)->family.member_guids.size()==(query?1U:2U),"real instruction grants family service while arbitrary query root cannot mutate it");
 }
}
}
int main(){try{identity_and_cold();membership_source_order();partial_failure_and_source33();same_family_and_foreign_cold();person_type_source_refresh();explicit_family_write_knownness();retained_provider_and_frontend_roster();actual_dispatch();std::cout<<checks<<" character/family checks passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
