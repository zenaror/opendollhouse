#include "linux_travel_session.hpp"
#include "linux_family_overlay_store.hpp"
#include "linux_session_archive_store.hpp"
#include "travel_session_fixture.hpp"
#include "odh/sha256.hpp"
#include "odh/original_companion_protocol.hpp"
#include "odh/family_spells.hpp"
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
using travel_test::require;
template<class F>void rejects(F f,const char* why){try{f();}catch(const std::exception&){return;}throw std::runtime_error(why);}
void original_frontend_global_phase(){
 auto destination=travel_test::travel_state(travel_test::managed_home());
 destination.protocol=odh::Ts1TravelProtocol::original_6e598;destination.current_house=23;destination.current_mode=2;
 std::vector<std::int16_t> simi(64);simi[32]=1;simi[9]=0;simi[18]=73;
 odh::RuntimeGlobals globals(simi,odh::Ts1GlobalActivation::saved_new,23);
 odh_host::reset_original_frontend_load_globals(globals,destination,false);
 require(globals.read(32)==0&&globals.read(9)==0&&globals.read(18)==73,
   "actual original preload clears imported Simless without replacing synthetic family0 or unrelated globals");
 require(globals.write(32,1)&&globals.write(9,21),"simulate source load globals before the actual postload boundary");
 odh_host::reset_original_frontend_load_globals(globals,destination,false);
 require(globals.read(32)==0&&globals.read(9)==21,"postload reset preserves whole-family selection as well");
 require(globals.write(32,1),"seed an exact saved native continuation");const auto before=globals.snapshot();
 rejects([&]{odh_host::reset_original_frontend_load_globals(globals,destination,true);},"cold restore cannot invoke frontend reset");
 require(globals.snapshot().words==before.words&&globals.snapshot().selected_sim_is_runtime_id==before.selected_sim_is_runtime_id,
   "refused cold reset preserves all exact continuation values");
 auto legacy=destination;legacy.protocol=odh::Ts1TravelProtocol::legacy_pin;
 rejects([&]{odh_host::reset_original_frontend_load_globals(globals,legacy,false);},"legacy frontend is not silently changed");
 destination.family_activated=true;
 rejects([&]{odh_host::reset_original_frontend_load_globals(globals,destination,false);},"ordinary active gameplay is not a fresh load phase");
 require(globals.read(32)==1,"refused phases do not normalize historical Simless state");
}

odh::FamilyEntry vacation_family(std::uint16_t id,std::int32_t number,std::int32_t house,std::uint32_t guid){
 odh::Family f;f.version=9;f.family_number=number;f.house_number=house;f.budget=7000;f.member_guids={guid};
 f.remaining_bytes=16;f.tail_bytes.resize(16);odh::FamilyV9Extension ext;ext.spell_words={1,-2,3,-4,5,-6};
 f.original_v9_extension=ext;
 for(std::size_t n=0;n<6;++n){const auto word=static_cast<std::uint16_t>(ext.spell_words[n]);f.tail_bytes[4+n*2]=static_cast<std::uint8_t>(word);f.tail_bytes[5+n*2]=static_cast<std::uint8_t>(word>>8);}
 return {id,f};
}
odh::SessionBundle original_family_departure(odh::SessionBundle b,const odh::FamilyVacationSnapshot& bank,
 std::int16_t destination,std::optional<odh::Ts1TravelState> previous={}){
 odh::World world;require(world.restore_snapshot(b.world),"restore real source World for Vacation Generic17");
 const auto member=b.household_roster.front();auto words=world.find(member.id)->person_data;words.resize(88);
 require(world.replace_person_data(member.id,words)&&world.set_person_data(member.id,61,static_cast<std::int16_t>(b.host.family_chunk_id)),
  "actual CallerPD61 is FAMI chunk21, distinct from serialized FamilyNumber12");
 odh::Ts1TravelState state=previous.value_or(odh::Ts1TravelState{});
 if(!previous){state.home_house=b.host.house_number;state.current_house=b.host.house_number;state.home_mode=b.host.neighborhood_mode;state.current_mode=b.host.neighborhood_mode;
  state.family_chunk_id=b.host.family_chunk_id;state.family_number=b.host.family_number;state.neighborhood_identity=bank.neighborhood_identity;
  state.family_identity=bank.families.front().family_identity;for(const auto& row:bank.families)if(row.family_chunk_id==state.family_chunk_id)state.family_identity=row.family_identity;
  state.occupancy_epoch=b.family_lease?b.family_lease->occupancy_epoch:0;
  for(const auto& r:b.household_roster)state.permanent_roster.push_back({r.guid,r.neighbor_id,r.object_definition_id,r.character_asset,r.character_fingerprint});
  state.travelers=state.permanent_roster;state.caller_guid=member.guid;state.caller_neighbor_id=member.neighbor_id;
 }
 state.protocol=odh::Ts1TravelProtocol::original_6e598;state.pending.reset();
 odh::Nbrs nbrs{};nbrs.version=62;odh::NeighborRecord record{};record.version=10;record.name="iffname";record.person_mode=1;
 record.neighbor_id=member.neighbor_id;record.guid=member.guid;record.person_data=std::vector<std::int16_t>(256);nbrs.entries.push_back({1,record});
 odh::NeighborContext neighbors(nbrs);odh::NeighborInventory inventory(odh::Ngbh{}, {},odh::Ts1TravelProtocol::original_6e598);
 const auto identity=state.permanent_roster.front();
 odh::Bhav code{0x8002,{{1,1,255,{17,0,0,0,0,0,0,0}},{0,254,255,{}}},{},odh::Bhav::Metadata{0,4,4,0}};
 odh::Vm vm({{odh::routine_key(4096),code}},world.vm_memory(),{},[&](const auto& frame,const auto& instruction,const auto& temps,const auto& dispatch){
  odh::Ts1TravelRequest request;request.destination=temps[0];request.thread_owner={member.id,member.generation};request.run_generation=dispatch.origin->run_generation();
  request.ticket=dispatch.ticket;request.source_frame=frame;request.source_instruction=instruction;request.departure_transit=temps[3];request.departure_members={{identity,{member.id,member.generation}}};
  const auto resolve=[&](odh::WorldEntityReference ref){require(ref==request.thread_owner,"capture exact source generation");return identity;};
  auto captured=odh::prepare_original_travel_capture(world,inventory,neighbors,request.thread_owner,request.departure_members,temps,
   b.simulation->clock.hour,b.simulation->clock.minute,state.current_house==state.home_house,resolve);
  request.original=std::move(captured.receipt);require(inventory.commit_replacement(std::move(captured.inventory)),"commit real original source inventory capture");
  state.pending=std::move(request);return odh::PrimitiveOutcome{odh::PrimitiveDisposition::goto_true_next_tick,{}};
 });
 require(vm.start(odh::routine_key(4096),{member.id,member.id,member.id,0})==odh::VmStatus::running,"start real Vacation source Generic17");
 vm.temps()[0]=destination;vm.temps()[3]=1;require(vm.run(1)==odh::VmStatus::next_tick,"seal actual family1 Generic17 next-tick boundary");
 odh::EntityThreadControllers controllers(world,member.id);require(controllers.bind(vm),"bind actual sealed source controllers");
 b.runtime_entities.front().vm=vm.snapshot();b.runtime_entities.front().controllers=controllers.snapshot();b.world=world.snapshot();
 for(auto& e:b.scheduler.entities)if(e.id==member.id){e.status=vm.status();e.steps=vm.steps();e.run_generation=vm.run_generation();}
 b.travel=state;b.neighbor_inventory=inventory.snapshot();b.family_vacation=bank;odh::seal_ts1_travel_request(*b.travel,b);odh::validate_ts1_travel_bundle(b);return b;
}
std::vector<std::uint8_t> legacy_sims_source(std::vector<std::uint8_t> bytes){
 auto archive=odh::decode_session_archive(bytes);bool changed=false;
 for(auto& section:archive.sections)if(section.id==std::array<char,4>{{'S','I','M','S'}}){
  require(section.data.size()>=24&&section.data[0]==2&&section.data[1]==0&&section.data[2]==0&&section.data[3]==0,
    "fixture begins at real SIMS2 writer");
  section.data[0]=1;section.data.erase(section.data.begin()+20,section.data.begin()+24);changed=true;
 }
 require(changed,"fixture carries actual simulation section");return odh::encode_session_archive(archive);
}
void raw_source_checkpoint_matrix(){
 const auto home=travel_test::managed_home();const auto legacy=legacy_sims_source(odh::encode_session_bundle(home));
 const auto head=odh::sha256(legacy.data(),legacy.size());
 auto external=legacy;const auto accepted=odh::decode_session_source_checkpoint(external,head);external.front()^=1;
 require(accepted.envelope_bytes()==legacy&&accepted.active_bytes()==legacy&&accepted.digest()==head&&
   accepted.active().simulation&&accepted.active().simulation->clock.fire_percent==20000,
   "authenticated old HOME retains owned original bytes plus decoded native bridge without rewriting head");
 const auto upgraded=odh::encode_session_bundle(accepted.active());
 require(upgraded!=legacy&&accepted.active_bytes()!=upgraded&&odh::sha256(upgraded.data(),upgraded.size())!=head,
   "semantic SIMS1 decode/SIMS2 write is not byte identity for source binding");
 rejects([&]{(void)odh::decode_session_source_checkpoint(upgraded,head);},"normalized current encoding cannot impersonate accepted old head");
 auto movable=accepted;const auto moved=std::move(movable);
 require(moved.envelope_bytes()==legacy&&moved.digest()==head,"copy/move retains one immutable raw/model/digest binding");
 rejects([&]{(void)movable.digest();},"moved-from source cannot expose an unbound digest");
 auto wrong_head=head;wrong_head[0]^=1;
 rejects([&]{(void)odh::decode_session_source_checkpoint(legacy,wrong_head);},"changed expected head is rejected");
 auto limits=odh::SessionBundleLimits{};limits.archive.max_file_bytes=legacy.size()-1;
 rejects([&]{(void)odh::decode_session_source_checkpoint(legacy,head,limits);},"raw source respects existing archive budget");
 const std::vector<std::uint8_t> malformed{1,2,3};const auto malformed_head=odh::sha256(malformed.data(),malformed.size());
 rejects([&]{(void)odh::decode_session_source_checkpoint(malformed,malformed_head);},"matching digest cannot authenticate an undecodable bundle");
 auto active=home;active.travel=travel_test::travel_state(home);active.travel->current_house=23;active.host.house_number=23;
 auto outer=odh_host::make_travel_session_envelope(legacy,active);
 const auto visited=legacy_sims_source(*outer.travel_visited_session);outer.travel_visited_session=visited;
 const auto outer_raw=legacy_sims_source(odh::encode_session_bundle(outer));const auto outer_head=odh::sha256(outer_raw.data(),outer_raw.size());
 const auto nested=odh::decode_session_source_checkpoint(outer_raw,outer_head);
 require(nested.envelope_bytes()==outer_raw&&nested.active_bytes()==visited&&nested.active_bytes()!=outer_raw&&
   nested.digest()==outer_head&&nested.active().host.house_number==23&&nested.envelope().host.house_number==7,
   "TVIS source is the exact nested payload while accepted digest remains the OUTER envelope head");
 require(odh::encode_session_bundle(nested.active())!=nested.active_bytes(),"legacy visited decode also upgrades without changing raw binding");
 auto replaced=odh::decode_session_archive(outer_raw);
 for(auto& section:replaced.sections)if(section.id==std::array<char,4>{{'T','V','I','S'}})
   section.data=odh::encode_session_bundle(nested.active());
 const auto replaced_raw=odh::encode_session_archive(replaced);
 rejects([&]{(void)odh::decode_session_source_checkpoint(replaced_raw,outer_head);},
   "even semantically equal but reencoded visited payload cannot reuse accepted outer head");
}
void vacation_publication_matrix(){
 const auto directory=std::filesystem::temp_directory_path()/("odh-vacation-owner-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
 std::filesystem::create_directory(directory);setenv("XDG_DATA_HOME",directory.c_str(),1);setenv("XDG_CONFIG_HOME",directory.c_str(),1);
 auto home=travel_test::managed_home();const auto neighborhood=home.family_lease->neighborhood_identity;home.family_lease.reset();
 // This whole matrix is an original-protocol fixture from its first HOME, not a conversion of a legacy save.
 home.neighbor_inventory=odh::NeighborInventory(odh::Ngbh{}, {},odh::Ts1TravelProtocol::original_6e598).snapshot();
 std::vector<odh::FamilyEntry> families{vacation_family(21,12,7,123456),vacation_family(22,13,8,123457)};
 auto bank=odh::make_family_vacation_state(families,neighborhood,nullptr);home.family_vacation=bank.snapshot();
 home.family_spells=odh::FamilySpells(families.front()).snapshot();const auto spells=home.family_spells->words;
 odh_host::PrivateVacationSession owner(families,neighborhood,std::nullopt,21,7,1);
 const auto first_path=owner.publish(home);
 require(owner.adopted()&&!odh::decode_session_bundle(owner.read_current()).family_lease&&owner.checkpoint_path()==first_path,
  "unmanaged ordinary HOME publishes immutable pointer and shared bank without occupancy lease");
 auto stale=owner;
 auto departure=original_family_departure(home,bank.snapshot(),40);const auto source_path=owner.publish(departure);const auto source_head=*owner.head();
 rejects([&]{stale.publish(home);},"stale private overlay publisher cannot overwrite newer source head");
 auto phase=odh_host::prepare_vacation_entry_phase(departure,source_head,bank.snapshot());
 const auto legacy_departure=legacy_sims_source(odh::encode_session_bundle(departure));
 const auto legacy_departure_head=odh::sha256(legacy_departure.data(),legacy_departure.size());
 const auto accepted_departure=odh::decode_session_source_checkpoint(legacy_departure,legacy_departure_head);
 const auto legacy_phase=odh_host::prepare_vacation_entry_phase(accepted_departure,bank.snapshot());
 require(legacy_phase.source_head==legacy_departure_head&&legacy_phase.family_identity==phase.family_identity&&
   legacy_phase.destination==phase.destination&&legacy_phase.before_word==phase.before_word&&legacy_phase.after_word==phase.after_word,
   "vacation entry preserves old accepted raw head while deriving identical source family/phase");
 rejects([&]{(void)odh_host::prepare_vacation_entry_phase(accepted_departure.envelope(),legacy_departure_head,bank.snapshot());},
   "value-only canonical phase overload cannot pretend to retain old serialized identity");
 require(phase.family_chunk_id==21&&phase.destination==40&&phase.before_word==0&&phase.after_word==40,
  "actual source CallerPD61 proves family21 instead of serialized FamilyNumber12");
 auto prepared=bank.prepare(phase);auto forged=prepared.preview_snapshot();++forged.revision;
 const auto before=odh::encode_family_overlay(*owner.expected_overlay());
 rejects([&]{owner.publish_phase(phase,forged);},"private publisher refuses a forged preview before archive/head mutation");
 require(odh::encode_family_overlay(*owner.expected_overlay())==before,"forged preview leaves entire overlay untouched");
 owner.publish_phase(phase,prepared.preview_snapshot());require(bank.commit(prepared),"commit live owner only after accepted overlay CAS");
 require(owner.head()==std::optional<odh_host::TravelCheckpointDigest>{source_head}&&owner.checkpoint_path()==source_path&&
  odh::encode_session_bundle(odh::decode_session_bundle(owner.read_current()))==odh::encode_session_bundle(departure),
  "preavatar phase retains exact sealed source bytes even while shared bank has advanced");
 const auto selected=odh::select_family_vacation_snapshot(families,neighborhood,departure.family_vacation,&*owner.expected_overlay(),
  odh::FamilyVacationRestoreHead{odh::FamilyVacationHeadKind::private_travel,21,phase.family_identity,source_head});
 require(selected&&selected->revision==bank.snapshot().revision&&selected->families.front().current_word==std::optional<std::uint32_t>{40},
  "cold source selects new bank only through its exact accepted pointer and phase receipt");
 // First entry must retain the real prebooking HOME (word0), while the
 // active destination carries the admitted bank/spell word40. A later40->40
 // envelope cannot substitute for this transition test.
 auto first_active=departure;first_active.host.house_number=40;first_active.host.neighborhood_mode=3;
 first_active.travel->current_house=40;first_active.travel->current_mode=3;
 first_active.travel->family_activated=true;first_active.travel->pending.reset();
 first_active.family_vacation=bank.snapshot();
 (void)odh::project_family_spells_vacation(*first_active.family_spells,bank.snapshot(),&phase);
 const auto first_home_bytes=odh::encode_session_bundle(departure);
 const auto first_envelope=odh_host::make_travel_session_envelope(first_home_bytes,first_active);
 const auto first_cold=odh_host::reopen_travel_session_envelope(odh::decode_session_bundle(odh::encode_session_bundle(first_envelope)));
 require(first_cold.suspended_home==first_home_bytes&&first_cold.active.family_spells->extension_dword==40&&
  odh::decode_session_bundle(first_cold.suspended_home).family_spells->extension_dword==0&&
  first_cold.active.family_spells->words==spells&&first_cold.active.family_vacation->families.front().current_word==std::optional<std::uint32_t>{40},
  "first0-to40 entry preserves exact prebooking HOME and six spells across active envelope/cold decode");
 const auto idempotent=odh::encode_family_overlay(*owner.expected_overlay());owner.publish_phase(phase,bank.snapshot());
 require(odh::encode_family_overlay(*owner.expected_overlay())==idempotent,"same accepted phase does not advance bank or publish twice");
 auto progressed=home;progressed.family_vacation=bank.snapshot();progressed.host.family_budget=6990;
 (void)odh::project_family_spells_vacation(*progressed.family_spells,bank.snapshot(),&phase);
 const auto normal=owner.publish(progressed);
 require(owner.head()!=std::optional<odh_host::TravelCheckpointDigest>{source_head}&&owner.checkpoint_path()==normal&&
  !odh::family_vacation_phase(owner.vacation_overlay(),21)&&odh::decode_session_bundle(owner.read_current()).host.family_budget==6990,
  "ordinary postadoption save advances sole immutable pointer and clears only accepted family's phase");
 require(progressed.family_spells->words==spells,"booking updates preserve all six spell words");
 auto origin=departure.travel.value();origin.pending.reset();origin.current_house=origin.home_house;origin.current_mode=origin.home_mode;
 rejects([&]{odh_host::publish_private_home_session_checkpoint(directory/"legacy.odhs",std::nullopt,home,origin);},
  "legacy mutable private writer cannot create a second head after typed adoption");
 auto bad_head=source_head;bad_head[0]^=1;rejects([&]{odh_host::prepare_vacation_entry_phase(departure,bad_head,bank.snapshot());},
  "entry proof rejects changed source hash before family mutation");
 auto other=departure;other.world.entities.front().person_data[61]=22;
 rejects([&]{const auto wire=odh::encode_session_bundle(other);const auto digest=odh::sha256(wire.data(),wire.size());
  odh_host::prepare_vacation_entry_phase(other,digest,bank.snapshot());},
  "actual CallerPD61 and authenticated source family mismatch cannot change another family's booking");
 owner.validate_restore(owner.read_current());
 // Another installed family adopts a proven legacy HOME without changing bank,
 // archive bytes, or occupancy; subsequent ordinary writes share the same CAS.
 auto legacy=home;legacy.family_vacation.reset();legacy.host.house_number=8;legacy.host.family_chunk_id=22;
 legacy.host.family_number=13;legacy.host.resident_guid=123457;legacy.household_roster.front().guid=123457;
 legacy.world.entities.front().definition->guid=123457;legacy.world.entities.front().person_data[61]=22;
 legacy.family_spells=odh::FamilySpells(families[1]).snapshot();
 const auto legacy_bytes=odh::encode_session_bundle(legacy);const auto legacy_digest=odh::sha256(legacy_bytes.data(),legacy_bytes.size());
 const auto legacy_path=directory/"legacy-family22.odhs";
 odh_host::write_session_archive_atomic(legacy_path,odh::decode_session_archive(legacy_bytes));
 const auto old_bank=odh::family_vacation_digest(owner.vacation_overlay().state);
 odh_host::PrivateVacationSession adopted(families,neighborhood,owner.expected_overlay(),22,8,1,legacy_path,legacy_digest);
 adopted.adopt_legacy_home();
 require(adopted.adopted()&&adopted.head()==std::optional<odh_host::TravelCheckpointDigest>{legacy_digest}&&
  adopted.read_current()==legacy_bytes&&odh::family_vacation_digest(adopted.vacation_overlay().state)==old_bank,
  "legacy HOME admission adds only typed pointer while retaining exact old bytes and existing multi-family bank");
 require(odh::encode_session_archive(odh_host::read_session_archive(legacy_path))==legacy_bytes,
  "legacy mutable file remains untouched after explicit overlay-pointer admission");
 auto next=legacy;next.family_vacation=adopted.vacation_overlay().state;next.host.family_budget=6998;
 const auto changed=adopted.publish(next);require(adopted.checkpoint_path()==changed&&adopted.head()!=std::optional<odh_host::TravelCheckpointDigest>{legacy_digest}&&
  adopted.vacation_overlay().state.families.front().current_word==std::optional<std::uint32_t>{40},
  "ordinary save by another adopted family cannot roll back previous family's booking");
 rejects([&]{owner.publish(progressed);},"other-family accepted overlay update invalidates old private publisher rather than merging stale state");
 // A genuine sealed visited return is anchored to its outer immutable head;
 // clearing is an explicit typed residential phase, never ordinary cold.
 owner=odh_host::PrivateVacationSession(families,neighborhood,adopted.expected_overlay(),21,7,1);
 auto next_departure=original_family_departure(progressed,bank.snapshot(),40);owner.publish(next_departure);
 const auto immutable_home=odh::encode_session_bundle(next_departure);
 auto active=progressed;auto active_state=*next_departure.travel;active_state.current_house=40;active_state.current_mode=3;
 active_state.family_activated=true;active_state.pending.reset();active.travel=active_state;active.host.house_number=40;active.host.neighborhood_mode=3;
 owner.publish(odh_host::make_travel_session_envelope(immutable_home,active));
 auto return_active=original_family_departure(active,bank.snapshot(),-1,active_state);
 const auto return_envelope=odh_host::make_travel_session_envelope(immutable_home,return_active);
 owner.publish(return_envelope);const auto return_head=*owner.head();
 auto clear=odh_host::prepare_vacation_clear_phase(return_envelope,return_head,bank.snapshot(),21,7,odh::OriginalLotClass::residential);
 auto legacy_return=return_envelope;legacy_return.travel_visited_session=legacy_sims_source(*legacy_return.travel_visited_session);
 const auto legacy_return_raw=legacy_sims_source(odh::encode_session_bundle(legacy_return));
 const auto legacy_return_head=odh::sha256(legacy_return_raw.data(),legacy_return_raw.size());
 const auto accepted_return=odh::decode_session_source_checkpoint(legacy_return_raw,legacy_return_head);
 const auto legacy_clear=odh_host::prepare_vacation_clear_phase(accepted_return,bank.snapshot(),21,7,odh::OriginalLotClass::residential);
 require(legacy_clear.source_head==legacy_return_head&&legacy_clear.source_head!=return_head&&
   legacy_clear.before_word==clear.before_word&&legacy_clear.after_word==clear.after_word,
   "vacation residential clear binds original outer head even when both outer and TVIS use older codecs");
 rejects([&]{(void)odh_host::prepare_vacation_clear_phase(accepted_return,bank.snapshot(),21,7,odh::OriginalLotClass::community);},
   "typed raw source does not relax source lot-class guard");
 require(clear.before_word==40&&clear.after_word==0&&clear.source_head==return_head,
  "real visited Generic17 return binds clear to outer head and actual residential family");
 rejects([&]{odh_host::prepare_vacation_clear_phase(return_envelope,return_head,bank.snapshot(),21,7,odh::OriginalLotClass::community);},
  "community or ordinary cold cannot masquerade as residential clear phase");
 auto clear_prepared=bank.prepare(clear);owner.publish_phase(clear,clear_prepared.preview_snapshot());require(bank.commit(clear_prepared),"live clear commits after durable source-bound phase");
 const auto clear_selected=odh::select_family_vacation_snapshot(families,neighborhood,return_envelope.family_vacation,&*owner.expected_overlay(),
  odh::FamilyVacationRestoreHead{odh::FamilyVacationHeadKind::private_travel,21,clear.family_identity,return_head});
 require(clear_selected&&clear_selected->families.front().current_word==std::optional<std::uint32_t>{0}&&bank.available_count()==9,
  "cold return source accepts cleared bank only through exact typed before/after phase");
 auto returned_home=progressed;returned_home.family_vacation=bank.snapshot();
 (void)odh::project_family_spells_vacation(*returned_home.family_spells,bank.snapshot(),&clear);owner.publish(returned_home);
 require(!odh::decode_session_bundle(owner.read_current()).travel&&returned_home.family_spells->words==spells,
  "HOME ordinary publication after clear preserves spells and closes source travel");
 // Managed uses the same bank/phase CAS, not a second neighborhood state store.
 const auto managed_dir=directory/"managed";std::filesystem::create_directory(managed_dir);setenv("XDG_DATA_HOME",managed_dir.c_str(),1);setenv("XDG_CONFIG_HOME",managed_dir.c_str(),1);
 auto managed_home=home;auto journal=odh_host::FamilySessionJournal::adopt_original_session(families,neighborhood,std::nullopt,7,21,
  managed_home,[](const auto&,const auto& bytes){(void)odh::decode_session_bundle(bytes);});
 managed_home.family_lease=journal.lease();auto managed_bank=odh::make_family_vacation_state(families,neighborhood,&*journal.expected_overlay());
 auto managed_departure=original_family_departure(managed_home,managed_bank.snapshot(),49);const auto managed_source=journal.publish(managed_departure);
 const auto managed_bytes=odh::encode_session_archive(odh_host::read_session_archive(managed_source));const auto managed_head=odh::sha256(managed_bytes.data(),managed_bytes.size());
 auto managed_phase=odh_host::prepare_vacation_entry_phase(managed_departure,managed_head,managed_bank.snapshot());auto managed_prepared=managed_bank.prepare(managed_phase);
 auto wrong=managed_prepared.preview_snapshot();wrong.families.front().current_word=48;
 rejects([&]{journal.publish_vacation_phase(managed_phase,wrong);},"managed publisher rejects forged phase preview");
 journal.publish_vacation_phase(managed_phase,managed_prepared.preview_snapshot());require(managed_bank.commit(managed_prepared),"managed live bank commits once after phase publication");
 require(journal.checkpoint_path()==managed_source&&managed_bank.available_count()==9,
  "managed phase keeps sealed source and preserves original house49 exclusion from Generic27");
 journal.validate_restore(managed_bytes);
 std::filesystem::remove_all(directory);
}
int main(){try{
 original_frontend_global_phase();raw_source_checkpoint_matrix();vacation_publication_matrix();
 auto home=travel_test::managed_home();odh::Family f;f.house_number=7;f.family_number=12;f.budget=7000;f.member_guids={123456};std::vector<odh::FamilyEntry> source{{21,f}};
 const auto directory=std::filesystem::temp_directory_path()/("odh-travel-checkpoint-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));std::filesystem::create_directory(directory);setenv("XDG_DATA_HOME",directory.c_str(),1);setenv("XDG_CONFIG_HOME",directory.c_str(),1);
 const auto identity=home.family_lease->neighborhood_identity;home.family_lease.reset();
 auto journal=odh_host::FamilySessionJournal::adopt_original_session(source,identity,std::nullopt,7,21,home,[](const auto&,const auto& bytes){(void)odh::decode_session_bundle(bytes);});
 {
    const odh_host::LinuxNeighborhoodStorage third(3),fourth(4);
    const auto validate=[](const auto&,const auto& bytes){(void)odh::decode_session_bundle(bytes);};
    auto first=odh_host::FamilySessionJournal::adopt_original_session(source,identity,std::nullopt,7,21,home,validate,third);
    auto second=odh_host::FamilySessionJournal::adopt_original_session(source,identity,std::nullopt,7,21,home,validate,fourth);
    require(first.checkpoint_path()!=second.checkpoint_path() && first.checkpoint_path()!=journal.checkpoint_path() &&
        first.checkpoint_path()->filename()==second.checkpoint_path()->filename(),
        "identical households and captures retain separate archive directories and unchanged archive digest");
    auto traveled=home;traveled.family_lease=first.lease();
    traveled=travel_test::pending_travel_home(traveled,travel_test::travel_state(traveled));
    first.publish(traveled);
    second.verify_current();journal.verify_current();
    require(first.checkpoint_path()->filename()!=second.checkpoint_path()->filename() &&
        std::filesystem::exists(*second.checkpoint_path()),"travel in town three preserves town four and legacy town one");
    rejects([&]{odh_host::adopt_private_checkpoint_namespace(directory/"home.odhs",directory/"UserData4"/"home.odhs",identity,{},fourth);},
        "an ambiguous legacy private checkpoint cannot be adopted outside town one");
    require(!std::filesystem::exists(directory/"UserData4"),"rejected private adoption creates no files");
 }
 odh::FamilySpellsSnapshot spells;spells.family_chunk_id=home.host.family_chunk_id;spells.family_number=home.host.family_number;
 spells.loaded_presence=true;spells.extension_dword=0x1234abcd;spells.words={-32768,-9,0,1,2,3};home.family_spells=spells;home.type_attributes=odh::TypeAttributesSnapshot{};home.type_attributes->by_guid[123]={4,-2,9};
 home.family_lease=journal.lease();home=travel_test::pending_travel_home(home,travel_test::travel_state(home));journal.publish(home);const auto original=odh::encode_session_bundle(home);
 const auto pending_managed=odh_host::reopen_travel_session_checkpoint(journal,original);require(pending_managed.suspended_home==original&&pending_managed.active.travel->pending&&pending_managed.active.runtime_entities.front().vm.status==odh::VmStatus::next_tick,"managed restart recovers exact sealed HOME departure and advanced VM");
 const auto pending_head=odh::sha256(original.data(),original.size());
 rejects([&]{odh_host::authenticate_managed_visited_restore(journal,pending_head,original);},"sealed pending HOME is not an authenticated visited payload");
 auto active=home;active.travel=travel_test::travel_state(home);active.travel->current_house=23;active.host.house_number=23;active.host.family_budget=6990;active.neighbor_inventory->inventory_by_id[41]={{2,7,20},{2,8,30},{6,123,1}};
 active.family_spells->words={-4,-8,0,5,6,7};active.type_attributes->by_guid[123][1]=27;
 const auto envelope=odh_host::make_travel_session_envelope(original,active);require(envelope.host.house_number==7&&envelope.host.family_budget==6990,"journal envelope retains residence with actual trip budget");
 const auto encoded=odh::encode_session_bundle(envelope);auto reopened=odh_host::reopen_travel_session_envelope(odh::decode_session_bundle(encoded));require(reopened.suspended_home==original&&reopened.active.host.house_number==23,"fresh decode retains exact suspended home and active visited lot");
 require(envelope.family_spells&&reopened.active.family_spells&&
     envelope.family_spells->words==active.family_spells->words&&reopened.active.family_spells->words==active.family_spells->words,
     "active family spell progress survives outer envelope and fresh TVIS extraction without rewriting THOM");
 require(envelope.type_attributes&&reopened.active.type_attributes&&envelope.type_attributes->by_guid.at(123)[1]==27&&reopened.active.type_attributes->by_guid.at(123)[1]==27,"shared type attributes follow active destination without altering suspended home");
 auto missing_types=active;missing_types.type_attributes.reset();rejects([&]{odh_host::make_travel_session_envelope(original,missing_types);},"visited session cannot drop shared type attributes");
 auto dropped=active;dropped.family_spells.reset();rejects([&]{odh_host::make_travel_session_envelope(original,dropped);},"visited carrier cannot silently drop originating family spells");
 auto changed_source=active;++changed_source.family_spells->extension_dword;
 rejects([&]{odh_host::make_travel_session_envelope(original,changed_source);},"visited carrier cannot change immutable FAMI extension provenance");
 auto spell_mismatch=envelope;spell_mismatch.family_spells->words[0]++;
 rejects([&]{odh::encode_session_bundle(spell_mismatch);},"outer spell carrier must exactly match authoritative active TVIS");
 auto corrupt=envelope;corrupt.world.entities.front().person_data[0]++;rejects([&]{odh::encode_session_bundle(corrupt);},"home simulation mutation rejected");
 corrupt=envelope;corrupt.host.family_budget=5;rejects([&]{odh::encode_session_bundle(corrupt);},"outer budget mismatch rejected");
 corrupt=envelope;corrupt.travel_visited_session=encoded;rejects([&]{odh::encode_session_bundle(corrupt);},"nested envelope rejected before recursion");
 auto path=odh_host::publish_travel_session_checkpoint(journal,original,active);std::ifstream in(path,std::ios::binary);std::vector<std::uint8_t> disk{std::istreambuf_iterator<char>(in),{}};auto durable=odh_host::reopen_travel_session_checkpoint(journal,disk);require(durable.suspended_home==original&&durable.active.host.family_budget==6990,"authenticated durable reopen");
 auto foreign=original;foreign.back()^=1;rejects([&]{odh_host::publish_travel_session_checkpoint(journal,foreign,active);},"home substitution rejected without publication");
 const auto envelope_head=odh::sha256(disk.data(),disk.size());const auto exact_inner=odh::encode_session_bundle(active);
 const auto authorized=odh_host::authenticate_managed_visited_restore(journal,envelope_head,exact_inner);
 require(authorized.suspended_home==original&&odh::encode_session_bundle(authorized.active)==exact_inner,
     "managed visited restore authenticates exact inner bytes through its current residential envelope");
 rejects([&]{journal.validate_restore(exact_inner);},"standalone visited payload is not the journal checkpoint");
 rejects([&]{odh_host::authenticate_managed_visited_restore(journal,envelope_head,disk);},"outer envelope cannot masquerade as its visited payload");
 auto altered_inner=active;altered_inner.host.family_budget=6989;
 rejects([&]{odh_host::authenticate_managed_visited_restore(journal,envelope_head,odh::encode_session_bundle(altered_inner));},"valid but altered visited archive is not the embedded payload");
 auto wrong_head=envelope_head;wrong_head[0]^=1;
 rejects([&]{odh_host::authenticate_managed_visited_restore(journal,wrong_head,exact_inner);},"visited payload cannot substitute an expected envelope head");
 auto stale_journal=journal;active.host.family_budget=6980;odh_host::publish_travel_session_checkpoint(journal,original,active);rejects([&]{odh_host::publish_travel_session_checkpoint(stale_journal,original,active);},"stale journal head rejected");
 rejects([&]{odh_host::authenticate_managed_visited_restore(stale_journal,envelope_head,exact_inner);},"stale journal cannot authenticate an old visited payload");
 rejects([&]{odh_host::authenticate_managed_visited_restore(journal,envelope_head,exact_inner);},"current journal rejects a previously valid visited envelope head");
 auto installed_home=home;installed_home.family_lease.reset();auto origin=travel_test::travel_state(home);origin.occupancy_epoch=0;installed_home=travel_test::pending_travel_home(installed_home,origin);
 const auto installed_bytes=odh::encode_session_bundle(installed_home);auto visited=installed_home;visited.travel->current_house=23;visited.travel->pending.reset();visited.host.house_number=23;visited.host.family_budget=6000;
 const auto private_path=directory/"installed-family.odhs";
 auto source_head=odh_host::publish_private_home_session_checkpoint(private_path,std::nullopt,installed_home,origin);
 auto pending_installed=odh_host::reopen_private_travel_session_checkpoint(private_path,source_head,origin);require(pending_installed.suspended_home==installed_bytes&&pending_installed.active.travel->pending&&pending_installed.active.runtime_entities.front().vm.status==odh::VmStatus::next_tick,"unmanaged restart recovers exact sealed HOME departure after Generic17");
 auto head=odh_host::publish_private_travel_session_checkpoint(private_path,source_head,installed_bytes,visited,origin);
 auto installed=odh_host::reopen_private_travel_session_checkpoint(private_path,head,origin);require(!installed.active.family_lease&&installed.suspended_home==installed_bytes,"unmanaged family reopens without adopting occupancy");
 const auto old_head=head;visited.host.family_budget=5990;head=odh_host::publish_private_travel_session_checkpoint(private_path,head,installed_bytes,visited,origin);
 rejects([&]{odh_host::publish_private_travel_session_checkpoint(private_path,old_head,installed_bytes,visited,origin);},"unmanaged stale expected head rejected");
 rejects([&]{odh_host::publish_private_travel_session_checkpoint(private_path,std::nullopt,installed_bytes,visited,origin);},"unmanaged null head cannot overwrite existing checkpoint");
 auto wrong_origin=origin;wrong_origin.neighborhood_identity[0]^=1;
 rejects([&]{odh_host::reopen_private_travel_session_checkpoint(private_path,head,wrong_origin);},"unmanaged changed installed source hash rejected");
 wrong_origin=origin;wrong_origin.permanent_roster.front().character_fingerprint[0]^=1;
 rejects([&]{odh_host::reopen_private_travel_session_checkpoint(private_path,head,wrong_origin);},"unmanaged changed character resource rejected");
 auto changed_home=installed_home;changed_home.resources.resource_fingerprint[0]^=1;const auto changed_bytes=odh::encode_session_bundle(changed_home);
 rejects([&]{odh_host::publish_private_travel_session_checkpoint(private_path,head,changed_bytes,visited,origin);},"unmanaged original home resource hash substitution rejected");
 require(odh_host::reopen_private_travel_session_checkpoint(private_path,head,origin).active.host.family_budget==5990,"failed unmanaged publication preserves prior valid head and budget");
 auto returned=installed_home;returned.travel.reset();returned.host.family_budget=5990;returned.neighbor_inventory=visited.neighbor_inventory;
 const auto scoped_path=directory/"neighborhood-test"/private_path.filename();
 odh_host::adopt_private_checkpoint_namespace(private_path,scoped_path,head);
 odh_host::adopt_private_checkpoint_namespace(private_path,scoped_path,head); // An identical competing adoption is safe.
 require(odh_host::reopen_private_travel_session_checkpoint(scoped_path,head,origin).suspended_home==installed_bytes,
     "migrated active trip retains its authenticated suspended HOME and can reopen");
 rejects([&]{odh_host::adopt_private_checkpoint_namespace(private_path,scoped_path,old_head);},
     "changed legacy head cannot be adopted");
 const auto trip_head=head;head=odh_host::publish_private_home_session_checkpoint(private_path,head,returned,origin);
 rejects([&]{odh_host::adopt_private_checkpoint_namespace(private_path,scoped_path,head);},
     "a new legacy head cannot replace a different accepted namespaced head");
 require(odh_host::reopen_private_travel_session_checkpoint(scoped_path,trip_head,origin).active.travel.has_value(),
     "a rejected legacy adoption preserves the accepted trip");
 rejects([&]{odh_host::publish_private_home_session_checkpoint(private_path,trip_head,returned,origin);},"duplicate return expected head cannot repeat publication");
 const auto returned_bundle=odh::decode_session_bundle(odh::encode_session_archive(odh_host::read_session_archive(private_path)));require(!returned_bundle.travel&&returned_bundle.host.house_number==7&&returned_bundle.host.family_budget==5990,"unmanaged return uses same guarded home archive without occupancy changes");
 auto bad_epoch=installed_home;bad_epoch.travel->occupancy_epoch=1;rejects([&]{odh::encode_session_bundle(bad_epoch);},"unmanaged travel cannot impersonate occupancy epoch");
 auto magic_home=installed_home;magic_home.host.neighborhood_mode=7;magic_home.travel->home_mode=7;magic_home.travel->current_mode=7;auto magic_active=magic_home;magic_active.host.house_number=23;magic_active.host.neighborhood_mode=2;magic_active.travel->current_house=23;magic_active.travel->current_mode=2;magic_active.travel->pending.reset();
 const auto magic=odh_host::make_travel_session_envelope(odh::encode_session_bundle(magic_home),magic_active);require(magic.host.neighborhood_mode==7,"magic permanent residence keeps original home mode");
 // A visited runtime owns a genuine subset, never the permanent FAMI roster.
 auto whole_home=travel_test::managed_home();auto single_visited=whole_home;whole_home.family_lease.reset();single_visited.family_lease.reset();
 odh::World two_world;require(two_world.restore_snapshot(whole_home.world)&&two_world.create_saved_entity(2,odh::EntityKind::avatar,123457,{},std::vector<std::int16_t>(80),std::vector<std::int16_t>(16),{},{},0),"create second permanent resident");
 require(two_world.set_person_data(2,31,42)&&two_world.set_person_data(2,61,21),"bind second permanent resident identity");
 auto second_member=whole_home.household_roster.front();second_member.id=2;second_member.generation=two_world.find(2)->generation;second_member.guid=123457;second_member.neighbor_id=42;second_member.character_asset="PlayerData/Characters/User2.iff";second_member.character_fingerprint.fill(0x32);whole_home.household_roster.push_back(second_member);whole_home.world=two_world.snapshot();
 odh::Vm second_vm(odh::RoutineSet{});odh::EntityThreadControllers second_controllers(two_world,2);require(second_controllers.bind(second_vm),"bind second permanent resident controller");odh::SessionRuntimeEntity second_runtime;second_runtime.id=2;second_runtime.vm=second_vm.snapshot();second_runtime.controllers=second_controllers.snapshot();whole_home.runtime_entities.push_back(second_runtime);
 odh::EntityVmScheduler second_scheduler(4);require(second_scheduler.add(2,second_vm,[](auto){return true;},[]{return true;},[]{return true;}),"register second permanent resident");whole_home.scheduler.entities.push_back(second_scheduler.snapshot().entities.front());whole_home.simulation->avatars.push_back({2,whole_home.simulation->avatars.front().decay});
 auto subset=travel_test::travel_state(home);subset.occupancy_epoch=0;subset.permanent_roster.push_back({second_member.guid,second_member.neighbor_id,second_member.object_definition_id,second_member.character_asset,second_member.character_fingerprint});subset.travelers={subset.permanent_roster.front()};subset.current_house=23;
 single_visited.host.house_number=23;single_visited.travel=subset;
 const auto single_envelope=odh_host::make_travel_session_envelope(odh::encode_session_bundle(whole_home),single_visited);const auto single_reopen=odh_host::reopen_travel_session_envelope(odh::decode_session_bundle(odh::encode_session_bundle(single_envelope)));
 require(single_envelope.household_roster.size()==2&&single_reopen.active.household_roster.size()==1&&single_reopen.active.runtime_entities.size()==1&&single_reopen.active.travel->permanent_roster.size()==2,"single traveler visited subset preserves full permanent home roster without phantom runtime resident");
 auto wrong_subset=single_visited;wrong_subset.household_roster=whole_home.household_roster;rejects([&]{odh::validate_ts1_travel_bundle(wrong_subset);},"visited roster cannot expand to nontraveling permanent family");
 std::cout<<"Linux travel checkpoint tests passed\n";return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
