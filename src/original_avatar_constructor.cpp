// SPDX-License-Identifier: MPL-2.0
// Behavioral adaptation: original6e598 459493/459797/459a54/431c90/423e3b.
#include "odh/original_avatar_constructor.hpp"
#include "odh/native_personal_state.hpp"
#include <algorithm>
namespace odh {
namespace {
bool unscaled_interests(const std::vector<std::int16_t>& bank){for(unsigned i=46;i<=53;++i)if(bank.at(i)>10)return false;return true;}
void randomize_personality(std::vector<std::int16_t>& bank,RandomState& random){
 //459c08's allocation-without-replacement categories and rejection draws.
 // Use the single native runtime RNG owner; never create/reseed a side RNG.
 std::size_t draws=0;
 const auto draw=[&](unsigned range){if(++draws>65536)throw FormatError("original personality random draw bound exceeded");return static_cast<unsigned>(random.next(32768)%range);};
 const auto fill=[&](std::array<unsigned,3> remaining,const std::vector<unsigned>& fields){
  for(const auto field:fields){unsigned category;do{category=draw(3);}while(!remaining[category]);--remaining[category];
   const auto value=category==0?draw(4):category==1?draw(3)+4:draw(4)+7;bank.at(field)=static_cast<std::int16_t>(value*100);
  }
 };
 fill({4,3,3},{46,47,48,49,50,51,52,53,54,55});fill({1,2,2},{13,14,16,20,26});
}
bool current(const World& world,const NpcAvatarRequest& request,WorldEntityReference ref){
 const auto* e=world.resolve_reference(ref);if(!e||e->dead||e->ui_ghost||e->kind!=EntityKind::avatar||!e->definition||e->definition->guid!=request.guid())return false;
 if(request.original_load_current)return request.original_load_current();
 const auto* caller=world.find(request.caller);return caller&&!caller->dead&&!caller->ui_ghost&&caller->generation==request.caller_generation;
}
void reset_base_bank(World& world,WorldEntityReference ref,Vm& thread){
 const auto* e=world.resolve_reference(ref);if(!e||e->position||e->attachment)throw FormatError("original fresh base reset requires its exact OOW body");
 constexpr std::array<std::int16_t,8> retained{1,21,24,29,41,45,46,47};std::array<std::int16_t,8> values{};
 for(std::size_t n=0;n<retained.size();++n){const auto f=e->object_words.find(retained[n]);if(f==e->object_words.end())throw FormatError("original fresh base reset lacks retained ObjectData");values[n]=f->second;}
 const auto attributes=e->attributes.size();
 for(std::int16_t i=0;i<72;++i)if(i!=2&&i!=3&&i!=11&&i!=67&&!world.declare_object_word(ref.id,i,0))throw FormatError("original base reset bank capacity");
 for(std::size_t n=0;n<retained.size();++n)if(!world.declare_object_word(ref.id,retained[n],values[n]))throw FormatError("original base retained word publication");
 if(!world.declare_object_word(ref.id,1,static_cast<std::int16_t>((values[0]%8+8)%8)))throw FormatError("original base direction normalization");
 VmFrame frame;frame.context={ref.id,ref.id,0,0};if(!world.bind_context_live(frame.context))throw FormatError("original base exact attribute owner missing");
 for(std::size_t i=0;i<attributes;++i)if(i>32767||!world.write(frame,0,static_cast<std::int16_t>(i),0))throw FormatError("original base attribute reset failed");
 std::fill_n(thread.temps().begin(),8,0);
}
}
NpcAvatarRequestResult resolve_original_load_avatar_request(std::int16_t nid,const NeighborContext& neighbors,
 const World& world,bool residential,std::function<bool()> source_current){
 NpcAvatarRequestResult result;if(!source_current||!source_current()){result.diagnostic="original frontend source authority unavailable";return result;}
 const auto* neighbor=neighbors.by_id(nid);if(!neighbor){result.status=NpcAvatarRequestStatus::missing_neighbor;result.diagnostic="original frontend selected neighbor absent";return result;}
 unsigned matches=0;for(const auto& e:neighbors.source().entries)if(e.record&&e.record->guid==neighbor->guid)++matches;
 if(matches!=1){result.diagnostic="original frontend GUID is ambiguous";return result;}
 try{(void)original_neighbor_seed_projection(*neighbor);}catch(const FormatError& e){result.diagnostic=e.what();return result;}
 for(const auto& e:world.snapshot().entities)if(!e.dead&&e.kind==EntityKind::avatar&&((e.definition&&e.definition->guid==neighbor->guid)||(e.person_data.size()>31&&e.person_data[31]==nid))){result.status=NpcAvatarRequestStatus::duplicate;result.diagnostic="original frontend identity is already live";return result;}
 NpcAvatarRequest request;request.creation={neighbor->guid,static_cast<std::uint8_t>(CreateObjectPosition::out_of_world),0,0,0};
 request.original_semantics=true;request.original_residential=residential;request.original_load_current=std::move(source_current);
 request.requested_neighbor=*neighbor;request.constructor_neighbor=*neighbor;result.request=std::move(request);result.status=NpcAvatarRequestStatus::ready;return result;
}
NpcAvatarStartupResult initialize_original_avatar(World& world,Vm& thread,std::int16_t id,CodeOwner owner,
 const NpcAvatarRequest& request,const EntryPointTable& entries,const NpcAvatarBodyInputs& inputs,RandomState& random,
 std::uint64_t budget,const VmPrimitive& primitive,bool scheduler_running_now,const std::function<bool()>& self_deleted){
 NpcAvatarStartupResult result;const auto fail=[&](std::string message){result.diagnostic=std::move(message);return result;};
 const auto* initial=world.find(id);if(!initial||!request.original_semantics||!request.original_residential||!request.constructor_neighbor||!inputs.birth_time||
  initial->person_data.size()!=256||initial->motives.size()!=16||
  initial->position||initial->attachment||thread.dispatch_active()||thread.status()!=VmStatus::ready||thread.run_generation()!=0||!owner||!budget||entries.entries.size()<=8||
  !request.original_main_prepared||!request.original_initial_tick||!request.original_lookup_flags||!request.original_curves_ready||!request.original_guarded_init||!request.original_definition_version)return fail("original initializer lacks its fresh body, explicit source profile or native lifecycle consumers");
 const WorldEntityReference ref{id,initial->generation};
 try{
  if(!current(world,request,ref))return fail("original constructor source expired");
  (void)original_neighbor_seed_projection(*request.constructor_neighbor);
  // Validate the installed visual resources without applying the PIN body's
  // all-needs100/person7=1000 defaults or consuming a name-selection draw.
  result.body=derive_avatar_body_descriptor(world,id,random,inputs.body,inputs.global_walk,inputs.global_swim,inputs.object_walk);
  result.body->selected_name=request.constructor_neighbor->name;
  auto load=[&]{auto bank=world.find(id)->person_data;apply_original_neighbor_personal(*request.constructor_neighbor,bank);
   if(!world.replace_person_data(id,bank))throw FormatError("original selective loader bank publication failed");};
  // ClassInit459797 consumes the same serialized/selective source first.
  load();auto bank=world.find(id)->person_data;
  if(unscaled_interests(bank)){randomize_personality(bank,random);if(!world.replace_person_data(id,bank))throw FormatError("original ClassInit personality publication failed");}
  const auto& birth=*inputs.birth_time;
  for(const auto field:std::array<std::pair<std::int16_t,std::int16_t>,5>{{{45,birth.year},{46,birth.month},{47,birth.day},{24,birth.hour},{21,birth.minute}}})
   if(!world.declare_object_word(id,field.first,field.second))throw FormatError("original pre-Init birth metadata publication failed");
  // Virtual40: retain the three native status words across the256-word reset.
  bank=world.find(id)->person_data;std::vector<std::int16_t> cleared(256);
  for(const unsigned index:{32,34,35})cleared[index]=bank[index];
  if(!world.replace_person_data(id,cleared))throw FormatError("original person reset bank publication failed");
  load();reset_base_bank(world,ref,thread);
  if(!world.declare_object_word(id,58,*request.original_definition_version))throw FormatError("original definition version publication failed");
  const auto main=entries.entries[1].action;
  if(main<=255||!thread.has_routine(routine_key(main),owner))return fail("original constructor Main resource is absent");
  // Common Create42 captures these inputs before construction. Resolve the
  // raw StackObject at the actual Main preparation boundary, never bind an
  // earlier generation or reinterpret it as a NeighborID.
  const auto* main_stack=world.find(request.main_stack_object);
  const auto stack_id=main_stack?main_stack->id:std::int16_t{0};
  if(thread.start_after_reset(routine_key(main),{id,id,stack_id,owner},{request.main_param,0,0,0},true)!=VmStatus::running)return fail("original constructor Main preparation failed");
  result.main_started=true;std::string prepare_diagnostic;
  if(!request.original_main_prepared(ref,thread,prepare_diagnostic))return fail("original Main owner binding: "+prepare_diagnostic);
  std::uint64_t initial_main_steps=0;
  const auto init=[&](const char* phase){
   if(!current(world,request,ref))throw FormatError("original Init lost source identity");
   const auto routine=entries.entries[0].action;if(!routine){result.stages.push_back({phase,0,VmStatus::ready,0,"absent original entry"});return;}
   const auto program=thread.shared_program_for_owner(routine_key(routine).scope==RoutineScope::global?0:owner);
   if(!program)throw FormatError("original Init resource owner unavailable");const auto found=program->routines().find(routine_key(routine));
   if(found==program->routines().end())throw FormatError("original Init routine unavailable");const auto& root=found->second;
   std::optional<std::size_t> count;if(root.version==0x8000)count=4;else if(root.version==0x8001)count=std::min<std::size_t>(root.header[1],4);else if(root.version==0x8002&&root.metadata)count=root.metadata->arguments;
   if(!count||*count>8)throw FormatError("original Init native TEMP-default argument metadata unavailable");
   const std::vector<std::int16_t> arguments(thread.temps().begin(),thread.temps().begin()+*count);
   if(result.steps>=budget||initial_main_steps>=budget-result.steps)throw FormatError("original initializer instruction bound exhausted");
   const auto operation=[&]()->bool{
   const auto checked=thread.evaluate_entry_point_check(routine_key(routine),{id,id,0,owner},arguments,budget-result.steps-initial_main_steps,true,primitive,{},static_cast<bool>(self_deleted));
   result.steps+=checked.steps;result.stages.push_back({phase,routine,checked.status,checked.steps,checked.diagnostic});
   if(checked.lifecycle_deleted&&self_deleted&&self_deleted()){result.self_deleted=true;return true;}
   if(!checked.started||(checked.status!=VmStatus::returned_true&&checked.status!=VmStatus::returned_false))throw FormatError("original synchronous Init failed: "+checked.diagnostic);
    return true;
   };
   if(!request.original_guarded_init(ref,thread,owner,operation))throw FormatError("original Init lifecycle authority rejected operation");
  };
  init("base_init0");if(result.self_deleted){result.completed=true;return result;}
  std::string diagnostic;
  const auto before_main=thread.steps();
  if(result.steps>=budget||!request.original_initial_tick(ref,thread,budget-result.steps,diagnostic))return fail("original Simulate0: "+diagnostic);
  if(thread.steps()<before_main)return fail("original Simulate0 replaced its Main run");
  initial_main_steps=thread.steps()-before_main;
  result.stages.push_back({"initial_main_tick0",main,thread.status(),initial_main_steps,{}});
  if(self_deleted&&self_deleted()){result.completed=true;result.self_deleted=true;return result;}
  if(!current(world,request,ref)||!request.original_lookup_flags(ref,diagnostic))return fail("original base lookup publication: "+diagnostic);
  init("person_init0");if(result.self_deleted){result.completed=true;return result;}
  bank=world.find(id)->person_data;
  if(bank[25]!=1966)load();else {bank[25]=0;if(!world.replace_person_data(id,bank))throw FormatError("original personal reload sentinel publication failed");}
  bank=world.find(id)->person_data;if(!*request.original_residential&&bank[32]!=2)bank[32]=1;
  if(unscaled_interests(bank))randomize_personality(bank,random);
  if(!world.replace_person_data(id,bank))throw FormatError("original person role/personality publication failed");
  //459e8f installs interaction score curves501/503 (not swim animations).
  if(!request.original_curves_ready(ref,diagnostic))return fail("original interaction curves: "+diagnostic);
  result.body=derive_avatar_body_descriptor(world,id,random,inputs.body,inputs.global_walk,inputs.global_swim,inputs.object_walk);
  result.body->selected_name=request.constructor_neighbor->name;
  if(!current(world,request,ref))return fail("original construction lost its exact source after Init");
  //00440b6d publishes selector1 only after the complete virtual40 initializer.
  if(!world.publish_lookup_selector(ref,1,true))return fail("original final scheduling selector publication failed");
  (void)scheduler_running_now;result.completed=true;
 }catch(const std::exception& e){result.diagnostic=e.what();}
 return result;
}
} // namespace odh
