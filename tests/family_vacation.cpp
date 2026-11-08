#include "odh/family_vacation.hpp"
#include "odh/family_overlay.hpp"
#include "odh/family_spells.hpp"
#include "odh/original_companion_protocol.hpp"
#include "odh/original_frontend_personal.hpp"
#include "odh/ts1_travel_return.hpp"
#include <sstream>
#include "travel_session_fixture.hpp"
#include <algorithm>
#include <iostream>
namespace {
int checks=0;
void require(bool b,const char* m){++checks;if(!b)throw std::runtime_error(m);}
template<class F>void rejects(F f,const char* m){bool caught=false;try{f();}catch(const odh::FormatError&){caught=true;}require(caught,m);}
odh::FamilyVacationDigest digest(unsigned b){odh::FamilyVacationDigest d{};d.fill(static_cast<std::uint8_t>(b));return d;}
odh::FamilyEntry family(std::uint16_t id,std::int32_t number,std::int32_t house,std::uint32_t version=7,std::uint32_t booking=0){
 odh::Family f{};f.version=version;f.family_number=number;f.house_number=house;f.budget=20000;
 if(id)f.member_guids={1000U+id};
 if(version>=8){f.tail_bytes.resize(version==9?19:7,0xa5);for(unsigned n=0;n<4;++n)f.tail_bytes[n]=static_cast<std::uint8_t>(booking>>(8*n));f.remaining_bytes=f.tail_bytes.size();if(version==9){f.original_v9_extension=odh::FamilyV9Extension{booking,{-1,2,3,4,5,6}};for(unsigned n=0;n<6;++n){const auto w=static_cast<std::uint16_t>(f.original_v9_extension->spell_words[n]);f.tail_bytes[4+n*2]=static_cast<std::uint8_t>(w);f.tail_bytes[5+n*2]=static_cast<std::uint8_t>(w>>8);}}}
 return {id,f,{}};
}
const odh::FamilyVacationRow& row(const odh::FamilyVacationSnapshot& s,std::uint16_t id){return *std::find_if(s.families.begin(),s.families.end(),[&](const auto& f){return f.family_chunk_id==id;});}
odh::FamilyVacationPrivateHead head(const odh::FamilyVacationSnapshot& s,std::uint16_t id,unsigned hash){const auto& r=row(s,id);return {id,5,1,r.serialized_family_number,r.family_identity,digest(hash)};}
odh::FamilyVacationPhase phase(const odh::FamilyVacationSnapshot& s,std::uint16_t id,std::uint16_t destination,unsigned hash,bool clear=false){const auto& r=row(s,id);return {clear?odh::FamilyVacationPhaseKind::clear_residential_load:odh::FamilyVacationPhaseKind::set_before_avatars,id,destination,r.family_identity,digest(hash),odh::family_vacation_digest(s),*r.current_word,clear?0U:destination};}
void seed_and_codec(){
 const std::vector<odh::FamilyEntry> source={family(0,-1,0),family(5,6,5),family(7,81,0,8,0xfedcba98U),family(8,82,0,9,49)};
 auto bank=odh::make_family_vacation_state(source,digest(1),nullptr);
 require(bank.snapshot().families.size()==4&&row(bank.snapshot(),0).current_word==std::optional<std::uint32_t>{0},"real FAMI0 included; v7 ctor runtime0 has source authority");
 require(bank.available_count()==std::optional<std::int16_t>{9},"arbitrary DWORD and lot49 do not consume40..48");
 auto encoded=odh::encode_family_vacation_snapshot(bank.snapshot());require(odh::encode_family_vacation_snapshot(odh::decode_family_vacation_snapshot(encoded))==encoded,"FVAC preserves exact arbitrary words/chunkID/rawnumber/version");
 auto bad=encoded;bad.push_back(0);rejects([&]{odh::decode_family_vacation_snapshot(bad);},"trailing bytes rejected");bad=encoded;bad.pop_back();rejects([&]{odh::decode_family_vacation_snapshot(bad);},"truncation rejected");
 auto copy=bank.snapshot();copy.families.push_back(copy.families.front());rejects([&]{odh::validate_family_vacation_snapshot(copy);},"duplicate chunks rejected");
 copy=bank.snapshot();copy.families.front().seed_word=9;rejects([&]{bank.restore(copy);},"v7 cannot acquire invented serialized seed");
 auto occupied=bank.snapshot();occupied.families[0].current_word=40;occupied.families[1].current_word=40;occupied.families[2].current_word=48;
 require(odh::family_vacation_available_count(occupied)==std::optional<std::int16_t>{7},"Generic27 decrements once per distinct40..48 across all real families including0");
 auto incomplete=source;incomplete[2].family.tail_bytes.resize(2);incomplete[2].family.remaining_bytes=2;rejects([&]{odh::make_family_vacation_state(incomplete,digest(1),nullptr);},"v8 incomplete suffix does not invent data");
}
void prepare_publish_restore(){
 const auto identity=digest(1);const std::vector<odh::FamilyEntry> source={family(0,-1,0),family(5,6,5),family(7,8,0)};
 auto bank=odh::make_family_vacation_state(source,identity,nullptr);const auto baseline=bank.snapshot();
 auto p=phase(baseline,5,40,11);auto prepared=bank.prepare(p);auto stale=bank.prepare(p);
 require(bank.snapshot().revision==1&&prepared.preview_snapshot().revision==2,"prepare has no live/bank effects");
 odh::FamilyVacationOverlayState replacement{prepared.preview_snapshot(),{head(baseline,5,11)},{p}};
 // Pointer namespace preserves the existing Ts1TravelState home-mode domain.
 for(const std::uint8_t mode:{1,2,3,4,5,7}){auto typed=replacement;typed.private_heads.front().home_mode=mode;typed.private_heads.front().home_house=mode==7?91:5;const auto wire=odh::encode_family_vacation_overlay_state(typed);require(odh::decode_family_vacation_overlay_state(wire).private_heads.front().home_mode==mode,"typed private head preserves existing travel home namespace including Magic residential7");}
 for(const std::uint8_t mode:{0,6,8}){auto typed=replacement;typed.private_heads.front().home_mode=mode;rejects([&]{odh::validate_family_vacation_overlay_state(typed);},"private namespace rejects exactly existing invalid travel modes");}
 const auto overlay=odh::plan_family_vacation_overlay(source,identity,nullptr,replacement);const auto bytes=odh::encode_family_overlay(overlay);
 require(bytes.at(4)==12&&bytes.at(5)==0,"new bank explicitly selects overlayv12");const auto cold=odh::decode_family_overlay(bytes);require(odh::encode_family_overlay(cold)==bytes,"v12 complete bytes roundtrip");
 const auto replay=odh::replay_family_overlay(source,identity,cold);require(odh::encode_preserved_fami(replay.families[1].family)==odh::encode_preserved_fami(source[1].family),"v7 payload stays tail-free and unpromoted despite runtimebooking40");
 require(bank.commit(prepared)&&!bank.commit(prepared)&&!bank.commit(stale),"single-use/current bank authority rejects duplicate and stale prepared commits");
 const odh::FamilyVacationRestoreHead accepted{odh::FamilyVacationHeadKind::private_travel,5,row(baseline,5).family_identity,digest(11)};
 require(odh::select_family_vacation_snapshot(source,identity,std::nullopt,&cold,accepted)->revision==2,"exact preavatar sourcehead authorizes old/absent FVAC against advanced overlay bank");
 auto wrong=accepted;wrong.archive_sha256=digest(12);rejects([&]{odh::select_family_vacation_snapshot(source,identity,std::nullopt,&cold,wrong);},"arbitrary stale file cannot adopt newer bank");
 wrong=accepted;wrong.family_identity=digest(44);rejects([&]{odh::select_family_vacation_snapshot(source,identity,std::nullopt,&cold,wrong);},"head family identity authenticated");
 auto corrupt=replacement;corrupt.state.families[1].current_word=41;rejects([&]{odh::plan_family_vacation_overlay(source,identity,nullptr,corrupt);},"unprepared word mutation cannot publish");
 corrupt=replacement;corrupt.phases[0].source_head=digest(12);rejects([&]{odh::plan_family_vacation_overlay(source,identity,nullptr,corrupt);},"source phase must bind actual accepted head");
 auto normal=*cold.family_vacation;odh::clear_family_vacation_phase(normal,5);odh::set_family_vacation_private_head(normal,head(normal.state,5,12));const auto next=odh::plan_family_vacation_overlay(source,identity,&cold,normal);
 auto saved=normal.state;const odh::FamilyVacationRestoreHead nexthead{accepted.kind,5,accepted.family_identity,digest(12)};
 require(odh::select_family_vacation_snapshot(source,identity,saved,&next,nexthead)->revision==2,"ordinary adopted publication carries snapshot without extra booking revision");
 rejects([&]{odh::select_family_vacation_snapshot(source,identity,std::nullopt,&next,nexthead);},"ordinary publication cleared sourcephase; cannot masquerade legacy absence as phase");
 auto bank2=odh::make_family_vacation_state(source,identity,&next);auto set7=phase(bank2.snapshot(),7,41,13);auto prepared7=bank2.prepare(set7);auto two=*next.family_vacation;two.state=prepared7.preview_snapshot();odh::set_family_vacation_private_head(two,head(two.state,7,13));odh::set_family_vacation_phase(two,set7);const auto other=odh::plan_family_vacation_overlay(source,identity,&next,two);
 require(odh::select_family_vacation_snapshot(source,identity,saved,&other,nexthead)->revision==3,"other-family update cannot roll back on resume of accepted older own snapshot");
 auto bank3=odh::make_family_vacation_state(source,identity,&other);auto clear=phase(bank3.snapshot(),5,5,12,true);auto cleared=bank3.prepare(clear);auto zero=*other.family_vacation;zero.state=cleared.preview_snapshot();odh::set_family_vacation_phase(zero,clear);const auto returned=odh::plan_family_vacation_overlay(source,identity,&other,zero);
 require(row(returned.family_vacation->state,5).current_word==std::optional<std::uint32_t>{0}&&row(returned.family_vacation->state,7).current_word==std::optional<std::uint32_t>{41},"residential clear changes exact family only and preserves other reservation");
 require(odh::select_family_vacation_snapshot(source,identity,saved,&returned,nexthead)->revision==4,"typed clear phase authorizes sealed old return head before Main");
 // The managed publisher uses its existing accepted checkpoint, not a second head.
 const auto managed=odh::plan_family_original_adoption_overlay(source,identity,nullptr,5,5,20000,digest(20));auto mb=odh::make_family_vacation_state(source,identity,&managed);auto mp=phase(mb.snapshot(),5,48,20);auto mc=mb.prepare(mp);
 const auto managedvac=odh::plan_family_vacation_overlay(source,identity,&managed,{mc.preview_snapshot(),{},{mp}});
 require(odh::select_family_vacation_snapshot(source,identity,std::nullopt,&managedvac,odh::FamilyVacationRestoreHead{odh::FamilyVacationHeadKind::managed,5,row(mb.snapshot(),5).family_identity,digest(20)}).has_value(),"managed source phase shares journal accepted head");
}
void projection_and_legacy(){
 const auto identity=digest(1);std::vector<odh::FamilyEntry> source={family(5,6,5,9,0xfedcba98U)};
 auto bank=odh::make_family_vacation_state(source,identity,nullptr);auto p=phase(bank.snapshot(),5,42,4);auto c=bank.prepare(p);const auto before=odh::encode_preserved_fami(source[0].family);const auto projected=odh::project_family_vacation_families(source,c.preview_snapshot());
 require(projected[0].family.original_v9_extension->extension_dword==42&&projected[0].family.original_v9_extension->spell_words==source[0].family.original_v9_extension->spell_words&&projected[0].family.tail_bytes[18]==0xa5,"FAMI9 explicit projection changes booking only, preserves six spell words and unknown bytes");require(odh::encode_preserved_fami(source[0].family)==before,"source payload immutable");
 odh::FamilySpells spells(source[0]);const auto words=spells.snapshot().words;spells.admit_vacation(c.preview_snapshot(),&p);auto snap=spells.snapshot();require(snap.extension_dword==42&&snap.words==words,"explicit spells admission preserves immutable seed and six words");auto bad=snap;bad.extension_dword=99;rejects([&]{spells.restore(bad);},"ordinary spell restore still rejects altered booking metadata");
 auto old=odh::FamilySpells(source[0]).snapshot();require(odh::project_family_spells_vacation(old,c.preview_snapshot(),&p)&&old.words==words,"saved source metadata projects under bank without changing words");
 auto eight=family(5,6,5,8,0xabcdef01U);odh::FamilySpells oldeight(eight);const auto eightwire=odh::encode_family_spells_snapshot(oldeight.snapshot());require(oldeight.snapshot().extension_dword==0xabcdef01U&&!oldeight.snapshot().loaded_presence&&odh::encode_family_spells_snapshot(odh::decode_family_spells_snapshot(eightwire))==eightwire,"legacy v8 existing DWORD metadata is byte-preserved with no new bank");
 auto legacy=family(5,6,5);odh::FamilySpells seven(legacy);auto sb=odh::make_family_vacation_state({legacy},identity,nullptr);auto sp=phase(sb.snapshot(),5,49,4);auto sc=sb.prepare(sp);seven.admit_vacation(sc.preview_snapshot(),&sp);require(!seven.snapshot().loaded_presence&&seven.snapshot().extension_dword==49&&legacy.family.tail_bytes.empty(),"v7 runtime booking projects without inventing spell presence or suffix");
 auto oldoverlay=odh::FamilyOverlay{identity,{}};const auto oldbytes=odh::encode_family_overlay(oldoverlay);require(odh::encode_family_overlay(odh::decode_family_overlay(oldbytes))==oldbytes&&!odh::decode_family_overlay(oldbytes).family_vacation,"legacy overlay remains byte-identical and absent");
 auto session=travel_test::managed_home();const auto oldsession=odh::encode_session_bundle(session);require(!odh::decode_session_bundle(oldsession).family_vacation,"old session absence stays absent");
 auto f=family(session.host.family_chunk_id,session.host.family_number,session.host.house_number);auto ss=odh::make_family_vacation_state({f},session.family_lease->neighborhood_identity,nullptr);session.family_vacation=ss.snapshot();session.family_vacation->families.front().family_identity=session.family_lease->family_identity;const auto with=odh::encode_session_bundle(session);require(odh::encode_session_bundle(odh::decode_session_bundle(with))==with,"required FVAC roundtrips with native session");
 session.family_vacation->neighborhood_identity=digest(99);rejects([&]{odh::encode_session_bundle(session);},"session neighborhood provenance cannot be replaced");
}
void saved_carrier_successor(){
 const auto identity=digest(1);const auto source=family(5,6,5);auto bank=odh::make_family_vacation_state({source},identity,nullptr);
 odh::FamilySpells carrier(source);auto saved=carrier.snapshot();saved.words={1,2,3,4,5,6};const auto words=saved.words;
 const auto before=bank.snapshot();auto set=phase(before,5,40,4);auto pending=bank.prepare(set);const auto occupied=pending.preview_snapshot();
 require(odh::project_family_spells_vacation_successor(saved,before,occupied)&&saved.extension_dword==40&&saved.words==words,"saved HOME0 carrier projects across authenticated bank0-to40 without touching six words");
 require(bank.commit(pending),"publish occupied source for clear proof");auto clear=phase(bank.snapshot(),5,5,5,true);auto clearing=bank.prepare(clear);const auto empty=clearing.preview_snapshot();
 auto wrong=saved;wrong.extension_dword=99;rejects([&]{odh::project_family_spells_vacation_successor(wrong,occupied,empty);},"arbitrary saved before word cannot gain clear authority");
 require(odh::project_family_spells_vacation_successor(saved,occupied,empty)&&saved.extension_dword==0&&saved.words==words,"saved40 carrier clears to0 only through exact authenticated before/after snapshots");
 auto unproven=carrier.snapshot();unproven.extension_dword=40;rejects([&]{odh::project_family_spells_vacation(unproven,empty);},"unbound old40 cannot be silently ignored when current bank is0");
 auto changed=empty;changed.families.front().family_identity=digest(99);rejects([&]{odh::project_family_spells_vacation_successor(saved,empty,changed);},"successor cannot rebind family provenance");
}
void lifecycle(){
 const auto id=digest(1);std::vector<odh::FamilyEntry> families={family(0,-1,0),family(5,6,5),family(7,8,0)};
 odh::Nbrs neighbors{};neighbors.version=62;odh::NeighborRecord seed{};seed.version=10;seed.name="iffname";seed.unknown3=9;seed.person_mode=9;seed.person_data=std::vector<std::int16_t>(256);seed.neighbor_id=51;seed.guid=1007;neighbors.entries.push_back({1,seed});
 auto bank=odh::make_family_vacation_state(families,id,nullptr);auto p=phase(bank.snapshot(),7,40,11);auto c=bank.prepare(p);auto base=odh::plan_family_vacation_overlay(families,id,nullptr,{c.preview_snapshot(),{head(bank.snapshot(),7,11)},{p}});
 const auto creation=odh::make_simitone_family_creation(families,neighbors,id,&base,"New",{{987654U,std::vector<std::int16_t>(88)}});
 const auto added=odh::plan_family_creation_overlay(families,neighbors,id,&base,creation);
 require(added.family_vacation->state.families.size()==4&&added.family_vacation->state.revision==3&&row(added.family_vacation->state,7).current_word==std::optional<std::uint32_t>{40},"create reconciles one shared bank preserving existing reservations");
 const auto archived=odh::plan_family_archive_overlay(families,neighbors,id,&added,creation.family_chunk_id);
 require(!row(archived.family_vacation->state,creation.family_chunk_id).active&&archived.family_vacation->state.revision==4,"archive retains reserved booking seed identity as inactive tombstone");
 const auto deleted=odh::plan_family_delete_overlay(families,neighbors,id,&archived,7);
 require(!row(deleted.family_vacation->state,7).active&&row(deleted.family_vacation->state,7).current_word==std::optional<std::uint32_t>{40}&&odh::family_vacation_available_count(deleted.family_vacation->state)==std::optional<std::int16_t>{9},"delete preserves historical value but releases active occupancy count");
 require(!odh::family_vacation_private_head(*deleted.family_vacation,7)&&!odh::family_vacation_phase(*deleted.family_vacation,7),"removed family cannot retain active privatehead/sourcephase");
 auto resurrect=deleted;resurrect.deletions.clear();rejects([&]{odh::reconcile_family_vacation_overlay(families,id,resurrect);},"reserved inactive family cannot be silently resurrected");
 auto substituted=families;substituted[1].family.family_number++;rejects([&]{odh::make_family_vacation_state(substituted,id,&deleted);},"numeric chunk reuse with changed immutable provenance rejects");
}

std::string neighborhood_iff(const odh::Nbrs& neighbors){
 const auto payload=odh::encode_nbrs(neighbors);std::string bytes="IFF FILE 2.5:TYPE FOLLOWED BY SIZE JAMIE DOORNBOS & MAXIS 1";bytes.resize(64,'\0');bytes+="NBRS";const auto size=static_cast<std::uint32_t>(76+payload.size());for(int shift:{24,16,8,0})bytes.push_back(static_cast<char>(size>>shift));bytes.append("\0\1\0\0",4);bytes.append(64,'\0');bytes.append(reinterpret_cast<const char*>(payload.data()),payload.size());return bytes;
}
void family_personal_proof(bool progress){
 auto b=travel_test::managed_home();auto& person=b.world.entities.front();const auto oldsize=person.person_data.size();person.person_data.resize(256);b.world.word_count+=256-oldsize;person.person_data[10]=progress?123:0;person.person_data[80]=progress?77:0;
 odh::World world;require(world.restore_snapshot(b.world),"restore actual256 source for family1 writer");const auto resident=b.household_roster.front();
 odh::Nbrs neighbors{};neighbors.version=62;odh::NeighborRecord seed{};seed.version=10;seed.name="iffname";seed.unknown3=9;seed.person_mode=9;seed.person_data=std::vector<std::int16_t>(256);(*seed.person_data)[31]=resident.neighbor_id;(*seed.person_data)[61]=b.host.family_chunk_id;seed.neighbor_id=resident.neighbor_id;seed.guid=resident.guid;neighbors.entries.push_back({1,seed});
 const auto bytes=neighborhood_iff(neighbors);std::istringstream stream(bytes,std::ios::binary);const auto source=odh::read_original_frontend_personal_source(odh::View(stream,bytes.size()));
 b.family_lease->neighborhood_identity=source.neighborhood_identity();odh::NeighborContext context(neighbors);odh::NativePersonalState personal(neighbors,source.neighborhood_identity());
 auto state=travel_test::travel_state(b);state.protocol=odh::Ts1TravelProtocol::original_6e598;const auto member=state.permanent_roster.front();
 odh::NeighborInventory inventory(odh::Ngbh{},{},odh::Ts1TravelProtocol::original_6e598);
 odh::Bhav program{0x8002,{{1,1,255,{17,0,0,0,0,0,0,0}},{0,254,255,{}}},{},odh::Bhav::Metadata{0,4,4,0}};
 odh::Vm vm({{odh::routine_key(4096),program}},world.vm_memory(),{},[&](const auto& frame,const auto& instruction,const auto& temps,const auto& dispatch){
  odh::Ts1TravelRequest request;request.destination=temps[0];request.thread_owner={resident.id,resident.generation};request.run_generation=dispatch.origin->run_generation();request.ticket=dispatch.ticket;request.source_frame=frame;request.source_instruction=instruction;request.departure_transit=temps[3];request.departure_members={{member,{resident.id,resident.generation}}};
  auto captured=odh::prepare_original_travel_capture(world,inventory,context,request.thread_owner,request.departure_members,temps,b.simulation->clock.hour,b.simulation->clock.minute,true,[&](auto ref){if(ref!=request.thread_owner)throw odh::FormatError("wrong source");return member;});request.original=captured.receipt;require(inventory.commit_replacement(std::move(captured.inventory)),"actual original effects publication");
  if(progress){auto prepared=personal.prepare(world,{{{member.neighbor_id,member.guid,member.definition_chunk_id,member.character_asset,member.character_fingerprint},request.thread_owner}},odh::NativePersonalWriteSource::family17);require(personal.commit(std::move(prepared),world),"real family17 selective writer");}state.pending=request;return odh::PrimitiveOutcome{odh::PrimitiveDisposition::goto_true_next_tick,{}};
 });
 require(vm.start(odh::routine_key(4096),{resident.id,resident.id,resident.id,0})==odh::VmStatus::running,"start real source Generic17");vm.temps()[0]=40;vm.temps()[3]=1;require(vm.run(1)==odh::VmStatus::next_tick,"real family1 sealed boundary");
 odh::EntityThreadControllers controllers(world,resident.id);require(controllers.bind(vm),"bind actual source controllers");b.runtime_entities.front().vm=vm.snapshot();b.runtime_entities.front().controllers=controllers.snapshot();b.world=world.snapshot();for(auto& e:b.scheduler.entities)if(e.id==resident.id){e.status=vm.status();e.steps=vm.steps();e.run_generation=vm.run_generation();}
 b.neighbor_inventory=inventory.snapshot();if(progress)b.personal_state=personal.snapshot();b.travel=state;odh::seal_ts1_travel_request(*b.travel,b);
 const auto before=odh::encode_session_bundle(b);odh::validate_original_family_departure_personal(b,source);odh::validate_original_family_departure_personal(b,source);require(odh::encode_session_bundle(b)==before,"personal proof idempotent and has no source/World/revision mutation");
 if(progress){auto missing=b;missing.personal_state.reset();rejects([&]{odh::validate_original_family_departure_personal(missing,source);},"unpersisted actual skill/fame delta cannot pass via immutable seed");}
 auto stale=b;stale.world.entities.front().person_data[10]++;rejects([&]{odh::validate_original_family_departure_personal(stale,source);},"actual personal value changed after writer is refused without whitelist");
 if(progress){auto wrong=b;wrong.personal_state->records.front().identity.character_fingerprint[0]++;rejects([&]{odh::validate_original_family_departure_personal(wrong,source);},"exact resource ownership remains enforced");
 auto prior=b;prior.personal_state->records.front().writer=odh::NativePersonalWriteSource::retirement;odh::validate_original_family_departure_personal(prior,source);require(prior.personal_state->records.front().revision==b.personal_state->records.front().revision,"equal prior selective writer accepted without artificial family17 freshness");}

}

}
int main(){try{seed_and_codec();prepare_publish_restore();projection_and_legacy();saved_carrier_successor();lifecycle();family_personal_proof(true);family_personal_proof(false);std::cout<<"family_vacation checks="<<checks<<'\n';return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
