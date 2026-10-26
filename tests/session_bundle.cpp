#include "odh/session_bundle.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value,const char* message) {
    if(!value)throw std::runtime_error(message);
}
template<class F> void rejects(F&& operation,const char* message) {
    try{operation();}catch(const odh::FormatError&){return;}
    throw std::runtime_error(message);
}

odh::SessionBundle fixture() {
    odh::World world;
    require(world.create(1,odh::EntityKind::avatar)&&world.create(2,odh::EntityKind::avatar),
            "create two actors");
    odh::Vm first(odh::RoutineSet{}),second(odh::RoutineSet{});
    odh::EntityThreadControllers first_controllers(world,1),second_controllers(world,2);
    require(first_controllers.bind(first)&&second_controllers.bind(second),"bind controllers");
    odh::EntityVmScheduler scheduler(4);
    require(scheduler.add(1,first,[](std::uint64_t){return true;},[]{return true;},[]{return true;})&&
            scheduler.add(2,second,[](std::uint64_t){return true;},[]{return true;},[]{return true;}),
            "register two actor VMs");
    odh::RelationshipState relationships;
    odh::SessionBundle bundle;
    bundle.resources.language_code=7;
    bundle.resources.expansion_mask=0x7f;
    bundle.resources.resource_fingerprint.fill(0x5a);
    bundle.host.neighborhood_mode=1;
    bundle.host.house_number=7;
    bundle.host.family_chunk_id=21;
    bundle.host.family_number=12;
    bundle.host.resident_guid=123456;
    bundle.host.resident_neighbor_id=41;
    bundle.host.runtime_avatar_id=1;
    bundle.host.tick=0;
    bundle.host.random_seed=0x123456789abcdef0ULL;
    bundle.host.family_budget=1234;
    bundle.host.global_values={{9,21},{17,-4},{25,99}};
    bundle.world=world.snapshot();
    bundle.relationships=relationships.snapshot();
    bundle.runtime_entities={{1,first.snapshot(),first_controllers.snapshot()},
                             {2,second.snapshot(),second_controllers.snapshot()}};
    bundle.scheduler=scheduler.snapshot();
    return bundle;
}

odh::SessionBundle live_wait_fixture(bool sleep_wait) {
    odh::World world;
    require(world.create(1,odh::EntityKind::avatar),"create live waiting actor");
    odh::EntityThreadControllers controllers(world,1);
    odh::Instruction instruction{static_cast<std::uint16_t>(sleep_wait?0:17),254,255,{}};
    if(!sleep_wait)instruction.operands[2]=0;
    odh::Bhav routine{0x8002,{instruction},{},odh::Bhav::Metadata{0,4,4,0}};
    odh::Vm vm({{odh::routine_key(4096),std::move(routine)}},world.vm_memory(),{},
        [&](const auto& frame,const auto& current,const auto& temps,const auto& dispatch){
            return sleep_wait?controllers.sleep_primitive()(frame,current,temps,dispatch):
                controllers.interaction_primitive()(frame,current,temps,dispatch);
        });
    if(vm.start(odh::routine_key(4096),{1,1,1,0},{6})!=odh::VmStatus::running)
        throw std::runtime_error("start waiting Main: "+vm.diagnostic());
    require(controllers.bind(vm),"bind waiting controllers");
    require(vm.run(1)==odh::VmStatus::waiting,"live VM reaches controller wait");
    odh::EntityVmScheduler scheduler(4);
    require(scheduler.add(1,vm,[](std::uint64_t){return true;},[]{return true;},[]{return true;}),
            "register waiting VM");
    auto bundle=fixture();
    bundle.world=world.snapshot();
    bundle.runtime_entities={{1,vm.snapshot(),controllers.snapshot()}};
    bundle.scheduler=scheduler.snapshot();
    return bundle;
}

odh::SessionBundle live_playback_wait_fixture() {
    odh::World world;
    require(world.create(1,odh::EntityKind::avatar,{0,88,1}),"create animation actor");
    world.declare_object_word(1,17,0);
    odh::EntityThreadControllers controllers(world,1);
    auto clip=std::make_shared<odh::AnimationHeader>();clip->name="bundle-animation";
    odh::BoneMotion motion{};motion.frames=3;
    motion.time_properties={{{0,{{"xevt","100"}}}}};clip->motions.push_back(motion);
    odh::Playback playback(world,[&](const auto&,std::uint32_t,std::uint16_t){return clip;});
    odh::Bhav routine{0x8002,{{44,254,1,{1,0,0,0,0,0,0,0}},
        {2,0,255,{0,0,0,0,0,0,0,0}}},{},odh::Bhav::Metadata{1,2,0,0}};
    odh::Vm vm({{odh::routine_key(4096),std::move(routine)}},world.vm_memory(),{},
        [&](const auto& frame,const auto& instruction,const auto& temps,const auto& dispatch){
            if(instruction.opcode==44)return playback.primitive()(frame,instruction,temps,dispatch);
            return controllers.sleep_primitive()(frame,instruction,temps,dispatch);
        });
    if(vm.start(odh::routine_key(4096),{1,2,2})!=odh::VmStatus::running)
        throw std::runtime_error("start animation VM: "+vm.diagnostic());
    require(controllers.bind(vm)&&playback.bind(vm),"bind animation session controllers");
    require(vm.run(8)==odh::VmStatus::waiting&&playback.progress().has_value(),
            "real opcode 44 reaches pending animation event wait");
    odh::EntityVmScheduler scheduler(4);
    require(scheduler.add(1,vm,[](std::uint64_t){return true;},[]{return true;},[]{return true;}),
            "register animation VM");
    auto bundle=fixture();bundle.world=world.snapshot();
    odh::PlaybackSnapshot playback_snapshot;std::string error;
    require(playback.capture_snapshot(playback_snapshot,&error)&&playback_snapshot.state&&
            playback_snapshot.state->wait,"capture live opcode 44 Playback wait");
    bundle.runtime_entities={{1,vm.snapshot(),controllers.snapshot(),playback_snapshot}};
    bundle.scheduler=scheduler.snapshot();
    return bundle;
}

odh::SessionBundle nested_interaction_fixture(unsigned phase) {
    auto bundle=fixture();
    bundle.runtime_entities.resize(1);bundle.scheduler.entities.resize(1);
    auto& runtime=bundle.runtime_entities.front();
    const odh::VmContext context{1,2,2,0};
    const auto main_key=odh::routine_key(4095),parent_key=odh::routine_key(4096),middle_key=odh::routine_key(4097),
        child_key=odh::routine_key(4098);
    odh::VmSnapshot vm;vm.status=phase==0?odh::VmStatus::running:odh::VmStatus::yielded;
    vm.run_generation=1;vm.steps=10;vm.ticket_sequence=3;
    const auto frame=[&](odh::RoutineKey key,odh::VmContext frame_context,
                         odh::VmCallReturnMode mode,std::uint64_t ticket){
        return odh::VmFrame{key,0,frame_context,{0,0,0,0},{0,0,0,0},mode,ticket};
    };
    vm.stack={frame(main_key,{1,1,1,0},odh::VmCallReturnMode::ordinary,0),
              frame(parent_key,context,odh::VmCallReturnMode::interaction_next_tick,1),
              frame(middle_key,context,odh::VmCallReturnMode::interaction_next_tick,2)};
    if(phase==0)vm.stack.push_back(frame(child_key,context,
        odh::VmCallReturnMode::interaction_next_tick,3));
    if(phase!=0)vm.interaction_return=odh::VmInteractionReturn{3,true,0,vm.run_generation};
    odh::RoutineSet routines;
    const auto routine=odh::Bhav{0x8002,{{17,0,255,{0,0,0,0,0,0,0,0}}},{},
        odh::Bhav::Metadata{0,4,4,0}};
    routines.emplace(main_key,routine);routines.emplace(parent_key,routine);
    routines.emplace(middle_key,routine);routines.emplace(child_key,routine);
    odh::Vm snapshot_validator(routines);
    require(snapshot_validator.restore_snapshot(vm),"nested fixture is a structurally valid restorable VM snapshot");
    runtime.vm=vm;
    auto& queue=runtime.controllers.interactions;
    queue.actor_id=1;queue.capacity=32;queue.actor_generation=bundle.world.entities[0].generation;
    queue.run_generation=vm.run_generation;queue.sequence=3;queue.profile=odh::InteractionQueueProfile::ts1_priority;
    queue.actions={{1,bundle.world.entities[1].generation,
        {parent_key,context,{},odh::VmCallReturnMode::interaction_next_tick}},
        {2,bundle.world.entities[1].generation,
        {middle_key,context,{},odh::VmCallReturnMode::interaction_next_tick}}};
    queue.suspended_active={{1,1}};
    if(phase==0){
        queue.actions.push_back({3,bundle.world.entities[1].generation,
            {child_key,context,{},odh::VmCallReturnMode::interaction_next_tick}});
        queue.suspended_active.push_back({2,2});queue.active=odh::InteractionActiveSnapshot{3,3};
    }else if(phase==1){
        queue.actions.push_back({3,bundle.world.entities[1].generation,
            {child_key,context,{},odh::VmCallReturnMode::interaction_next_tick}});
        queue.suspended_active.push_back({2,2});queue.active=odh::InteractionActiveSnapshot{3,3};
    }else{
        queue.active=odh::InteractionActiveSnapshot{2,2};
        queue.completion=odh::InteractionCompletion{3,3,0,true};queue.resume_tick=1;
    }
    auto& scheduled=bundle.scheduler.entities.front();
    scheduled.id=1;scheduled.steps=vm.steps;scheduled.status=vm.status;
    scheduled.run_generation=vm.run_generation;
    return bundle;
}

odh::SessionBundle local_route_fixture() {
    auto bundle=fixture();odh::World world;
    require(world.create(1,odh::EntityKind::avatar,{0,88,1})&&
            world.create(2,odh::EntityKind::avatar,{0,88,1}),"create local-route actors");
    require(world.set_person_data(1,0,0)&&world.set_position(1,{5,12,1,2})&&
            world.set_position(2,{28,12,1,0})&&
            world.set_static_footprint(1,false),"place the walking actor and target");
    bundle.world=world.snapshot();
    const odh::VmContext context{1,2,2,1};
    const auto key=odh::routine_key(4096);
    const odh::Instruction instruction{45,254,255,{1,0,2,0,0,0,0,0}};
    odh::VmSnapshot vm;vm.status=odh::VmStatus::waiting;vm.ticket_sequence=1;
    vm.stack.push_back({key,0,context,{},{},odh::VmCallReturnMode::interaction_next_tick,1});
    vm.pending=odh::VmPending{1,odh::VmTrace{0,1,key,0,instruction,1},context};
    auto& runtime=bundle.runtime_entities[0];runtime.vm=vm;
    odh::LocalRouteSnapshot route;route.ticket=1;route.actor=1;route.target=2;
    route.actor_generation=world.find(1)->generation;route.target_generation=world.find(2)->generation;
    route.expected={5,12,1,2};route.target_position={28,12,1,0};
    route.expected_radian_direction=world.find(1)->radian_direction;
    route.frame.state=odh::RouteFrameState::walking;
    route.path={{4,12},{5,12},{6,12},{7,12}};route.next=2;route.entry_flags=4;
    route.facing=2;route.walking_started=true;route.same_room_verified=true;runtime.route=route;
    auto& interactions=runtime.controllers.interactions;
    interactions.actor_id=1;interactions.capacity=32;
    interactions.actor_generation=world.find(1)->generation;
    interactions.run_generation=vm.run_generation;interactions.sequence=1;
    interactions.actions.push_back({1,world.find(2)->generation,
        {key,context,{},odh::VmCallReturnMode::interaction_next_tick}});
    interactions.active=odh::InteractionActiveSnapshot{1,1};
    bundle.scheduler.entities[0].status=odh::VmStatus::waiting;
    bundle.scheduler.entities[0].steps=0;bundle.scheduler.entities[0].run_generation=0;
    return bundle;
}

odh::SessionBundle dynamic_action_route_fixture() {
    auto bundle=fixture();odh::World world;
    require(world.create(1,odh::EntityKind::avatar,{0,88,1})&&
            world.create(2,odh::EntityKind::object,{0,0,1})&&world.define_type(2,0x12345678u)&&
            world.set_position(2,{28,12,1,0}),
            "create actor and dynamic action target");
    require(world.set_person_data(1,0,0)&&world.set_position(1,{5,12,1,2})&&
            world.set_static_footprint(1,false),"place dynamic-route actor");
    bundle.world=world.snapshot();
    bundle.runtime_entities.resize(1);bundle.scheduler.entities.resize(1);
    constexpr odh::CodeOwner owner=0x80000000U;
    const odh::VmContext dynamic_context{1,2,2,owner};
    const odh::VmContext route_context{1,2,2,1};
    const auto dynamic_key=odh::routine_key(8213),key=odh::routine_key(4097);
    const odh::Instruction instruction{45,254,255,{1,0,2,0,0,0,0,0}};
    odh::VmSnapshot vm;vm.status=odh::VmStatus::waiting;vm.ticket_sequence=1;
    vm.stack.push_back({dynamic_key,0,dynamic_context,{},{},odh::VmCallReturnMode::interaction_next_tick,1});
    vm.stack.push_back({key,0,route_context,{},{},odh::VmCallReturnMode::ordinary,1});
    vm.pending=odh::VmPending{1,odh::VmTrace{0,2,key,0,instruction,1},route_context};
    auto& runtime=bundle.runtime_entities[0];runtime.vm=vm;
    odh::LocalRouteSnapshot route;route.ticket=1;route.actor=1;route.target=2;
    route.actor_generation=world.find(1)->generation;route.target_generation=world.find(2)->generation;
    route.expected={5,12,1,2};route.target_position={28,12,1,0};
    route.expected_radian_direction=world.find(1)->radian_direction;
    route.frame.state=odh::RouteFrameState::walking;
    route.path={{4,12},{5,12},{6,12},{7,12}};route.next=2;route.entry_flags=4;
    route.facing=2;route.walking_started=true;route.same_room_verified=true;runtime.route=route;
    runtime.controllers.interactions.actor_id=1;runtime.controllers.interactions.capacity=32;
    runtime.controllers.interactions.actor_generation=world.find(1)->generation;
    runtime.controllers.interactions.run_generation=vm.run_generation;
    runtime.controllers.interactions.sequence=1;
    runtime.controllers.interactions.actions.push_back({1,world.find(2)->generation,
        {dynamic_key,dynamic_context,{},odh::VmCallReturnMode::interaction_next_tick}});
    runtime.controllers.interactions.active=odh::InteractionActiveSnapshot{1,1};
    odh::SessionDynamicActionOwner binding;binding.owner=owner;binding.target_id=2;
    binding.target_generation=world.find(2)->generation;binding.target_guid=0x12345678u;
    binding.source="Objects.far";binding.asset="fridge.iff";binding.definition_chunk_id=16807;
    binding.tree_table_id=3;binding.root_routine=8213;binding.linked_semiglobal=odh::SessionLinkedSemiglobal{
        "Appliance","GameData/Global/Global.far","Appliance.iff"};binding.routine_fingerprint.fill(0x37);
    runtime.dynamic_action_owners.push_back(binding);
    bundle.scheduler.entities[0].status=odh::VmStatus::waiting;
    bundle.scheduler.entities[0].steps=0;bundle.scheduler.entities[0].run_generation=0;
    return bundle;
}

void u16(std::vector<std::uint8_t>& bytes,std::uint16_t value) {
    bytes.push_back(static_cast<std::uint8_t>(value));
    bytes.push_back(static_cast<std::uint8_t>(value>>8));
}
void u32(std::vector<std::uint8_t>& bytes,std::uint32_t value) {
    for(unsigned shift=0;shift<32;shift+=8)
        bytes.push_back(static_cast<std::uint8_t>(value>>shift));
}
std::vector<std::uint8_t> controller_roster(const odh::SessionBundle& bundle) {
    std::vector<std::uint8_t> bytes;
    u16(bytes,1);
    u16(bytes,static_cast<std::uint16_t>(bundle.runtime_entities.size()));
    for(const auto& entity:bundle.runtime_entities) {
        const auto payload=odh::encode_entity_thread_controllers_snapshot(entity.controllers);
        u16(bytes,static_cast<std::uint16_t>(entity.id));
        u32(bytes,static_cast<std::uint32_t>(payload.size()));
        bytes.insert(bytes.end(),payload.begin(),payload.end());
    }
    return bytes;
}
std::vector<std::uint8_t> vm_roster(const odh::SessionBundle& bundle) {
    std::vector<std::uint8_t> bytes;
    u16(bytes,1);
    u16(bytes,static_cast<std::uint16_t>(bundle.runtime_entities.size()));
    for(const auto& entity:bundle.runtime_entities) {
        const auto payload=odh::encode_vm_snapshot(entity.vm);
        u16(bytes,static_cast<std::uint16_t>(entity.id));
        u32(bytes,static_cast<std::uint32_t>(payload.size()));
        bytes.insert(bytes.end(),payload.begin(),payload.end());
    }
    return bytes;
}
void replace_section(odh::SessionArchive& archive,const std::array<char,4>& id,
                     std::vector<std::uint8_t> data) {
    auto it=std::find_if(archive.sections.begin(),archive.sections.end(),[&](const auto& section){
        return section.id==id;
    });
    require(it!=archive.sections.end(),"bundle section exists");
    it->data=std::move(data);
}
void replace_runtime_sections(odh::SessionArchive& archive,const odh::SessionBundle& bundle) {
    replace_section(archive,{{'V','M',' ',' '}},vm_roster(bundle));
    replace_section(archive,{{'C','T','R','L'}},controller_roster(bundle));
    replace_section(archive,{{'S','C','H','D'}},
                    odh::encode_entity_vm_scheduler_snapshot(bundle.scheduler));
}

}

odh::SessionBundle object_thread_fixture() {
    auto bundle=fixture();odh::World world;
    require(world.restore_snapshot(bundle.world),"restore base world for object thread");
    constexpr std::int16_t id=207;constexpr std::uint32_t guid=2933422533u;
    require(world.create_runtime_object(id,guid,{},{}),"create PhoneLine-shaped object");
    odh::Sleep sleep(world);
    odh::Bhav main{0x8002,{{0,254,255,{0,0,0,0,0,0,0,0}}},{},odh::Bhav::Metadata{0,1,0,0}};
    odh::RoutineOwners owners{{guid,{{odh::routine_key(4096),main}}}};
    odh::Vm vm({},world.vm_memory(),{},
        [&](const auto& frame,const auto& instruction,const auto& temps,const auto& dispatch){
            return sleep.primitive()(frame,instruction,temps,dispatch);
        },std::move(owners));
    require(sleep.bind(vm),"bind object Sleep to fresh VM");
    require(sleep.join_at(0),"join object Sleep to initial host tick");
    require(vm.start(odh::routine_key(4096),{id,id,id,guid},{4})==odh::VmStatus::running,
            "start object Main");
    require(vm.run(1)==odh::VmStatus::waiting,"object Main reaches Sleep wait");
    auto object_bundle=std::move(bundle);object_bundle.world=world.snapshot();
    auto& object=object_bundle.world.entities.back();
    require(object.id==id&&object.definition&&object.definition->guid==guid,"object identity is present");
    odh::SessionObjectThread thread;thread.id=id;thread.generation=object.generation;thread.guid=guid;
    odh::SessionActionCodeOwner owner;owner.owner=guid;owner.source="private";
    owner.asset="Objects.far!PhoneLine.iff#16807";owner.root_routine=4096;
    owner.routine_fingerprint.fill(0x71);thread.code_owners.push_back(owner);
    thread.vm=vm.snapshot();thread.sleep=sleep.snapshot();object_bundle.object_threads.push_back(thread);
    object_bundle.scheduler.entities.push_back({id,thread.vm.run_generation,thread.vm.steps,
        thread.vm.status,false,false,false});
    object_bundle.scheduler.entity_limit=4;
    return object_bundle;
}

odh::SessionResourceCodeOwner resource_owner(odh::CodeOwner owner,std::uint32_t guid,
    std::uint16_t root=4097) {
    odh::SessionResourceCodeOwner result;result.owner=owner;result.resource_guid=guid;
    result.source="GameData/Objects/Objects.far";result.asset="synthetic-object.iff";
    result.definition_chunk_id=17;result.tree_table_id=3;result.root_routine=root;
    result.routine_fingerprint.fill(0x53);return result;
}
odh::SessionBundle generic_action_fixture() {
    auto bundle=fixture();odh::World world;
    require(world.create(1,odh::EntityKind::avatar)&&world.create_runtime_object(2,0x12345678u,{}, {})&&
        world.create_runtime_object(3,0x12345678u,{},{}),"create generic actor and equal-resource targets");
    odh::EntityThreadControllers controllers(world,1);
    const odh::Bhav main{0x8002,{{17,0,255,{0,0,1,0,0,0,0,0}}},{},odh::Bhav::Metadata{0,1,0,0}};
    const odh::Bhav action{0x8002,{{17,254,255,{0,0,0,0,0,0,0,0}}},{},odh::Bhav::Metadata{0,1,0,0}};
    constexpr odh::CodeOwner owner=0x80000000U;
    odh::Vm vm({{odh::routine_key(4096),main}},world.vm_memory(),{},controllers.interaction_primitive(),
        {{owner,{{odh::routine_key(4097),action}}}});
    require(vm.start(odh::routine_key(4096),{1,1,1},{10})==odh::VmStatus::running&&controllers.bind(vm),
        "start generic Main and bind controllers");
    require(controllers.interactions().enqueue(odh::routine_key(4097),{1,2,2,owner},{4})&&
        controllers.interactions().enqueue(odh::routine_key(4097),{1,3,3,owner},{4}),
        "queue one resource owner against two object instances");
    require(vm.run(8)==odh::VmStatus::waiting&&controllers.interactions().active()&&
        controllers.interactions().waiting(),"actual action tree reaches active IdleForInput wait");
    odh::EntityVmScheduler scheduler(4);
    require(scheduler.add(1,vm,[](std::uint64_t){return true;},[]{return true;},[]{return true;}),
        "schedule generic action thread");
    bundle.world=world.snapshot();bundle.runtime_entities={{1,vm.snapshot(),controllers.snapshot()}};
    bundle.runtime_entities[0].resource_owners={resource_owner(owner,0x12345678u)};
    bundle.scheduler=scheduler.snapshot();bundle.generic_execution=true;bundle.next_code_owner=owner+1;
    return bundle;
}
odh::SessionBundle generic_object_fixture() {
    auto bundle=object_thread_fixture();bundle.generic_execution=true;bundle.next_code_owner=0x80000001U;
    auto& object=bundle.object_threads[0];const auto owner=0x80000000U;
    object.code_owners.clear();object.resource_owners={resource_owner(owner,object.guid,4096)};object.primary_owner=owner;
    object.resource_owners[0].bank_kind=odh::SessionCodeBankKind::complete_private;
    for(auto& frame:object.vm.stack)frame.context.code_owner=owner;
    if(object.vm.pending){object.vm.pending->context.code_owner=owner;
        object.vm.pending->instruction.code_owner=owner;}
    bundle.created_objects.push_back({object.id,object.generation,object.guid,
        object.resource_owners[0].source,object.resource_owners[0].asset,17});
    bundle.world.entities.back().static_footprint=false;
    bundle.deferred_removals.push_back({object.id,object.generation});
    bundle.removed_original_objects.push_back({206,1,0x654321u});
    return bundle;
}
void generic_execution_cases() {
    const auto bundle=generic_action_fixture();const auto bytes=odh::encode_session_bundle(bundle);
    const auto decoded=odh::decode_session_bundle(bytes);
    require(decoded.generic_execution&&decoded.runtime_entities[0].resource_owners.size()==1&&
        decoded.runtime_entities[0].controllers.interactions.actions.size()==2&&
        decoded.runtime_entities[0].controllers.interactions.active&&
        decoded.runtime_entities[0].controllers.interactions.wait&&!decoded.runtime_entities[0].route&&
        decoded.next_code_owner==bundle.next_code_owner&&odh::encode_session_bundle(decoded)==bytes,
        "generic active action Idle wait and shared resource owner round-trip without route or fixed target");
    auto archive=odh::decode_session_archive(bytes);
    const auto section=std::find_if(archive.sections.begin(),archive.sections.end(),
        [](const auto& value){return value.id==std::array<char,4>{{'G','E','X','P'}};});
    require(section!=archive.sections.end()&&section->required,"generic execution capability is required");
    const auto section_index=static_cast<std::size_t>(section-archive.sections.begin());
    auto malformed=archive;malformed.sections[section_index].required=false;
    rejects([&]{odh::decode_session_bundle(odh::encode_session_archive(malformed));},"optional GEXP accepted");
    malformed=archive;malformed.sections[section_index].data[0]=2;
    rejects([&]{odh::decode_session_bundle(odh::encode_session_archive(malformed));},"unknown GEXP version accepted");
    malformed=archive;malformed.sections[section_index].data[6]=2;
    rejects([&]{odh::decode_session_bundle(odh::encode_session_archive(malformed));},"invalid exhaustion marker accepted");
    malformed=archive;malformed.sections[section_index].data[7]=0;
    rejects([&]{odh::decode_session_bundle(odh::encode_session_archive(malformed));},"partial thread manifest accepted");
    malformed=archive;malformed.sections[section_index].id={{'G','X','9','9'}};
    rejects([&]{odh::decode_session_bundle(odh::encode_session_archive(malformed));},"unknown required execution capability accepted");
    malformed=archive;malformed.sections[section_index].data.push_back(0);
    rejects([&]{odh::decode_session_bundle(odh::encode_session_archive(malformed));},"GEXP trailing bytes accepted");
    auto bad=bundle;bad.generic_execution=false;
    rejects([&]{odh::encode_session_bundle(bad);},"new provenance silently written without required capability");
    bad=bundle;bad.runtime_entities[0].resource_owners.clear();
    rejects([&]{odh::encode_session_bundle(bad);},"unproven generic owner accepted");
    bad=bundle;bad.runtime_entities[0].resource_owners[0].routine_fingerprint.fill(0);
    rejects([&]{odh::encode_session_bundle(bad);},"empty owner fingerprint accepted");
    bad=bundle;bad.runtime_entities[0].resource_owners[0].bank_kind=static_cast<odh::SessionCodeBankKind>(99);
    rejects([&]{odh::encode_session_bundle(bad);},"unknown code bank kind accepted");
    bad=bundle;bad.runtime_entities[0].resource_owners[0].linked_semiglobal=odh::SessionLinkedSemiglobal{
        "Wrong","GameData/Global/Global.far","Wrong.iff"};
    rejects([&]{odh::encode_session_bundle(bad);},"private bank unexpectedly binds semiglobal");
    bad=bundle;bad.runtime_entities[0].resource_owners.push_back(resource_owner(0x80000001U,123));
    ++bad.next_code_owner;
    const auto retained_owner=odh::decode_session_bundle(odh::encode_session_bundle(bad));
    require(retained_owner.runtime_entities[0].resource_owners.size()==2&&
        retained_owner.runtime_entities[0].resource_owners[1].owner==0x80000001U,
        "installed idle owner cache did not survive the session bundle");
    bad=bundle;bad.next_code_owner=0x80000000U;
    rejects([&]{odh::encode_session_bundle(bad);},"owner allocator would reuse live ID");
    bad=bundle;bad.next_code_owner=0x80000011U;
    require(odh::decode_session_bundle(odh::encode_session_bundle(bad)).next_code_owner==0x80000011U,
        "allocator cursor preserves gaps from unreferenced past owners");
    bad.code_owner_ids_exhausted=true;
    rejects([&]{odh::encode_session_bundle(bad);},"exhausted allocator with nonMAX cursor accepted");
    bad.next_code_owner=UINT32_MAX;
    require(odh::decode_session_bundle(odh::encode_session_bundle(bad)).code_owner_ids_exhausted,
        "exhaustion survives reload without ID reuse");
    const auto objects=generic_object_fixture();const auto object_bytes=odh::encode_session_bundle(objects);
    const auto object_copy=odh::decode_session_bundle(object_bytes);
    require(object_copy.object_threads[0].code_owners.empty()&&object_copy.object_threads[0].resource_owners.size()==1&&
        object_copy.created_objects.size()==1&&object_copy.removed_original_objects.size()==1&&
        object_copy.deferred_removals.size()==1&&odh::encode_session_bundle(object_copy)==object_bytes,
        "created object Main with process owner and deferred removal round-trips without GUID owner assumption");
    auto wrong=objects;wrong.created_objects[0].generation++;
    rejects([&]{odh::encode_session_bundle(wrong);},"created provenance with stale generation accepted");
    wrong=objects;wrong.created_objects[0].guid++;
    rejects([&]{odh::encode_session_bundle(wrong);},"created provenance with wrong GUID accepted");
    wrong=objects;wrong.created_objects[0].source="different.far";
    rejects([&]{odh::encode_session_bundle(wrong);},"created Main resource differs from origin");
    wrong=objects;wrong.deferred_removals[0].generation++;
    rejects([&]{odh::encode_session_bundle(wrong);},"stale deferred removal accepted");
    wrong=objects;wrong.deferred_removals.push_back(wrong.deferred_removals[0]);
    rejects([&]{odh::encode_session_bundle(wrong);},"duplicate deferred removal accepted");
    wrong=objects;wrong.removed_original_objects={{objects.created_objects[0].id,
        objects.created_objects[0].generation,objects.created_objects[0].guid}};
    rejects([&]{odh::encode_session_bundle(wrong);},"removed identity still live accepted");
    wrong=objects;wrong.created_objects.push_back(wrong.created_objects[0]);
    rejects([&]{odh::encode_session_bundle(wrong);},"duplicate creation provenance accepted");
    wrong=objects;wrong.object_threads[0].primary_owner=0;
    rejects([&]{odh::encode_session_bundle(wrong);},"generic object primary bank inferred instead of explicit identity");
    wrong=objects;wrong.object_threads[0].primary_owner++;
    rejects([&]{odh::encode_session_bundle(wrong);},"object primary bank points to undeclared program");
    wrong=objects;wrong.object_threads[0].resource_owners[0].resource_guid++;
    rejects([&]{odh::encode_session_bundle(wrong);},"object bank provenance lacks its actual private resource");
    auto same_guid_banks=objects;same_guid_banks.next_code_owner=0x80000002U;
    auto secondary_bank=same_guid_banks.object_threads[0].resource_owners[0];secondary_bank.owner++;
    same_guid_banks.object_threads[0].resource_owners.push_back(secondary_bank);
    for(auto& frame:same_guid_banks.object_threads[0].vm.stack)frame.context.code_owner=secondary_bank.owner;
    same_guid_banks.object_threads[0].vm.pending->context.code_owner=secondary_bank.owner;
    same_guid_banks.object_threads[0].vm.pending->instruction.code_owner=secondary_bank.owner;
    const auto explicit_primary=odh::decode_session_bundle(odh::encode_session_bundle(same_guid_banks));
    require(explicit_primary.object_threads[0].primary_owner==0x80000000U&&
        explicit_primary.object_threads[0].resource_owners.size()==2,
        "explicit primary owner survives another referenced complete bank with the same GUID");
    auto guid_backed=objects;guid_backed.next_code_owner=0x80000000U;
    auto& guid_thread=guid_backed.object_threads[0];auto& guid_owner=guid_thread.resource_owners[0];
    guid_owner.owner=guid_thread.guid;guid_thread.primary_owner=guid_thread.guid;guid_owner.identity=odh::SessionCodeOwnerIdentity::resource_guid;
    for(auto& frame:guid_thread.vm.stack)frame.context.code_owner=guid_thread.guid;
    guid_thread.vm.pending->context.code_owner=guid_thread.guid;
    guid_thread.vm.pending->instruction.code_owner=guid_thread.guid;
    require(odh::decode_session_bundle(odh::encode_session_bundle(guid_backed)).next_code_owner==0x80000000U,
        "GUID-backed private owner does not consume process owner allocator range");
    guid_owner.identity=odh::SessionCodeOwnerIdentity::process;
    rejects([&]{odh::encode_session_bundle(guid_backed);},"explicit process identity bypassed allocator cursor");
    guid_owner.identity=static_cast<odh::SessionCodeOwnerIdentity>(99);
    rejects([&]{odh::encode_session_bundle(guid_backed);},"unknown code owner identity profile accepted");
    auto coincident=objects;coincident.next_code_owner=0x80000000U;
    coincident.object_threads[0].guid=0x80000000U;
    coincident.object_threads[0].resource_owners[0].resource_guid=0x80000000U;
    coincident.world.entities.back().definition->guid=0x80000000U;
    coincident.created_objects[0].guid=0x80000000U;
    rejects([&]{odh::encode_session_bundle(coincident);},"allocated owner equal to GUID bypassed explicit allocation identity");
    // A single process-wide resource owner may be installed into two native VMs.
    auto shared=objects;
    shared.runtime_entities[0].vm.stack.push_back({odh::routine_key(4096),0,
        {1,207,207,0x80000000U},{},{},odh::VmCallReturnMode::ordinary,0});
    shared.runtime_entities[0].vm.status=odh::VmStatus::running;
    shared.runtime_entities[0].resource_owners=shared.object_threads[0].resource_owners;
    shared.scheduler.entities[0].status=odh::VmStatus::running;
    require(odh::decode_session_bundle(odh::encode_session_bundle(shared)).generic_execution,
        "one identical resource owner shared across VM manifests");
    shared.runtime_entities[0].resource_owners[0].routine_fingerprint[0]^=1;
    rejects([&]{odh::encode_session_bundle(shared);},"same global owner ID bound to conflicting bank fingerprints");
    auto implicit_collision=objects;
    auto& collided=implicit_collision.object_threads[0];collided.resource_owners[0].owner=2;collided.primary_owner=2;
    for(auto& frame:collided.vm.stack)frame.context.code_owner=2;
    collided.vm.pending->context.code_owner=2;collided.vm.pending->instruction.code_owner=2;
    rejects([&]{odh::encode_session_bundle(implicit_collision);},
        "resource owner in object VM collides with another resident implicit bank");
    auto legacy_collision=object_thread_fixture();legacy_collision.generic_execution=true;
    const auto legacy_id=legacy_collision.object_threads[0].guid;
    legacy_collision.next_code_owner=legacy_id+1;
    auto& invoker=legacy_collision.runtime_entities[0];invoker.vm.status=odh::VmStatus::running;
    invoker.vm.stack={{odh::routine_key(4096),0,{1,207,207,legacy_id},{},{},odh::VmCallReturnMode::ordinary,0}};
    invoker.resource_owners={resource_owner(legacy_id,legacy_id,4096)};
    legacy_collision.scheduler.entities[0].status=odh::VmStatus::running;
    rejects([&]{odh::encode_session_bundle(legacy_collision);},
        "resource owner collides with another object's legacy bank registry");
    auto mixed=dynamic_action_route_fixture();mixed.generic_execution=true;mixed.next_code_owner=0x80000001U;
    odh::World mixed_world;require(mixed_world.restore_snapshot(mixed.world)&&
        mixed_world.create(3,odh::EntityKind::avatar),"create second resident for mixed owner registries");
    mixed.world=mixed_world.snapshot();
    auto extra=fixture().runtime_entities[1];extra.id=3;extra.controllers.interactions.actor_id=3;
    extra.controllers.interactions.actor_generation=mixed_world.find(3)->generation;
    extra.vm.status=odh::VmStatus::running;
    extra.vm.stack={{odh::routine_key(8213),0,{3,2,2,0x80000000U},{},{},odh::VmCallReturnMode::ordinary,0}};
    const auto& dyno=mixed.runtime_entities[0].dynamic_action_owners[0];
    auto res=resource_owner(dyno.owner,dyno.target_guid,dyno.root_routine);
    res.source=dyno.source;res.asset=dyno.asset;res.definition_chunk_id=dyno.definition_chunk_id;
    res.tree_table_id=dyno.tree_table_id;res.routine_fingerprint=dyno.routine_fingerprint;
    res.bank_kind=odh::SessionCodeBankKind::private_and_semiglobal;res.linked_semiglobal=dyno.linked_semiglobal;
    extra.resource_owners={res};mixed.runtime_entities.push_back(extra);
    mixed.scheduler.entities.push_back({3,extra.vm.run_generation,extra.vm.steps,extra.vm.status,false,false,false});
    rejects([&]{odh::encode_session_bundle(mixed);},"GEXP owner collides with another resident DYNO registry");
    auto legacy_shared=object_thread_fixture();legacy_shared.generic_execution=true;
    odh::World twin_world;require(twin_world.restore_snapshot(legacy_shared.world)&&
        twin_world.create_runtime_object(208,legacy_shared.object_threads[0].guid,{},{}),
        "create two instances with one legacy private resource");
    legacy_shared.world=twin_world.snapshot();auto twin=legacy_shared.object_threads[0];
    twin.id=208;twin.generation=twin_world.find(208)->generation;
    for(auto& frame:twin.vm.stack)frame.context.caller=frame.context.callee=frame.context.stack_object=208;
    twin.vm.pending->context.caller=twin.vm.pending->context.callee=twin.vm.pending->context.stack_object=208;
    twin.sleep.actor->id=208;twin.sleep.actor->generation=twin.generation;
    legacy_shared.object_threads.push_back(twin);
    legacy_shared.scheduler.entities.push_back({208,twin.vm.run_generation,twin.vm.steps,twin.vm.status,false,false,false});
    require(odh::decode_session_bundle(odh::encode_session_bundle(legacy_shared)).object_threads.size()==2,
        "identical legacy GUID owner sharing does not advance process allocator");
    legacy_shared.object_threads[1].code_owners[0].routine_fingerprint[0]^=1;
    rejects([&]{odh::encode_session_bundle(legacy_shared);},"legacy shared owner bank fingerprint conflict accepted");
    auto secondary=object_thread_fixture();secondary.generic_execution=true;
    auto secondary_owner=secondary.object_threads[0].code_owners[0];secondary_owner.owner=UINT32_MAX;
    secondary.object_threads[0].code_owners.push_back(secondary_owner);
    rejects([&]{odh::encode_session_bundle(secondary);},"secondary legacy process owner skipped allocator cursor validation");
    // Native push-head insertion reorders UIDs; a completed UID is not an order bound.
    auto reordered=live_wait_fixture(false);auto& queue=reordered.runtime_entities[0].controllers.interactions;
    const auto target_generation=reordered.world.entities.front().generation;
    const odh::VmCall call{odh::routine_key(4097),{1,1,1}, {},odh::VmCallReturnMode::interaction_next_tick};
    queue.sequence=3;queue.actions={{3,target_generation,call},{1,target_generation,call}};
    queue.completion=odh::InteractionCompletion{2,1,0,true};
    require(odh::decode_session_bundle(odh::encode_session_bundle(reordered)).runtime_entities[0].controllers.
        interactions.actions.front().uid==3,"push-head queue order and prior completion UID are preserved");
}

void ui_state_cases() {
    const std::array<char,4> id{{'U','I','S','T'}};
    auto legacy=fixture();
    require(!odh::decode_session_bundle(odh::encode_session_bundle(legacy)).ui_state,
        "old absent UI state must remain absent");
    for(auto& actor:legacy.world.entities)
        actor.definition=odh::EntityType{legacy.host.resident_guid+static_cast<std::uint32_t>(actor.id-1),std::nullopt};
    legacy.ui_state=odh::SessionUiState{1,legacy.world.entities[0].generation,legacy.host.resident_guid};
    const auto old=odh::decode_session_bundle(odh::encode_session_bundle(legacy));
    require(old.ui_state&&old.ui_state->active_actor==1,"legacy HOST resident UI selection roundtrip");
    auto outside=legacy;outside.ui_state=odh::SessionUiState{2,legacy.world.entities[1].generation,legacy.world.entities[1].definition->guid};
    rejects([&]{odh::encode_session_bundle(outside);},"legacy UI selected non-HOST family actor");
    auto household=legacy;
    for(const auto& actor:household.world.entities){
        odh::SessionHouseholdMember member;
        member.id=actor.id;member.generation=actor.generation;member.guid=actor.definition->guid;
        member.neighbor_id=static_cast<std::int16_t>(household.host.resident_neighbor_id+actor.id-1);
        member.object_definition_id=128;member.character_asset="PlayerData/Characters/User"+std::to_string(actor.id)+".iff";
        member.character_fingerprint.fill(0x61);household.household_roster.push_back(member);
    }
    household.ui_state=odh::SessionUiState{2,household.world.entities[1].generation,household.world.entities[1].definition->guid};
    const auto bytes=odh::encode_session_bundle(household);
    const auto decoded=odh::decode_session_bundle(bytes);
    require(decoded.ui_state&&decoded.ui_state->active_actor==2&&
        decoded.ui_state->generation==household.ui_state->generation&&decoded.ui_state->guid==household.ui_state->guid,
        "nonprimary household selection roundtrip");
    require(odh::encode_session_bundle(decoded)==bytes,"UI state roundtrip byte stable");
    auto bad=household;++bad.ui_state->generation;
    rejects([&]{odh::encode_session_bundle(bad);},"stale UI actor generation accepted");
    bad=household;++bad.ui_state->guid;
    rejects([&]{odh::encode_session_bundle(bad);},"wrong UI actor GUID accepted");
    bad=household;bad.ui_state->active_actor=3;
    rejects([&]{odh::encode_session_bundle(bad);},"missing UI actor accepted");
    bad=household;bad.world.entities[1].ui_ghost=true;
    rejects([&]{odh::encode_session_bundle(bad);},"ghost UI actor accepted");
    bad=household;bad.world.entities[1].kind=odh::EntityKind::object;
    rejects([&]{odh::encode_session_bundle(bad);},"object UI selection accepted");
    auto archive=odh::decode_session_archive(bytes);
    auto selected=std::find_if(archive.sections.begin(),archive.sections.end(),[&](const auto& section){return section.id==id;});
    require(selected!=archive.sections.end()&&!selected->required&&selected->data.size()==16,
        "UIST must be optional exact v1 extent");
    selected->data[4]^=1;
    rejects([&]{odh::decode_session_bundle(odh::encode_session_archive(archive));},"decoded stale UI generation accepted");
    archive=odh::decode_session_archive(bytes);selected=std::find_if(archive.sections.begin(),archive.sections.end(),[&](const auto& section){return section.id==id;});
    selected->data.pop_back();
    rejects([&]{odh::decode_session_bundle(odh::encode_session_archive(archive));},"truncated UIST accepted");
    archive=odh::decode_session_archive(bytes);selected=std::find_if(archive.sections.begin(),archive.sections.end(),[&](const auto& section){return section.id==id;});
    selected->data[0]=2;
    rejects([&]{odh::decode_session_bundle(odh::encode_session_archive(archive));},"unknown UIST version accepted");
    archive=odh::decode_session_archive(bytes);selected=std::find_if(archive.sections.begin(),archive.sections.end(),[&](const auto& section){return section.id==id;});
    selected->required=true;
    rejects([&]{odh::decode_session_bundle(odh::encode_session_archive(archive));},"required UIST accepted");
    selected->required=false;archive.sections.push_back(*selected);
    rejects([&]{odh::decode_session_bundle(odh::encode_session_archive(archive));},"duplicate UIST accepted");
    archive=odh::decode_session_archive(bytes);
    archive.sections.erase(std::remove_if(archive.sections.begin(),archive.sections.end(),[&](const auto& section){return section.id==id;}),archive.sections.end());
    const auto absent=odh::decode_session_bundle(odh::encode_session_archive(archive));
    require(!absent.ui_state&&absent.household_roster.size()==2,"older household archive without UIST accepted");
}

int main() {
    ui_state_cases();
    generic_execution_cases();
    {
        auto managed=fixture();managed.generic_execution=true;
        for(auto& actor:managed.world.entities){
            const auto guid=managed.host.resident_guid+static_cast<std::uint32_t>(actor.id-1);
            actor.definition=odh::EntityType{guid,std::nullopt};
            odh::SessionHouseholdMember member;
            member.id=actor.id;member.generation=actor.generation;member.guid=guid;
            member.neighbor_id=static_cast<std::int16_t>(managed.host.resident_neighbor_id+actor.id-1);
            member.object_definition_id=128;
            member.character_asset="PlayerData/Characters/User"+std::to_string(actor.id)+".iff";
            member.character_fingerprint.fill(0x31);
            managed.household_roster.push_back(member);
        }
        odh::SessionFamilyLease lease;
        lease.neighborhood_identity.fill(0x12);lease.family_identity.fill(0x34);
        lease.occupancy_epoch=0x100000002ULL;lease.family_chunk_id=managed.host.family_chunk_id;
        managed.family_lease=lease;
        const auto encoded=odh::encode_session_bundle(managed);
        const auto restored_managed=odh::decode_session_bundle(encoded);
        require(restored_managed.family_lease&&
            restored_managed.family_lease->occupancy_epoch==lease.occupancy_epoch&&
            restored_managed.family_lease->family_identity==lease.family_identity&&
            restored_managed.family_lease->neighborhood_identity==lease.neighborhood_identity&&
            odh::encode_session_bundle(restored_managed)==encoded,"managed family lease round-trip");
        auto bad=managed;bad.family_lease->occupancy_epoch=0;
        rejects([&]{(void)odh::encode_session_bundle(bad);},"zero occupancy epoch rejected");
        bad=managed;bad.family_lease->family_identity.fill(0);
        rejects([&]{(void)odh::encode_session_bundle(bad);},"zero family identity rejected");
        bad=managed;bad.family_lease->neighborhood_identity.fill(0);
        rejects([&]{(void)odh::encode_session_bundle(bad);},"zero neighborhood identity rejected");
        bad=managed;++bad.family_lease->family_chunk_id;
        rejects([&]{(void)odh::encode_session_bundle(bad);},"lease family differs from host rejected");
        bad=managed;bad.generic_execution=false;
        rejects([&]{(void)odh::encode_session_bundle(bad);},"managed save requires generic execution");
        bad=managed;bad.household_roster.clear();
        rejects([&]{(void)odh::encode_session_bundle(bad);},"managed save requires household roster");
        bad=managed;bad.host.house_number=0;
        rejects([&]{(void)odh::encode_session_bundle(bad);},"managed save requires occupied house");
        const auto mutate_section=[&](auto mutate){
            auto archive=odh::decode_session_archive(encoded);
            for(auto& section:archive.sections)if(section.id==std::array<char,4>{{'F','A','M','L'}})mutate(section);
            return odh::encode_session_archive(archive);
        };
        rejects([&]{(void)odh::decode_session_bundle(mutate_section([](auto& s){s.required=false;}));},
            "optional lease rejected");
        rejects([&]{(void)odh::decode_session_bundle(mutate_section([](auto& s){s.data[0]=2;}));},
            "unknown lease version rejected");
        rejects([&]{(void)odh::decode_session_bundle(mutate_section([](auto& s){s.data.push_back(0);}));},
            "oversized lease rejected");
        rejects([&]{(void)odh::decode_session_bundle(mutate_section([](auto& s){s.data.pop_back();}));},
            "truncated lease rejected");
    }
    const auto source=fixture();
    const auto bytes=odh::encode_session_bundle(source);
    const auto restored=odh::decode_session_bundle(bytes,source.resources);
    require(!restored.family_lease,"legacy archive has no implicit family lease");
    require(odh::encode_session_bundle(restored)==bytes,"two-resident bundle is canonical");
    require(restored.host.random_seed==source.host.random_seed&&restored.host.global_values.size()==3&&
            restored.host.family_budget==source.host.family_budget&&
            restored.host.global_values[1].value==-4&&restored.runtime_entities.size()==2&&
            restored.runtime_entities[1].id==2,"host and both residents round-trip");
    require(!restored.runtime_entities[0].playback.state&&
            !restored.runtime_entities[0].playback.carry&&
            !restored.runtime_entities[0].playback.hands,
            "empty Playback state round-trips for each resident");
    require(!restored.simulation,"legacy archive without SIMS stays decodable");
    for(unsigned phase=0;phase<3;++phase){
        const auto nested=nested_interaction_fixture(phase);
        const auto nested_bytes=odh::encode_session_bundle(nested);
        const auto nested_restored=odh::decode_session_bundle(nested_bytes,nested.resources);
        const auto& interactions=nested_restored.runtime_entities[0].controllers.interactions;
        require(odh::encode_session_bundle(nested_restored)==nested_bytes&&
            interactions.actions.size()==(phase==2?2u:3u)&&
            interactions.suspended_active.size()==(phase==2?1u:2u)&&interactions.active&&
            nested_restored.runtime_entities[0].vm.stack.size()==(phase==0?4u:3u),
            "nested interaction frames and save phase round-trip in outer-to-inner order");
    }
    auto mutable_stack_object=nested_interaction_fixture(0);
    mutable_stack_object.runtime_entities[0].vm.stack[2].context.stack_object=1;
    require(odh::decode_session_bundle(odh::encode_session_bundle(mutable_stack_object)).
        runtime_entities[0].vm.stack[2].context.stack_object==1,
        "mutable current StackObjectID does not invalidate its saved action call");
    auto idle_retry=nested_interaction_fixture(0);
    auto& idle_queue=idle_retry.runtime_entities[0].controllers.interactions;
    idle_queue.clock=1;idle_queue.last_tick=1;idle_queue.idle_start=1;
    idle_retry.host.tick=1;idle_retry.scheduler.last_tick=1;
    const auto idle_retry_bytes=odh::encode_session_bundle(idle_retry);
    const auto idle_retry_restored=odh::decode_session_bundle(idle_retry_bytes,idle_retry.resources);
    require(idle_retry_restored.runtime_entities[0].controllers.interactions.idle_start==1&&
        !idle_retry_restored.runtime_entities[0].controllers.interactions.wait&&
        odh::encode_session_bundle(idle_retry_restored)==idle_retry_bytes,
        "active IdleForInput retry cut survives save before the VM retries it");
    auto invalid_idle_retry=idle_retry;
    invalid_idle_retry.runtime_entities[0].vm.status=odh::VmStatus::yielded;
    invalid_idle_retry.scheduler.entities[0].status=odh::VmStatus::yielded;
    rejects([&]{odh::encode_session_bundle(invalid_idle_retry);},
        "active IdleForInput retry cut with a yielded VM is rejected");
    auto invalid_nested=nested_interaction_fixture(0);
    invalid_nested.runtime_entities[0].vm.stack[2].invocation_ticket=3;
    rejects([&]{odh::encode_session_bundle(invalid_nested);},
        "nested middle frame with a ticket from another queue depth is rejected");
    invalid_nested=nested_interaction_fixture(0);
    invalid_nested.runtime_entities[0].vm.stack[2].key=odh::routine_key(4098);
    rejects([&]{odh::encode_session_bundle(invalid_nested);},
        "nested frame key that differs from its ordered action is rejected");
    invalid_nested=nested_interaction_fixture(1);
    invalid_nested.runtime_entities[0].vm.interaction_return->ticket=2;
    rejects([&]{odh::encode_session_bundle(invalid_nested);},
        "unobserved deepest return with a parent ticket is rejected");
    invalid_nested=nested_interaction_fixture(2);
    invalid_nested.runtime_entities[0].controllers.interactions.actions.push_back(
        {3,invalid_nested.world.entities[1].generation,{odh::routine_key(4098),{1,2,2,0},{},
            odh::VmCallReturnMode::interaction_next_tick}});
    rejects([&]{odh::encode_session_bundle(invalid_nested);},
        "observed child remains in queue beside its completion record");
    invalid_nested=nested_interaction_fixture(0);
    invalid_nested.runtime_entities[0].controllers.interactions.profile=odh::InteractionQueueProfile::legacy_fifo;
    rejects([&]{odh::encode_session_bundle(invalid_nested);},
        "legacy FIFO queue profile cannot claim nested active frames");
    invalid_nested=nested_interaction_fixture(2);
    invalid_nested.runtime_entities[0].vm.status=odh::VmStatus::running;
    invalid_nested.scheduler.entities[0].status=odh::VmStatus::running;
    rejects([&]{odh::encode_session_bundle(invalid_nested);},
        "interaction return cannot be saved while the VM is running");
    const auto object_bundle=object_thread_fixture();
    const auto object_bytes=odh::encode_session_bundle(object_bundle);
    const auto object_restored=odh::decode_session_bundle(object_bytes,object_bundle.resources);
    require(object_restored.object_threads.size()==1&&object_restored.object_threads[0].id==207&&
            object_restored.object_threads[0].guid==2933422533u&&object_restored.object_threads[0].sleep.job&&
            object_restored.object_threads[0].vm.pending&&
            odh::encode_session_bundle(object_restored)==object_bytes,
            "object Main VM, Sleep wait, resource identity and scheduler roster round-trip");
    auto bad_object=object_bundle;bad_object.scheduler.entities.back().id=208;
    rejects([&]{(void)odh::encode_session_bundle(bad_object);},
            "object VM missing from scheduler roster is rejected");
    bad_object=object_bundle;bad_object.object_threads[0].code_owners.clear();
    rejects([&]{(void)odh::encode_session_bundle(bad_object);},
            "object thread without exact code provenance is rejected");
    const auto route_bundle=local_route_fixture();
    const auto route_bytes=odh::encode_session_bundle(route_bundle);
    const auto route_restored=odh::decode_session_bundle(route_bytes,route_bundle.resources);
    require(route_restored.runtime_entities[0].route&&
            route_restored.runtime_entities[0].route->path==route_bundle.runtime_entities[0].route->path&&
            route_restored.runtime_entities[0].route->next==2&&
            odh::encode_session_bundle(route_restored)==route_bytes,
            "local Route45 continuation is bounded and canonical in a session archive");
    auto bad_route=route_bundle;bad_route.runtime_entities[0].route.reset();
    rejects([&]{odh::encode_session_bundle(bad_route);},"Route45 pending wait without path state is rejected");
    bad_route=route_bundle;bad_route.runtime_entities[0].route->same_room_verified=false;
    rejects([&]{odh::encode_session_bundle(bad_route);},"unverified cross-room route cannot be saved");
    bad_route=route_bundle;bad_route.runtime_entities[0].route->target_position.level=2;
    rejects([&]{odh::encode_session_bundle(bad_route);},"cross-floor Route45 state cannot be saved as a local route");
    bad_route=route_bundle;bad_route.runtime_entities[0].vm.pending->instruction.instruction.operands[2]=3;
    rejects([&]{odh::encode_session_bundle(bad_route);},"unsupported Route45 scope cannot be paired with a local route");
    bad_route=route_bundle;bad_route.runtime_entities[0].vm.pending->instruction.instruction.operands[4]=2;
    rejects([&]{odh::encode_session_bundle(bad_route);},"unsupported Route45 flags cannot be paired with a local route");
    bad_route=route_bundle;bad_route.world.entities[0].static_footprint=true;
    rejects([&]{odh::encode_session_bundle(bad_route);},"route footprint must match walking progress");
    auto go_here_route=route_bundle;
    auto& go_here_vm=go_here_route.runtime_entities[0].vm;
    go_here_vm.pending->instruction.instruction={27,254,255,{10,0,254,255,10,0,5,0}};
    go_here_route.runtime_entities[0].route->entry_flags=0;
    go_here_route.runtime_entities[0].route->call_failure_trees=true;
    const auto go_here_bytes=odh::encode_session_bundle(go_here_route);
    const auto go_here_restored=odh::decode_session_bundle(go_here_bytes,go_here_route.resources);
    require(go_here_restored.runtime_entities[0].route&&
            go_here_restored.runtime_entities[0].route->call_failure_trees&&
            go_here_restored.runtime_entities[0].route->entry_flags==0&&
            odh::encode_session_bundle(go_here_restored)==go_here_bytes,
            "Go Here local route and its failure behavior round-trip");
    const auto dynamic_route=dynamic_action_route_fixture();
    const auto dynamic_bytes=odh::encode_session_bundle(dynamic_route);
    const auto dynamic_restored=odh::decode_session_bundle(dynamic_bytes,dynamic_route.resources);
    require(dynamic_restored.runtime_entities[0].dynamic_action_owners.size()==1&&
            dynamic_restored.runtime_entities[0].dynamic_action_owners[0].linked_semiglobal==
                dynamic_route.runtime_entities[0].dynamic_action_owners[0].linked_semiglobal&&
            odh::encode_session_bundle(dynamic_restored)==dynamic_bytes,
            "dynamic route owner target, exact resource, GLOB link and closure hash round-trip");
    auto private_root_with_link=dynamic_route;
    private_root_with_link.runtime_entities[0].dynamic_action_owners[0].root_routine=4097;
    const auto private_root_link_bytes=odh::encode_session_bundle(private_root_with_link);
    const auto private_root_link_restored=odh::decode_session_bundle(private_root_link_bytes,
        private_root_with_link.resources);
    require(private_root_link_restored.runtime_entities[0].dynamic_action_owners[0].root_routine==4097&&
            private_root_link_restored.runtime_entities[0].dynamic_action_owners[0].linked_semiglobal==
                private_root_with_link.runtime_entities[0].dynamic_action_owners[0].linked_semiglobal&&
            odh::encode_session_bundle(private_root_link_restored)==private_root_link_bytes,
            "single-resident dynamic owner preserves a private root with its exact linked check bank");
    auto semiglobal_root_without_link=dynamic_route;
    semiglobal_root_without_link.runtime_entities[0].dynamic_action_owners[0].linked_semiglobal.reset();
    rejects([&]{odh::encode_session_bundle(semiglobal_root_without_link);},
            "single-resident semiglobal action root still requires its exact linked bank");
    auto legacy_route_archive=odh::decode_session_archive(route_bytes);
    auto legacy_route_section=std::find_if(legacy_route_archive.sections.begin(),legacy_route_archive.sections.end(),
        [](const auto& section){return section.id==std::array<char,4>{{'R','O','U','T'}};});
    require(legacy_route_section!=legacy_route_archive.sections.end(),"encoded dynamic route section exists");
    auto& legacy_route_data=legacy_route_section->data;
    constexpr std::size_t route_payload_offset=2+2+2+4+1;
    require(legacy_route_data.size()>route_payload_offset+2&&legacy_route_data[route_payload_offset]==2,
            "local route uses the version-2 payload");
    legacy_route_data[route_payload_offset]=1;legacy_route_data[route_payload_offset+1]=0;
    const auto legacy_record_size=static_cast<std::uint32_t>(legacy_route_data[6])|
        (static_cast<std::uint32_t>(legacy_route_data[7])<<8)|
        (static_cast<std::uint32_t>(legacy_route_data[8])<<16)|
        (static_cast<std::uint32_t>(legacy_route_data[9])<<24);
    require(legacy_record_size>0,"legacy route roster record has a payload");
    legacy_route_data.erase(legacy_route_data.begin()+10+legacy_record_size-1);
    const auto patched_record_size=legacy_record_size-1;
    for(unsigned n=0;n<4;++n)legacy_route_data[6+n]=static_cast<std::uint8_t>(patched_record_size>>(8*n));
    const auto legacy_route_bytes=odh::encode_session_archive(legacy_route_archive);
    const auto legacy_route_bundle=odh::decode_session_bundle(legacy_route_bytes,route_bundle.resources);
    require(legacy_route_bundle.runtime_entities[0].route&&
            !legacy_route_bundle.runtime_entities[0].route->call_failure_trees,
            "version-1 local routes load with their historical false failure-tree policy");
    auto bad_route_archive=odh::decode_session_archive(go_here_bytes);
    auto bad_route_section=std::find_if(bad_route_archive.sections.begin(),bad_route_archive.sections.end(),
        [](const auto& section){return section.id==std::array<char,4>{{'R','O','U','T'}};});
    require(bad_route_section!=bad_route_archive.sections.end(),"encoded route section exists for flag validation");
    const auto bad_route_record_size=static_cast<std::uint32_t>(bad_route_section->data[6])|
        (static_cast<std::uint32_t>(bad_route_section->data[7])<<8)|
        (static_cast<std::uint32_t>(bad_route_section->data[8])<<16)|
        (static_cast<std::uint32_t>(bad_route_section->data[9])<<24);
    bad_route_section->data[10+bad_route_record_size-1]=2;
    rejects([&]{(void)odh::decode_session_bundle(odh::encode_session_archive(bad_route_archive),
        go_here_route.resources);},"non-boolean Go Here route failure flag is rejected");
    auto bad_dynamic=dynamic_route;bad_dynamic.runtime_entities[0].dynamic_action_owners.clear();
    rejects([&]{odh::encode_session_bundle(bad_dynamic);},
            "Route45 VM reference to an absent dynamic action owner is rejected");
    bad_dynamic=dynamic_route;bad_dynamic.runtime_entities[0].dynamic_action_owners[0].target_guid++;
    rejects([&]{odh::encode_session_bundle(bad_dynamic);},
            "dynamic action owner with a changed target GUID is rejected");
    bad_dynamic=dynamic_route;bad_dynamic.runtime_entities[0].dynamic_action_owners[0].linked_semiglobal->entry="other.iff";
    rejects([&]{odh::encode_session_bundle(bad_dynamic);},
            "dynamic action owner with a mismatched GLOB entry is rejected");
    auto without_family_budget=source;without_family_budget.host.family_budget.reset();
    const auto legacy_bytes=odh::encode_session_bundle(without_family_budget);
    const auto legacy=odh::decode_session_bundle(legacy_bytes,source.resources);
    require(!legacy.host.family_budget&&odh::encode_session_bundle(legacy)==legacy_bytes,
        "legacy host without a saved family budget remains readable and canonical");
    auto simulated=source;simulated.simulation.emplace();
    simulated.simulation->avatars={{1,{}},{2,{}}};
    simulated.simulation->avatars[1].decay.fractions[4]=1000;
    const auto simulation_bytes=odh::encode_session_bundle(simulated);
    const auto simulation_restored=odh::decode_session_bundle(simulation_bytes,source.resources);
    require(simulation_restored.simulation && simulation_restored.simulation->avatars[1].decay.fractions[4]==1000 &&
        odh::encode_session_bundle(simulation_restored)==simulation_bytes,"SIMS v1 deterministic full round-trip");
    auto invalid_sim=simulated;invalid_sim.simulation->clock.ticks=1;
    rejects([&]{odh::encode_session_bundle(invalid_sim);},"simulation tick must match host");
    invalid_sim=simulated;invalid_sim.simulation->avatars[1].id=1;
    rejects([&]{odh::encode_session_bundle(invalid_sim);},"simulation roster IDs must match runtime roster");
    invalid_sim=simulated;invalid_sim.simulation->avatars[0].decay.fractions[0]=-1;
    rejects([&]{odh::encode_session_bundle(invalid_sim);},"invalid motive carry rejected");
    invalid_sim=simulated;invalid_sim.host.global_values.insert(invalid_sim.host.global_values.begin(),{0,12});
    rejects([&]{odh::encode_session_bundle(invalid_sim);},"clock globals cannot contradict simulation");
    auto sim_archive=odh::decode_session_archive(simulation_bytes);
    for(auto& section:sim_archive.sections)if(section.id==std::array<char,4>{{'S','I','M','S'}})section.data[0]=2;
    rejects([&]{odh::decode_session_bundle(odh::encode_session_archive(sim_archive));},"unknown SIMS version rejected");
    sim_archive=odh::decode_session_archive(simulation_bytes);
    for(auto& section:sim_archive.sections)if(section.id==std::array<char,4>{{'S','I','M','S'}})section.data.push_back(0);
    rejects([&]{odh::decode_session_bundle(odh::encode_session_archive(sim_archive));},"SIMS trailing bytes rejected");
    auto with_playback=source;
    odh::PlaybackClipSnapshot clip{};clip.resource.kind=odh::PlaybackResourceKey::Kind::animation;
    clip.resource.source=1;clip.resource.id=7;clip.resource.caller=1;clip.resource.actor_age=0;
    clip.actor=1;clip.actor_generation=source.world.entities[0].generation;
    clip.vm_generation=source.runtime_entities[0].vm.run_generation;clip.frame=2.5f;clip.speed=1.2f;
    clip.frames=12;clip.high=0;clip.ticks=3;
    with_playback.runtime_entities[0].playback.state=clip;
    with_playback.runtime_entities[0].playback.hands=odh::PlaybackHandsSnapshot{
        {odh::HandGesture::pointing,odh::HandGesture::fist},1,clip.actor_generation,clip.vm_generation};
    with_playback.runtime_entities[0].playback.last_tick=0;
    const auto playback_bytes=odh::encode_session_bundle(with_playback);
    const auto playback_restored=odh::decode_session_bundle(playback_bytes,source.resources);
    require(odh::encode_session_bundle(playback_restored)==playback_bytes&&
            playback_restored.runtime_entities[0].playback.state->frame==2.5f&&
            playback_restored.runtime_entities[0].playback.hands->value.left==odh::HandGesture::pointing,
            "nonempty Playback state and hands round-trip canonically");
    auto bad_playback=with_playback;
    ++bad_playback.runtime_entities[0].playback.state->actor_generation;
    rejects([&]{(void)odh::encode_session_bundle(bad_playback);},
            "stale Playback avatar generation rejected");
    bad_playback=source;
    odh::PlaybackResourceKey reset_carry{};reset_carry.kind=odh::PlaybackResourceKey::Kind::posture_reset;
    reset_carry.posture=2;reset_carry.caller=1;
    bad_playback.runtime_entities[0].playback.carry=reset_carry;
    rejects([&]{(void)odh::encode_session_bundle(bad_playback);},
            "posture reset key cannot be used as a carry resource");
    reset_carry.posture=-1;
    bad_playback.runtime_entities[0].playback.carry=reset_carry;
    require(!odh::encode_session_bundle(bad_playback).empty(),
            "default held-object carry animation has a stable snapshot key");
    bad_playback=source;
    bad_playback.runtime_entities[0].vm.status=odh::VmStatus::waiting;
    bad_playback.runtime_entities[0].vm.ticket_sequence=1;
    odh::VmPending animation_wait{};animation_wait.ticket=1;animation_wait.context.caller=1;
    animation_wait.instruction.depth=1;
    animation_wait.instruction.instruction=odh::Instruction{44,0,0,{}};
    bad_playback.runtime_entities[0].vm.pending=animation_wait;
    bad_playback.scheduler.entities[0].status=odh::VmStatus::waiting;
    rejects([&]{(void)odh::encode_session_bundle(bad_playback);},
            "pending opcode 44 without Playback state rejected");
    for(const bool sleep_wait:{true,false}) {
        const auto waiting=live_wait_fixture(sleep_wait);
        const auto waiting_bytes=odh::encode_session_bundle(waiting);
        const auto waiting_restored=odh::decode_session_bundle(waiting_bytes,waiting.resources);
        require(odh::encode_session_bundle(waiting_restored)==waiting_bytes,
                "real Sleep and IdleForInput waits survive bundle round-trip");
    }
    auto legacy_main_allow=live_wait_fixture(false);
    legacy_main_allow.runtime_entities[0].vm.pending->instruction.instruction.operands[2]=1;
    const auto legacy_allow_bytes=odh::encode_session_bundle(legacy_main_allow);
    const auto legacy_allow_restored=odh::decode_session_bundle(legacy_allow_bytes,legacy_main_allow.resources);
    require(legacy_allow_restored.runtime_entities[0].controllers.interactions.wait->deadline==
            legacy_main_allow.runtime_entities[0].controllers.interactions.wait->deadline&&
            odh::encode_session_bundle(legacy_allow_restored)==legacy_allow_bytes,
            "Main AllowPush=1 keeps legacy remaining-time wait deadlines on save and reopen");
    auto current_main_allow=legacy_main_allow;
    current_main_allow.runtime_entities[0].controllers.interactions.wait->deadline=
        current_main_allow.runtime_entities[0].controllers.interactions.idle_start+1;
    require(odh::decode_session_bundle(odh::encode_session_bundle(current_main_allow)).
        runtime_entities[0].controllers.interactions.wait->deadline==
        current_main_allow.runtime_entities[0].controllers.interactions.wait->deadline,
        "Main AllowPush=1 also accepts the current one-tick polling deadline");
    auto active_allow=generic_action_fixture();
    active_allow.runtime_entities[0].vm.pending->instruction.instruction.operands[2]=1;
    active_allow.runtime_entities[0].controllers.interactions.wait->deadline=
        active_allow.runtime_entities[0].controllers.interactions.idle_start+1;
    require(odh::decode_session_bundle(odh::encode_session_bundle(active_allow)).
        runtime_entities[0].controllers.interactions.wait->deadline==
        active_allow.runtime_entities[0].controllers.interactions.wait->deadline,
        "active action AllowPush=1 uses its one-tick wait");
    auto stale_active_allow=generic_action_fixture();
    stale_active_allow.runtime_entities[0].vm.pending->instruction.instruction.operands[2]=1;
    rejects([&]{(void)odh::encode_session_bundle(stale_active_allow);},
        "active action AllowPush=1 rejects a legacy remaining-time deadline");
    const auto animation_waiting=live_playback_wait_fixture();
    const auto animation_bytes=odh::encode_session_bundle(animation_waiting);
    const auto animation_restored=odh::decode_session_bundle(animation_bytes,animation_waiting.resources);
    require(animation_restored.runtime_entities[0].playback.state->wait.has_value()&&
            odh::encode_session_bundle(animation_restored)==animation_bytes,
            "real pending opcode 44 Playback wait survives bundle round-trip");
    auto broken_animation=animation_waiting;
    ++broken_animation.runtime_entities[0].playback.state->wait->expected;
    rejects([&]{(void)odh::encode_session_bundle(broken_animation);},
            "Playback event output mismatch with pending opcode 44 rejected");
    for(const std::uint8_t mode_flags:{std::uint8_t{1},std::uint8_t{16}}) {
        auto nonblocking=animation_waiting;
        nonblocking.runtime_entities[0].vm.pending->instruction.instruction.operands[5]=mode_flags;
        rejects([&]{(void)odh::encode_session_bundle(nonblocking);},
                "nonblocking opcode 44 mode cannot have a Playback VM wait");
    }
    broken_animation=with_playback;
    broken_animation.runtime_entities[0].playback.state->queue.push_back(1);
    auto tight_playback_limits=odh::SessionBundleLimits{};tight_playback_limits.max_playback_queue=0;
    rejects([&]{(void)odh::encode_session_bundle(broken_animation,tight_playback_limits);},
            "oversized Playback queue rejected");
    broken_animation=with_playback;broken_animation.runtime_entities[0].playback.state->end=true;
    broken_animation.runtime_entities[0].playback.state->loop=true;
    rejects([&]{(void)odh::encode_session_bundle(broken_animation);},
            "Playback state cannot be both ended and looping");
    broken_animation=with_playback;broken_animation.runtime_entities[0].playback.state->end=true;
    rejects([&]{(void)odh::encode_session_bundle(broken_animation);},
            "forward Playback cannot end before its last frame");

    for(std::size_t size=0;size<bytes.size();++size) {
        std::vector<std::uint8_t> prefix(bytes.begin(),bytes.begin()+static_cast<std::ptrdiff_t>(size));
        rejects([&]{(void)odh::decode_session_bundle(prefix);},"truncated bundle rejected");
    }
    auto wrong_resource=source.resources;
    wrong_resource.resource_fingerprint[0]^=1;
    rejects([&]{(void)odh::decode_session_bundle(bytes,wrong_resource);},"resource mismatch rejected");

    auto archive=odh::decode_session_archive(bytes);
    archive.sections.pop_back();
    rejects([&]{(void)odh::decode_session_bundle(odh::encode_session_archive(archive));},
            "missing controller section rejected");

    archive=odh::decode_session_archive(bytes);
    archive.sections.push_back({{{'N','E','W','!'}},true,{}});
    rejects([&]{(void)odh::encode_session_archive(archive);},"invalid section ID rejected");
    archive.sections.back().id={{'N','E','W','1'}};
    rejects([&]{(void)odh::decode_session_bundle(odh::encode_session_archive(archive));},
            "unknown required section rejected");
    archive.sections.back().required=false;
    require(odh::decode_session_bundle(odh::encode_session_archive(archive)).runtime_entities.size()==2,
            "unknown optional section is tolerated");

    archive=odh::decode_session_archive(bytes);
    for(auto& section:archive.sections)
        if(section.id==std::array<char,4>{{'C','T','R','L'}}){
            require(section.data.size()>7,"controller roster has records");
            section.data[4]=3; // First controller ID, with valid archive checksum.
            break;
        }
    rejects([&]{(void)odh::decode_session_bundle(odh::encode_session_archive(archive));},
            "mismatched controller roster rejected");

    archive=odh::decode_session_archive(bytes);
    for(auto& section:archive.sections)
        if(section.id==std::array<char,4>{{'H','O','S','T'}}){
            section.data.push_back(0);
            break;
        }
    rejects([&]{(void)odh::decode_session_bundle(odh::encode_session_archive(archive));},
            "trailing host bytes rejected with valid archive checksum");

    const auto reject_controller_link=[&](auto change,const char* message){
        auto changed=source;
        change(changed.runtime_entities[0].controllers);
        rejects([&]{(void)odh::encode_session_bundle(changed);},message);
        auto packaged=odh::decode_session_archive(bytes);
        replace_runtime_sections(packaged,changed);
        rejects([&]{(void)odh::decode_session_bundle(odh::encode_session_archive(packaged));},message);
    };
    reject_controller_link([](auto& state){++state.interactions.actor_generation;},
                           "stale interaction actor generation rejected");
    reject_controller_link([](auto& state){++state.interactions.run_generation;},
                           "stale interaction VM generation rejected");
    reject_controller_link([&](auto& state){
        state.sleep.actor=odh::SleepActorSnapshot{2,source.world.entities[1].generation,0};
    },"sleep actor from another resident rejected");
    reject_controller_link([&](auto& state){
        state.sleep.actor=odh::SleepActorSnapshot{1,source.world.entities[0].generation+1,0};
    },"stale sleep actor generation rejected");
    reject_controller_link([&](auto& state){
        state.sleep.actor=odh::SleepActorSnapshot{1,source.world.entities[0].generation,1};
    },"stale sleep VM generation rejected");
    reject_controller_link([](auto& state){
        state.interactions.sequence=1;
        state.interactions.actions.push_back({1,1,
            {odh::routine_key(4096),{1,99,99,0},{},odh::VmCallReturnMode::interaction_next_tick}});
    },"missing queued interaction target rejected");
    reject_controller_link([](auto& state){
        state.interactions.sequence=1;
        state.interactions.actions.push_back({1,99,
            {odh::routine_key(4096),{1,2,2,0},{},odh::VmCallReturnMode::interaction_next_tick}});
    },"stale queued interaction target generation rejected");

    const auto reject_vm_controller_link=[&](auto change,const char* message){
        auto changed=source;
        change(changed.runtime_entities[0].vm,changed.runtime_entities[0].controllers);
        changed.scheduler.entities[0].status=changed.runtime_entities[0].vm.status;
        changed.scheduler.entities[0].steps=changed.runtime_entities[0].vm.steps;
        changed.scheduler.entities[0].run_generation=changed.runtime_entities[0].vm.run_generation;
        rejects([&]{(void)odh::encode_session_bundle(changed);},message);
        auto packaged=odh::decode_session_archive(bytes);
        replace_runtime_sections(packaged,changed);
        rejects([&]{(void)odh::decode_session_bundle(odh::encode_session_archive(packaged));},message);
    };
    reject_vm_controller_link([](auto&,auto& controls){
        controls.sleep.actor=odh::SleepActorSnapshot{1,1,0};
        controls.sleep.job=odh::SleepJobSnapshot{1,0,2,0};
    },"Sleep job without pending VM wait rejected");
    reject_vm_controller_link([](auto&,auto& controls){
        controls.sleep.actor=odh::SleepActorSnapshot{1,1,0};
        controls.sleep.notify_resume=true;
    },"Sleep notification without resumed VM frame rejected");
    reject_vm_controller_link([](auto&,auto& controls){
        controls.interactions.wait=odh::InteractionWaitSnapshot{1,2};
    },"interaction wait without pending VM call rejected");
    reject_vm_controller_link([](auto& vm,auto& controls){
        vm.ticket_sequence=1;
        controls.interactions.sequence=1;
        controls.interactions.actions.push_back({1,1,
            {odh::routine_key(4096),{1,2,2,0},{},odh::VmCallReturnMode::interaction_next_tick}});
        controls.interactions.active=odh::InteractionActiveSnapshot{1,1};
    },"active interaction without VM frame or return rejected");
    reject_vm_controller_link([](auto& vm,auto& controls){
        vm.ticket_sequence=1;
        controls.interactions.sequence=1;
        controls.interactions.completion=odh::InteractionCompletion{1,1,0,true};
        controls.interactions.resume_tick=1;
    },"interaction resume without matching VM return rejected");
    reject_vm_controller_link([](auto& vm,auto&){
        vm.status=odh::VmStatus::waiting;
        vm.ticket_sequence=1;
        odh::VmPending pending{};
        pending.ticket=1;
        pending.instruction.key=odh::routine_key(4096);
        pending.instruction.instruction=odh::Instruction{0,0,0,{}};
        pending.context.caller=1;
        vm.pending=pending;
    },"pending VM Sleep without controller job rejected");
    reject_vm_controller_link([](auto& vm,auto& controls){
        vm.status=odh::VmStatus::waiting;
        vm.ticket_sequence=2;
        vm.stack.push_back({odh::routine_key(4096),0,{1,1,1,0},{3},{}});
        odh::VmPending pending{};
        pending.ticket=1;
        pending.instruction.key=odh::routine_key(4096);
        pending.instruction.instruction=odh::Instruction{0,0,0,{}};
        pending.context.caller=1;
        vm.pending=pending;
        controls.sleep.actor=odh::SleepActorSnapshot{1,1,0};
        controls.sleep.job=odh::SleepJobSnapshot{2,0,4,0};
    },"Sleep job ticket different from pending VM ticket rejected");
    reject_vm_controller_link([](auto& vm,auto& controls){
        vm.status=odh::VmStatus::waiting;
        vm.ticket_sequence=1;
        vm.stack.push_back({odh::routine_key(4096),0,{1,1,1,0},{3},{}});
        odh::VmPending pending{};
        pending.ticket=1;
        pending.instruction.key=odh::routine_key(4096);
        pending.instruction.instruction=odh::Instruction{17,0,0,{0,0,0,0,0,0,0,0}};
        pending.context.caller=1;
        vm.pending=pending;
        controls.interactions.wait=odh::InteractionWaitSnapshot{1,99};
    },"interaction wait deadline different from pending VM argument rejected");

    auto bad_link=source;
    bad_link.runtime_entities[1].vm.steps=1;
    rejects([&]{(void)odh::encode_session_bundle(bad_link);},"scheduler and VM mismatch rejected");
    bad_link=source;
    bad_link.host.global_values.push_back({17,5});
    rejects([&]{(void)odh::encode_session_bundle(bad_link);},"duplicate host global rejected");
    bad_link=source;
    bad_link.world.entities.erase(bad_link.world.entities.begin());
    rejects([&]{(void)odh::encode_session_bundle(bad_link);},"missing world actor rejected");
    bad_link=source;
    bad_link.host.neighborhood_mode=7;
    require(odh::decode_session_bundle(odh::encode_session_bundle(bad_link)).host.neighborhood_mode==7,
            "Makin' Magic neighborhood mode is preserved");

    std::cout<<"session bundle checks passed\n";
}
