#pragma once
#include "odh/session_bundle.hpp"
#include <stdexcept>
namespace travel_test {
inline void require(bool v,const char* m){if(!v)throw std::runtime_error(m);}
inline odh::SessionBundle travel_bundle_fixture() {
    odh::World world;
    require(world.create_saved_entity(1, odh::EntityKind::avatar, 123456,
        {}, std::vector<std::int16_t>(80), std::vector<std::int16_t>(16), {}, {}, 0), "create fixture actor");
    require(world.set_person_data(1,31,41) && world.set_person_data(1,61,21),
        "bind fixture person neighbor and family identity");
    odh::Vm vm(odh::RoutineSet{});
    odh::EntityThreadControllers controllers(world, 1);
    require(controllers.bind(vm), "bind fixture controllers");
    odh::EntityVmScheduler scheduler(4);
    require(scheduler.add(1, vm, [](std::uint64_t) { return true; },
        [] { return true; }, [] { return true; }), "register fixture VM");
    odh::SessionBundle bundle;
    bundle.generic_execution = true;
    odh::SessionHouseholdMember member;
    member.id = 1; member.generation = world.find(1)->generation;
    member.guid = 123456; member.neighbor_id = 41; member.object_definition_id = 128;
    member.character_asset = "PlayerData/Characters/User1.iff";
    member.character_fingerprint.fill(0x31);
    bundle.household_roster.push_back(member);
    bundle.resources.language_code = 7;
    bundle.resources.expansion_mask = 0x7f;
    bundle.resources.resource_fingerprint.fill(0x5a);
    bundle.host.neighborhood_mode = 1;
    bundle.host.house_number = 7;
    bundle.host.family_chunk_id = 21;
    bundle.host.family_number = 12;
    bundle.host.resident_guid = 123456;
    bundle.host.resident_neighbor_id = 41;
    bundle.host.runtime_avatar_id = 1;
    bundle.host.random_seed = 1234;
    bundle.host.family_budget = 7000;
    bundle.host.global_values = {{9, 21}};
    bundle.world = world.snapshot();
    bundle.relationships = odh::RelationshipState{}.snapshot();
    odh::SessionRuntimeEntity runtime;
    runtime.id = 1; runtime.vm = vm.snapshot(); runtime.controllers = controllers.snapshot();
    bundle.runtime_entities.push_back(std::move(runtime));
    bundle.scheduler = scheduler.snapshot();
    odh::SessionSimulationState simulation;
    simulation.clock = odh::Ts1Clock{}.snapshot();
    odh::Ts1MotiveSnapshot decay;
    decay.fractions = {{11,22,33,44,55,66,77}};
    simulation.avatars.push_back({1, decay});
    bundle.simulation = simulation;
    return bundle;
}
inline odh::Ts1TravelState travel_state(const odh::SessionBundle& b){
 odh::Ts1TravelState s;s.home_house=b.host.house_number;s.current_house=s.home_house;s.home_mode=b.host.neighborhood_mode;s.current_mode=s.home_mode;s.family_chunk_id=b.host.family_chunk_id;s.family_number=b.host.family_number;s.occupancy_epoch=b.family_lease->occupancy_epoch;s.neighborhood_identity=b.family_lease->neighborhood_identity;s.family_identity=b.family_lease->family_identity;
 for(const auto& m:b.household_roster)s.permanent_roster.push_back({m.guid,m.neighbor_id,m.object_definition_id,m.character_asset,m.character_fingerprint});s.travelers=s.permanent_roster;s.caller_guid=b.host.resident_guid;s.caller_neighbor_id=b.host.resident_neighbor_id;return s;
}
inline odh::SessionBundle managed_home(){auto b=travel_bundle_fixture();odh::SessionFamilyLease l;l.neighborhood_identity.fill(1);l.family_identity.fill(2);l.occupancy_epoch=1;l.family_chunk_id=b.host.family_chunk_id;b.family_lease=l;b.neighbor_inventory=odh::NeighborInventorySnapshot{};return b;}
inline odh::SessionBundle pending_travel_home(odh::SessionBundle home,odh::Ts1TravelState state){
 odh::World world;require(world.restore_snapshot(home.world),"restore pending departure world");const auto actor=home.host.runtime_avatar_id;
 odh::Bhav program{0x8002,{{1,1,255,{17,0,0,0,0,0,0,0}},{0,254,255,{}}},{},odh::Bhav::Metadata{0,4,4,0}};
 odh::Vm vm({{odh::routine_key(4096),program}},world.vm_memory(),{},[&](const auto& frame,const auto& instruction,const auto&,const auto& dispatch){
  odh::Ts1TravelRequest request;request.destination=23;request.thread_owner={actor,world.find(actor)->generation};request.run_generation=dispatch.origin->run_generation();request.ticket=dispatch.ticket;request.source_frame=frame;request.source_instruction=instruction;state.pending=request;return odh::PrimitiveOutcome{odh::PrimitiveDisposition::goto_true_next_tick,{}};
 });
 require(vm.start(odh::routine_key(4096),{actor,actor,actor,0})==odh::VmStatus::running&&vm.run(1)==odh::VmStatus::next_tick,"actual pending departure Generic17 boundary");
 odh::EntityThreadControllers controllers(world,actor);require(controllers.bind(vm),"bind pending departure controllers");
 for(auto& entity:home.runtime_entities)if(entity.id==actor){entity.vm=vm.snapshot();entity.controllers=controllers.snapshot();}
 for(auto& entity:home.scheduler.entities)if(entity.id==actor){entity.status=vm.status();entity.steps=vm.snapshot().steps;entity.run_generation=vm.run_generation();}
 home.travel=state;odh::seal_ts1_travel_request(*home.travel,home);return home;
}

// Three permanent residents at HOME; one admitted traveler and one genuine
// relative materialized as an independent visitor in the destination.
inline odh::SessionBundle departure_party_envelope(){
 auto home=managed_home();home.world.entities.front().person_data.resize(88);home.world.word_count+=8;
 for(std::int16_t id:{2,3}){
  odh::World world;require(world.restore_snapshot(home.world)&&world.create_saved_entity(id,odh::EntityKind::avatar,
      static_cast<std::uint32_t>(123455+id),{},std::vector<std::int16_t>(88),std::vector<std::int16_t>(16),{},{},0),"create permanent relative");
  require(world.set_person_data(id,31,static_cast<std::int16_t>(40+id))&&world.set_person_data(id,61,21),"bind permanent relative");
  auto member=home.household_roster.front();member.id=id;member.generation=world.find(id)->generation;
  member.guid=123455+id;member.neighbor_id=40+id;member.character_asset="PlayerData/Characters/User"+std::to_string(id)+".iff";member.character_fingerprint.fill(static_cast<std::uint8_t>(0x30+id));
  home.household_roster.push_back(member);home.world=world.snapshot();
  odh::Vm vm(odh::RoutineSet{});odh::EntityThreadControllers controllers(world,id);require(controllers.bind(vm),"bind permanent relative runtime");
  odh::SessionRuntimeEntity runtime;runtime.id=id;runtime.vm=vm.snapshot();runtime.controllers=controllers.snapshot();home.runtime_entities.push_back(runtime);
  home.scheduler.entities.push_back({id,vm.run_generation(),vm.snapshot().steps,vm.status(),false,false,false});
  home.simulation->avatars.push_back({id,home.simulation->avatars.front().decay});
 }
 auto state=travel_state(home);state.current_house=23;state.current_mode=2;state.family_activated=true;
 state.travelers={state.permanent_roster.front()};
 odh::World world;
 for(std::int16_t id:{9,10}){
  const auto& identity=state.permanent_roster[static_cast<std::size_t>(id-9)];
  require(world.create_saved_entity(id,odh::EntityKind::avatar,identity.guid,{},std::vector<std::int16_t>(88),std::vector<std::int16_t>(16),{},{},0)&&
      world.set_person_data(id,31,identity.neighbor_id)&&world.set_person_data(id,61,21),"create source admitted Sim and visiting relative");
 }
 constexpr unsigned motives[7]={9,6,5,15,7,8,14};
 odh::NeighborInventory inventory(odh::Ngbh{});
 for(std::int16_t id:{9,10})for(unsigned key=0;key<7;++key){
  const auto value=static_cast<std::int16_t>(-80+id+key);require(world.set_motive(id,motives[key],value),"set departure source motive");
  inventory.set_data(static_cast<std::int16_t>(id==9?41:42),key,static_cast<std::uint16_t>(value));
 }
 odh::Bhav program{0x8002,{{1,1,255,{17,0,0,0,0,0,0,0}},{0,254,255,{}}},{},odh::Bhav::Metadata{0,4,4,0}};
 odh::Vm vm({{odh::routine_key(4096),program}},world.vm_memory(),{},[&](const auto& frame,const auto& instruction,const auto&,const auto& dispatch){
  odh::Ts1TravelRequest request;request.destination=-1;request.thread_owner={9,world.find(9)->generation};
  request.run_generation=dispatch.origin->run_generation();request.ticket=dispatch.ticket;request.source_frame=frame;request.source_instruction=instruction;
  request.departure_transit=75;
  for(std::int16_t id:{9,10})request.departure_members.push_back({state.permanent_roster[static_cast<std::size_t>(id-9)],{id,world.find(id)->generation}});
  state.pending=std::move(request);return odh::PrimitiveOutcome{odh::PrimitiveDisposition::goto_true_next_tick,{}};
 });
 require(vm.start(odh::routine_key(4096),{9,9,9,0})==odh::VmStatus::running,"start actual two-person departure");
 vm.temps()[0]=-1;require(vm.run(1)==odh::VmStatus::next_tick,"defer actual two-person departure");
 odh::EntityThreadControllers controllers(world,9);require(controllers.bind(vm),"bind admitted source controller");
 odh::Vm npc_vm(odh::RoutineSet{});odh::EntityThreadControllers npc_controllers(world,10);require(npc_controllers.bind(npc_vm),"bind visitor source controller");
 auto active=home;active.host.house_number=23;active.host.neighborhood_mode=2;active.host.runtime_avatar_id=9;active.host.family_budget=6975;
 active.host.global_values={{9,21},{34,75}};active.world=world.snapshot();
 active.household_roster.resize(1);active.household_roster.front().id=9;active.household_roster.front().generation=world.find(9)->generation;
 odh::SessionRuntimeEntity resident;resident.id=9;resident.vm=vm.snapshot();resident.controllers=controllers.snapshot();active.runtime_entities={resident};
 active.simulation->avatars.resize(1);active.simulation->avatars.front().id=9;
 odh::SessionNpcAvatar npc;const auto& relative=state.permanent_roster[1];
 npc.origin={10,world.find(10)->generation,relative.guid,relative.neighbor_id,relative.character_asset,"",relative.definition_chunk_id,relative.character_fingerprint};
 npc.primary_owner=relative.guid;npc.runtime.id=10;npc.runtime.vm=npc_vm.snapshot();npc.runtime.controllers=npc_controllers.snapshot();
 odh::SessionResourceCodeOwner owner;owner.owner=relative.guid;owner.resource_guid=relative.guid;owner.source=relative.character_asset;
 owner.definition_chunk_id=relative.definition_chunk_id;owner.root_routine=4096;owner.bank_kind=odh::SessionCodeBankKind::complete_private;
 owner.identity=odh::SessionCodeOwnerIdentity::resource_guid;owner.routine_fingerprint.fill(0x51);npc.runtime.resource_owners.push_back(owner);active.npc_avatars={npc};
 active.scheduler.entities={{9,vm.run_generation(),vm.snapshot().steps,vm.status(),false,false,false},
     {10,npc_vm.run_generation(),npc_vm.snapshot().steps,npc_vm.status(),false,false,false}};
 active.neighbor_inventory=inventory.snapshot();active.travel=state;odh::seal_ts1_travel_request(*active.travel,active);
 auto envelope=home;envelope.travel=active.travel;envelope.neighbor_inventory=active.neighbor_inventory;envelope.host.family_budget=active.host.family_budget;
 envelope.travel_home_session=odh::encode_session_bundle(home);envelope.travel_visited_session=odh::encode_session_bundle(active);
 (void)odh::encode_session_bundle(envelope);return envelope;
}

}
