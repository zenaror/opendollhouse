#include "travel_session_fixture.hpp"
#include <iostream>
using travel_test::require;
template<class F>void rejects(F f,const char* why){try{f();}catch(const odh::FormatError&){return;}throw std::runtime_error(why);}
int main(){try{
 auto home=travel_test::managed_home();auto state=travel_test::travel_state(home);
 {
  auto source=travel_test::managed_home();source.type_attributes=odh::TypeAttributesSnapshot{1,{{700,{-1,2}}}};
  auto visited=source;visited.host.house_number=23;visited.host.neighborhood_mode=2;
  auto visiting=travel_test::travel_state(source);visiting.current_house=23;visiting.current_mode=2;
  visited.travel=visiting;visited.type_attributes->by_guid[700][0]=99;
  auto envelope=source;envelope.travel=visiting;envelope.type_attributes=visited.type_attributes;
  envelope.travel_home_session=odh::encode_session_bundle(source);envelope.travel_visited_session=odh::encode_session_bundle(visited);
  const auto encoded=odh::encode_session_bundle(envelope);const auto decoded=odh::decode_session_bundle(encoded);
  require(decoded.type_attributes->by_guid.at(700)[0]==99&&
      decoded.travel_home_session==envelope.travel_home_session&&odh::encode_session_bundle(decoded)==encoded,
      "travel envelope preserves current shared type attributes and exact suspended HOME bytes");
  auto mismatch=envelope;mismatch.type_attributes->by_guid[700][0]=100;
  rejects([&]{odh::encode_session_bundle(mismatch);},"outer shared type attributes must match actual visited snapshot");
  auto dropped=visited;dropped.type_attributes.reset();mismatch=envelope;
  mismatch.travel_visited_session=odh::encode_session_bundle(dropped);
  rejects([&]{odh::encode_session_bundle(mismatch);},"visited state cannot discard originating shared type attributes");
 }
 const auto wire=odh::encode_ts1_travel_state(state);require(odh::encode_ts1_travel_state(odh::decode_ts1_travel_state(wire))==wire,"travel state roundtrip");
 auto wrong=state;wrong.travelers.front().neighbor_id++;rejects([&]{odh::validate_ts1_travel_state(wrong);},"foreign traveler rejected");
 wrong=state;wrong.caller_guid++;rejects([&]{odh::validate_ts1_travel_state(wrong);},"foreign caller rejected");
 auto truncated=wire;truncated.pop_back();rejects([&]{odh::decode_ts1_travel_state(truncated);},"truncated TRIP rejected");
 home.travel=state;auto no_inventory=home;no_inventory.neighbor_inventory.reset();rejects([&]{odh::encode_session_bundle(no_inventory);},"trip requires inventory");
 auto bad_lease=home;bad_lease.family_lease->occupancy_epoch++;rejects([&]{odh::encode_session_bundle(bad_lease);},"trip rejects stale home lease");
 // Actual Generic17 dispatch, rather than a fabricated opcode whitelist.
 odh::World world;require(world.restore_snapshot(home.world),"restore source world");
 odh::Instruction change{1,1,255,{17,0,0,0,0,0,0,0}};
 odh::Bhav program{0x8002,{change,{0,254,255,{}}},{},odh::Bhav::Metadata{0,4,4,0}};
 odh::Vm vm({{odh::routine_key(4096),program}},world.vm_memory(),{},[&](const auto& f,const auto& ins,const auto&,const auto& dispatch){
  odh::Ts1TravelRequest r;r.destination=23;r.thread_owner={1,world.find(1)->generation};r.run_generation=dispatch.origin->run_generation();r.ticket=dispatch.ticket;r.source_frame=f;r.source_instruction=ins;state.pending=r;return odh::PrimitiveOutcome{odh::PrimitiveDisposition::goto_true_next_tick,{}};});
 require(vm.start(odh::routine_key(4096),{1,1,1,0})==odh::VmStatus::running,"start actual travel VM");require(vm.run(1)==odh::VmStatus::next_tick,"deferred travel boundary");
 home.runtime_entities.front().vm=vm.snapshot();home.travel=state;odh::seal_ts1_travel_request(*home.travel,home);odh::validate_ts1_travel_bundle(home);
 auto stale=home;stale.travel->pending->ticket++;rejects([&]{odh::validate_ts1_travel_bundle(stale);},"stale original child ticket rejected");
 stale=home;stale.runtime_entities.front().vm.stack.back().pc=0;rejects([&]{odh::validate_ts1_travel_bundle(stale);},"original child continuation mismatch rejected");
 stale=home;stale.travel->pending->thread_owner.generation++;rejects([&]{odh::validate_ts1_travel_bundle(stale);},"reused owner identity rejected");
 {
  const auto legacy=odh::encode_ts1_travel_state(*home.travel);
  require(legacy.front()==1&&odh::encode_ts1_travel_state(odh::decode_ts1_travel_state(legacy))==legacy,
      "legacy pending request retains exact v1 bytes and historical party semantics");
  const auto envelope=travel_test::departure_party_envelope();
  const auto active=odh::decode_session_bundle(*envelope.travel_visited_session);
  const auto modern=odh::encode_ts1_travel_state(*active.travel);
  require(modern.front()==2&&odh::encode_ts1_travel_state(odh::decode_ts1_travel_state(modern))==modern,
      "explicit pending departure group has exact v2 roundtrip");
  const auto bytes=odh::encode_session_bundle(envelope);const auto reopened=odh::decode_session_bundle(bytes);
  require(odh::encode_session_bundle(reopened)==bytes&&active.household_roster.size()==1&&active.runtime_entities.size()==1&&
      active.npc_avatars.size()==1&&active.travel->travelers.size()==1&&active.travel->pending->departure_members.size()==2&&
      reopened.household_roster.size()==3,"pending two-person return preserves one admitted resident, independent visitor and full HOME roster");
  require(odh::ts1_travel_departure_members(*active.travel).size()==2&&odh::ts1_travel_departure_transit(*active.travel)==75&&
      odh::resolve_ts1_travel_departure_avatar(active,active.travel->pending->departure_members[1].member).id==10,
      "common departure resolver authenticates genuine visiting relative without promotion");
  const auto reject_change=[&](const auto& change,const char* why){auto bad=active;change(bad);rejects([&]{odh::validate_ts1_travel_bundle(bad);},why);};
  reject_change([](auto& b){b.travel->pending->departure_members.pop_back();},"source-defined same-family visitor cannot be omitted");
  reject_change([](auto& b){b.travel->pending->departure_members.push_back(b.travel->pending->departure_members.back());},"duplicate passenger rejected");
  reject_change([](auto& b){b.travel->pending->departure_members[1].avatar.generation++;},"reused numeric visitor ID cannot substitute generation");
  reject_change([](auto& b){b.travel->pending->departure_members[1].member.character_fingerprint[0]++;},"foreign permanent resource substitution rejected");
  reject_change([](auto& b){b.npc_avatars.front().origin.character_fingerprint[0]++;},"visitor carrier must retain exact permanent character hash");
  reject_change([](auto& b){b.npc_avatars.front().origin.source="GameData/Objects/WorldNPC.far";b.npc_avatars.front().origin.asset="Service.iff";},"service resource cannot masquerade as permanent relative");
  reject_change([](auto& b){b.npc_avatars.front().origin.neighbor_id=0;},"resource-only service NPC has no family departure identity");
  reject_change([](auto& b){b.npc_avatars.clear();},"World family word alone grants no passenger provenance");
  reject_change([](auto& b){b.npc_avatars.push_back(b.npc_avatars.front());},"ambiguous NPC runtime carrier rejected");
  reject_change([](auto& b){b.world.entities.back().person_data[61]=22;},"visitor must still belong to actual source family");
  reject_change([](auto& b){b.world.entities.back().dead=true;},"dead source body is not a live passenger");
  reject_change([](auto& b){b.world.entities.back().motives.pop_back();},"complete source motive bank required");
  reject_change([](auto& b){b.runtime_entities.push_back(b.runtime_entities.front());},"resident runtime carrier must be unique");
  reject_change([](auto& b){b.travel->pending->departure_transit=0;},"captured transit must match source global34");
  reject_change([](auto& b){b.runtime_entities.front().vm.temps[0]=23;},"sealed destination cannot be substituted");
  reject_change([](auto& b){b.travel->pending->ticket++;},"new party does not weaken source continuation ticket proof");
  reject_change([](auto& b){b.travel->travelers.push_back(b.travel->permanent_roster[1]);},"departure visitor cannot silently enlarge current HHRD subset");
  auto malformed=modern;malformed.pop_back();rejects([&]{odh::decode_ts1_travel_state(malformed);},"truncated v2 passenger reference rejected");
  malformed=modern;malformed.push_back(0);rejects([&]{odh::decode_ts1_travel_state(malformed);},"unclaimed v2 trailing bytes rejected");
  auto legacy_shape=*active.travel;legacy_shape.pending->departure_members.clear();legacy_shape.pending->departure_transit.reset();
  const auto common_size=odh::encode_ts1_travel_state(legacy_shape).size();
  for(const std::uint8_t count:{std::uint8_t{0},std::uint8_t{9}}){malformed=modern;malformed[common_size+2]=count;
   rejects([&]{odh::decode_ts1_travel_state(malformed);},"empty and oversized v2 passenger groups rejected before allocation");}
  malformed=modern;malformed.front()=3;rejects([&]{odh::decode_ts1_travel_state(malformed);},"unknown travel version remains unsupported");
  malformed=wire;malformed.front()=2;rejects([&]{odh::decode_ts1_travel_state(malformed);},"v2 without explicit pending group rejected");
  auto bad_state=*active.travel;bad_state.pending->departure_transit.reset();rejects([&]{odh::encode_ts1_travel_state(bad_state);},"modern party cannot lose its transit marker");
  bad_state=*active.travel;std::swap(bad_state.pending->departure_members[0],bad_state.pending->departure_members[1]);rejects([&]{odh::validate_ts1_travel_state(bad_state);},"first passenger must be bound source Caller");
 }
 std::cout<<"TS1 travel state tests passed\n";return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
