// SPDX-License-Identifier: MPL-2.0
#include "odh/original_companion_protocol.hpp"
#include "odh/ts1_travel_return.hpp"
#include "travel_session_fixture.hpp"
#include <iostream>
#include <stdexcept>
#include <algorithm>
namespace {
void require(bool value,const char* why){if(!value)throw std::runtime_error(why);}
template<class F>void rejects(F&& f,const char* why){try{f();}catch(const odh::FormatError&){return;}throw std::runtime_error(why);}
odh::Ts1TravelMember identity(std::int16_t nid){odh::Ts1TravelMember m;
 m.guid=123400+nid;m.neighbor_id=nid;m.definition_chunk_id=128;m.character_asset="UserData/Characters/User"+std::to_string(nid)+".iff";m.character_fingerprint.fill(static_cast<std::uint8_t>(nid));return m;}
odh::Nbrs seed(){odh::Nbrs n{};n.version=62;for(std::int16_t nid:{41,42,43}){odh::NeighborRecord r{};
 r.version=10;r.neighbor_id=nid;r.guid=identity(nid).guid;r.unknown3=0;r.person_mode=1;r.person_data=std::vector<std::int16_t>(256);n.entries.push_back({1,r});}return n;}
void person(odh::World& w,std::int16_t id,std::int16_t nid,std::int16_t family=5){
 require(w.create(id,odh::EntityKind::avatar,{0,101,3,16})&&w.define_type(id,identity(nid).guid),"create authenticatable person");
 require(w.set_person_data(id,31,nid)&&w.set_person_data(id,61,family)&&w.set_person_data(id,8,7)&&w.set_person_data(id,80,23)&&w.set_person_data(id,81,41)&&w.set_person_data(id,82,31),"bind personal fields");
 for(std::int16_t motive:{9,6,5,15,7,8,14})require(w.set_motive(id,motive,static_cast<std::int16_t>(motive*4-50)),"bind distinct needs");
}
odh::OriginalTravelIdentityResolver resolver(const odh::World& w){return [&w](odh::WorldEntityReference r){const auto* e=w.find(r.id);if(!e||e->generation!=r.generation)throw odh::FormatError("stale body");return identity(e->person_data.at(31));};}
odh::NeighborInventory inventory(){return odh::NeighborInventory(odh::Ngbh{}, {},odh::Ts1TravelProtocol::original_6e598);}
odh::Instruction op(std::uint8_t mode,std::uint8_t type=2,unsigned flags=0){return {51,254,255,{mode,type,static_cast<std::uint8_t>(flags),static_cast<std::uint8_t>(flags>>8),1,0,0,0}};}
void matrix_inventory(){
 for(const auto mode:{5,6}){auto inv=inventory();auto s=inv.snapshot();s.inventory_by_id[41]={{mode==5?1:3,999,7},{2,999,0},{17,55,65535}};inv.restore(s);
  odh::Ts1Temps t{};t[0]=42;require(inv.execute_original(41,op(mode),t,1,identity(42).guid).disposition==odh::PrimitiveDisposition::goto_true,"direction marker production");
  const auto& row=*inv.items(41);require(row.size()==3&&row.back().type==(mode==5?1:3)&&row.back().guid==identity(42).guid&&row.back().count==1&&row.front().type==2,"literal direction clears all same type, preserves other entries");
 }
 auto inv=inventory();auto s=inv.snapshot();s.inventory_by_id[41]={{2,0,0},{2,1,65535},{0,1,6},{2,1,77}};inv.restore(s);odh::Ts1Temps t{};
 const auto before=inv.snapshot();auto r=inv.execute_original(41,op(3,2,32|(2<<11)),t,0,{});
 require(r.disposition==odh::PrimitiveDisposition::goto_false&&!r.temp_write&&!r.second_temp_write&&odh::same_neighbor_inventory_snapshot(before,inv.snapshot()),"zero entry found is false without register writes");
 r=inv.execute_original(41,op(3,2,32|(2<<11)),t,1,{});require(r.temp_write->index==1&&r.temp_write->value==-1&&r.second_temp_write->index==2&&r.second_temp_write->value==1,"negative signed entry and original flag mapping");
 t[0]=-1;r=inv.execute_original(41,op(4,0,32|(2<<11)),t,1,{});require(r.second_temp_write->value==2&&r.temp_write->value==6,"type0 exact rather than wildcard");
 t[0]=25;r=inv.execute_original(99,op(1,2,2),t,1,{});require(r.disposition==odh::PrimitiveDisposition::goto_false&&!inv.items(99),"missing removal does not manufacture inventory row");
 auto prepared=inv.prepare_replacement(inv.snapshot());inv.set_data(41,88,2);require(!inv.commit_replacement(std::move(prepared)),"stale prepared publication rejected");
 auto invalid=inv.snapshot();invalid.version=1;rejects([&]{inv.prepare_replacement(invalid);},"prepared commit cannot migrate semantic profile");
 auto legacy=odh::NeighborInventory(odh::Ngbh{});rejects([&]{odh::consume_original_travel_time(legacy,41);},"original consumer cannot run over legacy producer");
}
void matrix_capture_and_reuse(){
 for(const bool family:{false,true})for(const bool returning:{false,true}){
  odh::World w;person(w,11,41);person(w,12,42,6);odh::NeighborContext neighbors(seed());auto inv=inventory();
  auto initial=inv.snapshot();initial.inventory_by_id[41]={{2,999,27},{returning?3:1,identity(42).guid,1},{17,66,0}};
  initial.inventory_by_id[42]={{2,999,8},{21,1,7}};inv.restore(initial);
  require(w.set_motive(12,9,0),"source companion zero need");
  odh::Ts1Temps temps{};temps[0]=returning?-1:23;temps[1]=1;temps[2]=10;temps[3]=family?1:0;
  const std::vector<odh::Ts1TravelDepartureMember> members=family?std::vector<odh::Ts1TravelDepartureMember>{{identity(41),{11,w.find(11)->generation}},{identity(42),{12,w.find(12)->generation}}}:std::vector<odh::Ts1TravelDepartureMember>{{identity(41),{11,w.find(11)->generation}}};
  const auto exact_before=inv.snapshot();auto capture=odh::prepare_original_travel_capture(w,inv,neighbors,{11,w.find(11)->generation},members,temps,23,55,true,resolver(w));
  require(odh::same_neighbor_inventory_snapshot(exact_before,inv.snapshot()),"prepare leaves mutable authority unchanged");
  require(capture.receipt.effects.size()==2&&capture.receipt.time_hour==1&&capture.receipt.time_minute==5,"literal source time and selected companion/family capture");
  require(inv.commit_replacement(std::move(capture.inventory)),"publish sealed inventory");
  require(!inv.get_data(41,999)&&!inv.get_data(42,999)&&!inv.get_data(42,0),"actual Generic17 clears all type2; source0 remains absent");
  const auto marker=odh::select_original_companion(inv,neighbors,41,returning?3:1);require(marker.has_value(),"direction marker survives capture");
  const auto count=w.snapshot().entities.size();const auto ref=odh::lookup_original_companion(w,*marker,resolver(w));require(ref&&ref->id==12&&w.snapshot().entities.size()==count,"return/outgoing reuse source exact existing cross-family body");
  auto consumption=odh::prepare_original_companion_consumption(inv,*marker);require(inv.commit_replacement(std::move(consumption))&&!odh::select_original_companion(inv,neighbors,41,returning?3:1),"reuse consumes exactly one selected marker");
  rejects([&]{odh::prepare_original_companion_consumption(inv,*marker);},"consumed marker cannot replay");
  person(w,13,42,6);rejects([&]{odh::lookup_original_companion(w,*marker,resolver(w));},"ambiguous duplicated GUID/NID rejects reuse");
 }
 require(odh::original_travel_time(40,23,58,0,0,true)==std::make_pair(std::int16_t{0},std::int16_t{0}),"residential vacation uses raw source Temps");
 require(odh::original_travel_time(23,2,4,0,125,false)==std::make_pair(std::int16_t{3},std::int16_t{9}),"original minute carry increments hour only once");
}
void matrix_effects(){
 for(const bool held:{false,true}){
  odh::World w;person(w,1,41);require(w.set_position(1,{24,24,1,2}),"in-world source");
  if(held)require(w.create(500,odh::EntityKind::object,{0,0,1})&&w.set_position(500,{24,24,1,2})&&w.attach(500,0,1),"held source");
  auto inv=inventory();auto s=inv.snapshot();s.inventory_by_id[41]={{2,0,65463},{2,9,3},{2,10,55},{2,11,66},{2,12,77},{2,7,12},{2,8,0},{2,1,13},{29,6,0}};inv.restore(s);
  const auto before=*w.find(1);const auto tokens=inv.snapshot();
  rejects([&]{odh::apply_original_travel_effects(w,inv,{1,before.generation},{});},"missing outfit resource must fail complete effects operation");
  require(w.find(1)->motives==before.motives&&w.find(1)->person_data==before.person_data&&odh::same_neighbor_inventory_snapshot(tokens,inv.snapshot()),"failed outfit resolution is atomic with needs/fame/tokens");
  odh::AvatarOutfitReference chosen;chosen.kind=odh::AvatarOutfitKind::none;
  odh::apply_original_travel_effects(w,inv,{1,before.generation},[&](const odh::World&,std::int16_t id,std::uint16_t outfit)->std::optional<odh::AvatarOutfitReference>{require(id==1&&outfit==3,"real effect resolves transported outfit");return chosen;});
  const auto* after=w.find(1);require(after->motives[9]==-73&&after->person_data[8]==3&&after->person_data[80]==55&&after->person_data[82]==66&&after->person_data[81]==77&&after->avatar_appearance&&after->avatar_appearance->body==chosen,"live consumer publishes needs/outfit appearance/fame mapping");
  require(after->position==before.position&&static_cast<bool>(after->attachment)==static_cast<bool>(before.attachment)&&(!held||w.find(500)->slots[0]==1),"live consumer preserves physical source and reciprocal container");
  require(inv.get_data(41,1)==13&&after->motives[6]==before.motives[6],"first unrecognized/time entry blocks later effects exactly");
  auto time=odh::consume_original_travel_time(inv,41);require(time.temp_write&&time.temp_write->value==12&&time.second_temp_write&&time.second_temp_write->value==0,"time consumer distinguishes explicit zero from absent");
  odh::apply_original_travel_effects(w,inv,{1,before.generation},{});require(w.find(1)->motives[6]==13&&inv.items(41)->back().type==29,"later authored effects call consumes after time removal; unknown other type retained");
 }
 auto inv=inventory();inv.set_data(41,8,39);const auto before=inv.snapshot();auto result=odh::consume_original_travel_time(inv,41);require(!result.temp_write&&!result.second_temp_write&&odh::same_neighbor_inventory_snapshot(before,inv.snapshot()),"absent hour leaves minute and Temps untouched");
}
void matrix_sealed_dispatch(){
 auto b=travel_test::managed_home();b.world.entities.front().person_data.resize(101);b.world.word_count+=21;
 odh::World w;require(w.restore_snapshot(b.world),"restore complete original source body");
 const auto source=b.household_roster.front();
 odh::Nbrs n{};n.version=62;odh::NeighborRecord record{};record.version=10;record.unknown3=0;record.person_mode=1;
 record.person_data=std::vector<std::int16_t>(256);record.neighbor_id=source.neighbor_id;record.guid=source.guid;n.entries.push_back({1,record});odh::NeighborContext neighbors(n);
 auto inv=inventory();inv.set_data(source.neighbor_id,999,37);auto state=travel_test::travel_state(b);state.protocol=odh::Ts1TravelProtocol::original_6e598;
 const auto member=state.permanent_roster.front();
 const odh::OriginalTravelIdentityResolver resolve=[&](odh::WorldEntityReference ref){require(ref==odh::WorldEntityReference{source.id,source.generation},"exact source owner");return member;};
 odh::Bhav program{0x8002,{{1,1,255,{17,0,0,0,0,0,0,0}},{0,254,255,{}}},{},odh::Bhav::Metadata{0,4,4,0}};
 odh::Vm vm({{odh::routine_key(4096),program}},w.vm_memory(),{},[&](const auto& frame,const auto& instruction,const auto& temps,const auto& dispatch){
  odh::Ts1TravelRequest request;request.destination=temps[0];request.thread_owner={source.id,source.generation};request.run_generation=dispatch.origin->run_generation();request.ticket=dispatch.ticket;request.source_frame=frame;request.source_instruction=instruction;request.departure_transit=temps[3];request.departure_members={{member,{source.id,source.generation}}};
  auto capture=odh::prepare_original_travel_capture(w,inv,neighbors,{source.id,source.generation},request.departure_members,temps,b.simulation->clock.hour,b.simulation->clock.minute,true,resolve);
  request.original=std::move(capture.receipt);require(inv.commit_replacement(std::move(capture.inventory)),"actual Generic17 atomic source capture");state.pending=std::move(request);
  return odh::PrimitiveOutcome{odh::PrimitiveDisposition::goto_true_next_tick,{}};
 });
 require(vm.start(odh::routine_key(4096),{source.id,source.id,source.id,0})==odh::VmStatus::running,"start source Generic17 VM");vm.temps()[0]=23;vm.temps()[1]=2;vm.temps()[2]=15;
 require(vm.run(1)==odh::VmStatus::next_tick,"actual original Generic17 deferred boundary");
 odh::EntityThreadControllers controllers(w,source.id);require(controllers.bind(vm),"bind source controllers");
 b.runtime_entities.front().vm=vm.snapshot();b.runtime_entities.front().controllers=controllers.snapshot();b.world=w.snapshot();
 for(auto& e:b.scheduler.entities)if(e.id==source.id){e.status=vm.status();e.steps=vm.snapshot().steps;e.run_generation=vm.run_generation();}
 b.neighbor_inventory=inv.snapshot();b.travel=state;odh::seal_ts1_travel_request(*b.travel,b);odh::validate_ts1_travel_bundle(b);
 const auto wire=odh::encode_session_bundle(b);require(odh::encode_session_bundle(odh::decode_session_bundle(wire))==wire,"sealed original request complete codec roundtrip");
 auto wrong=b;wrong.travel->pending->original->effects.front().values[0]=1;rejects([&]{odh::validate_ts1_travel_bundle(wrong);},"source receipt cannot invent zero needs");
 wrong=b;wrong.neighbor_inventory->inventory_by_id[source.neighbor_id].insert(wrong.neighbor_inventory->inventory_by_id[source.neighbor_id].begin(),{2,0,0});rejects([&]{odh::validate_ts1_travel_bundle(wrong);},"absent original zero cannot become a fabricated zero token");
 wrong=b;wrong.travel->pending->original->effects.front().source.avatar.generation++;rejects([&]{odh::validate_ts1_travel_bundle(wrong);},"sealed source cannot reuse numeric body identity");
 wrong=b;wrong.travel->pending->original->effects.front().source.member.character_fingerprint[0]++;rejects([&]{odh::validate_ts1_travel_bundle(wrong);},"sealed source resource substitution rejected");
 wrong=b;wrong.travel->pending->original->source_temps[0]++;rejects([&]{odh::validate_ts1_travel_bundle(wrong);},"recipe cannot change sealed source Temps");
}
void matrix_original_return(){
 auto home=travel_test::managed_home();home.neighbor_inventory=inventory().snapshot();
 auto origin=travel_test::travel_state(home);origin.protocol=odh::Ts1TravelProtocol::original_6e598;home.travel=origin;
 auto active=home;active.host.house_number=23;active.host.neighborhood_mode=2;
 active.travel->current_house=23;active.travel->current_mode=2;active.travel->family_activated=true;
 odh::World world;require(world.restore_snapshot(active.world),"return source World");
 const auto member=origin.permanent_roster.front();const auto id=active.host.runtime_avatar_id;
 auto bank=world.find(id)->person_data;bank.resize(256);bank[77]=123;bank[150]=789;
 require(world.replace_person_data(id,bank)&&world.set_motive(id,9,-17),"original source runtime256 and needs");
 odh::Nbrs source{};source.version=62;odh::NeighborRecord row{};row.version=4;row.person_mode=5;row.neighbor_id=member.neighbor_id;row.guid=member.guid;row.person_data=std::vector<std::int16_t>(80);source.entries.push_back({1,row});
 odh::NeighborContext neighbors(source);auto inv=inventory();
 odh::Bhav program{0x8002,{{1,1,255,{17,0,0,0,0,0,0,0}},{0,254,255,{}}},{},odh::Bhav::Metadata{0,4,4,0}};
 odh::Vm vm({{odh::routine_key(4096),program}},world.vm_memory(),{},[&](const auto& frame,const auto& instruction,const auto& temps,const auto& dispatch){
  odh::Ts1TravelRequest request;request.destination=-1;request.thread_owner={id,world.find(id)->generation};request.run_generation=dispatch.origin->run_generation();request.ticket=dispatch.ticket;request.source_frame=frame;request.source_instruction=instruction;request.departure_transit=0;request.departure_members={{member,request.thread_owner}};
  auto captured=odh::prepare_original_travel_capture(world,inv,neighbors,request.thread_owner,request.departure_members,temps,active.simulation->clock.hour,active.simulation->clock.minute,false,[&](auto ref){require(ref==request.thread_owner,"return exact source");return member;});
  request.original=std::move(captured.receipt);require(inv.commit_replacement(std::move(captured.inventory)),"return effects publication");active.travel->pending=std::move(request);
  return odh::PrimitiveOutcome{odh::PrimitiveDisposition::goto_true_next_tick,{}};
 });
 require(vm.start(odh::routine_key(4096),{id,id,id,0})==odh::VmStatus::running,"return actual dispatch start");vm.temps()[0]=-1;
 require(vm.run(1)==odh::VmStatus::next_tick,"return authored boundary");
 odh::EntityThreadControllers controllers(world,id);require(controllers.bind(vm),"return controllers");
 active.runtime_entities.front().vm=vm.snapshot();active.runtime_entities.front().controllers=controllers.snapshot();active.world=world.snapshot();active.neighbor_inventory=inv.snapshot();
 for(auto& e:active.scheduler.entities)if(e.id==id){e.status=vm.status();e.steps=vm.steps();e.run_generation=vm.run_generation();}
 odh::seal_ts1_travel_request(*active.travel,active);
 auto envelope=home;envelope.travel=active.travel;envelope.neighbor_inventory=active.neighbor_inventory;
 envelope.travel_home_session=odh::encode_session_bundle(home);envelope.travel_visited_session=odh::encode_session_bundle(active);
 const auto prepared=odh::prepare_ts1_travel_return(envelope);
 require(!prepared.requires_original_personal_persistence_resolution(),"original256 source workspace is not unresolved personal progress");
 const auto& restored=prepared.home();require(restored.world.entities.front().person_data==home.world.entities.front().person_data&&restored.world.entities.front().motives==home.world.entities.front().motives,"return preserves exact HOME body before authored21");
 require(restored.neighbor_inventory->inventory_by_id.at(member.neighbor_id).front().count==static_cast<std::uint16_t>(-17),"source need remains a carried effect for real HOME consumer");
 require(!prepared.retained_runtime_deltas().empty(),"source-only runtime words remain explicitly diagnosed");
}
void matrix_codec(){
 auto legacy=travel_test::managed_home();legacy.neighbor_inventory.reset();const auto old=odh::encode_session_bundle(legacy);require(odh::encode_session_bundle(odh::decode_session_bundle(old))==old,"legacy absent NINV remains byte identical");
 auto original=legacy;original.neighbor_inventory=odh::NeighborInventorySnapshot{2,{{41,{{19,123,0},{2,999,65535},{19,123,0}}}}};
 auto state=travel_test::travel_state(original);state.protocol=odh::Ts1TravelProtocol::original_6e598;original.travel=state;
 const auto encoded=odh::encode_session_bundle(original);const auto reopened=odh::decode_session_bundle(encoded);require(reopened.travel->protocol==odh::Ts1TravelProtocol::original_6e598&&odh::encode_session_bundle(reopened)==encoded,"NINV2/TRIP3 exact roundtrip preserves unknowns/order/duplicates/zero");
 auto mixed=original;mixed.neighbor_inventory->version=1;rejects([&]{odh::encode_session_bundle(mixed);},"mixed producer/consumer profile rejected");
 mixed=original;mixed.travel->protocol=odh::Ts1TravelProtocol::legacy_pin;rejects([&]{odh::encode_session_bundle(mixed);},"legacy TRIP cannot reinterpret original NINV2");
}
}
int main(){try{for(const auto& test:std::vector<std::pair<const char*,void(*)()>>{{"inventory",matrix_inventory},{"capture_reuse",matrix_capture_and_reuse},{"live_effects",matrix_effects},{"sealed_dispatch",matrix_sealed_dispatch},{"original_return",matrix_original_return},{"codec_migration",matrix_codec}}){test.second();std::cout<<test.first<<": PASS\n";}return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
