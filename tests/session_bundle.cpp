#include "odh/session_bundle.hpp"
#include "odh/seated_session_continuation.hpp"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <utility>

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
    // This hand-authored base exercises old value archives. Live fixtures
    // below capture current VM snapshots and their explicit ActionTree marker.
    for(auto& runtime:bundle.runtime_entities)runtime.vm.action_tree_known=false;
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

odh::SessionBundle immediate_interaction_fixture(bool returned=false) {
    auto bundle=fixture();
    bundle.runtime_entities.resize(1);bundle.scheduler.entities.resize(1);
    auto& runtime=bundle.runtime_entities.front();
    auto& vm=runtime.vm;
    auto& interactions=runtime.controllers.interactions;
    const auto main_key=odh::routine_key(4095),child_key=odh::routine_key(4098);
    const odh::VmContext parent_context{1,1,1,0},child_context{1,2,2,0};
    const odh::Instruction sleep{0,254,255,{0,0,0,0,0,0,0,0}};
    vm.status=returned?odh::VmStatus::yielded:odh::VmStatus::waiting;
    vm.steps=10;vm.ticket_sequence=3;vm.run_generation=1;
    vm.stack={{main_key,0,parent_context,{5},{},odh::VmCallReturnMode::ordinary,0}};
    vm.immediate_continuations.push_back({2,1,odh::VmStatus::waiting,
        odh::VmPending{1,odh::VmTrace{9,1,main_key,0,sleep,0},parent_context}});
    interactions.actor_id=1;interactions.capacity=32;interactions.actor_generation=bundle.world.entities[0].generation;
    interactions.run_generation=1;interactions.sequence=1;
    const auto target_generation=bundle.world.entities[1].generation;
    interactions.actions.push_back({1,target_generation,
        {child_key,child_context,{},odh::VmCallReturnMode::immediate},0,1,
        odh::InteractionQueueMode::idle,4});
    interactions.active=odh::InteractionActiveSnapshot{1,2};
    runtime.controllers.schedule_idle_start=0;
    runtime.controllers.sleep.actor=odh::SleepActorSnapshot{1,interactions.actor_generation,vm.run_generation};
    if(returned){
        vm.immediate_return=odh::VmImmediateReturn{2,true,0,vm.run_generation};
    }else{
        const odh::Instruction create{42,254,255,{0,0,0,0,0,0,0,0}};
        vm.stack.push_back({child_key,0,child_context,{},{},odh::VmCallReturnMode::immediate,2});
        vm.pending=odh::VmPending{3,odh::VmTrace{vm.steps-1,2,child_key,0,create,0},child_context};
    }
    bundle.scheduler.entities.front().id=1;
    bundle.scheduler.entities.front().run_generation=vm.run_generation;
    bundle.scheduler.entities.front().steps=vm.steps;
    bundle.scheduler.entities.front().status=vm.status;
    return bundle;
}

odh::SessionBundle parked_playback_parent_fixture(bool returned=false,std::uint8_t mode_flags=0) {
    auto bundle=immediate_interaction_fixture(returned);
    auto& runtime=bundle.runtime_entities.front();
    auto& pending=*runtime.vm.immediate_continuations.front().parent_pending;
    pending.instruction.instruction={44,254,255,{7,0,0,0,1,mode_flags,1,0}};
    odh::PlaybackClipSnapshot clip{};
    clip.resource.kind=odh::PlaybackResourceKey::Kind::animation;
    clip.resource.source=2;clip.resource.id=9;clip.resource.caller=1;clip.resource.callee=2;
    clip.resource.stack_object=2;clip.resource.actor_age=0;clip.resource.code_owner=0;
    clip.actor=1;clip.actor_generation=bundle.world.entities.front().generation;
    clip.vm_generation=runtime.vm.run_generation;clip.frame=0;clip.speed=1;clip.frames=12;
    runtime.playback.state=clip;
    runtime.playback.hands=odh::PlaybackHandsSnapshot{{},clip.actor,clip.actor_generation,clip.vm_generation};
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

void carried_route_archive_tests(){
    const auto legacy=local_route_fixture();const auto legacy_bytes=odh::encode_session_bundle(legacy);
    auto carried=legacy;auto& route=*carried.runtime_entities.front().route;
    auto& actor=carried.world.entities[0];auto& target=carried.world.entities[1];
    require(!actor.slots.empty(),"carried codec fixture has a real actor slot");
    actor.slots[0]=target.id;target.attachment=odh::Attachment{actor.id,0,true};
    const auto original=*target.position;
    target.position=odh::EntityPosition{actor.position->x,actor.position->y,actor.position->level,original.direction};
    route.carried_target.push_back({{target.id,target.generation},{actor.id,actor.generation},0,original,target.radian_direction,true});
    const auto bytes=odh::encode_session_bundle(carried);auto loaded=odh::decode_session_bundle(bytes);
    const auto& chain=loaded.runtime_entities.front().route->carried_target;
    require(chain.size()==1&&chain[0].child.id==2&&chain[0].parent.id==1&&chain[0].slot==0&&
        chain[0].original==original&&chain[0].positioned&&odh::encode_session_bundle(loaded)==bytes,
        "carried target preserves frozen ancestry and anchor while live object follows actor");
    loaded.runtime_entities.front().route->carried_target.clear();
    loaded.world.entities[0].slots[0]=0;loaded.world.entities[1].attachment.reset();loaded.world.entities[1].position=original;
    require(odh::encode_session_bundle(loaded)==legacy_bytes,"empty carried state preserves exact historical local-route bytes");
    auto stack=carried;auto& runtime=stack.runtime_entities.front();odh::RouteJobSnapshot job;
    static_cast<odh::LocalRouteSnapshot&>(job)=*runtime.route;job.pending=*runtime.vm.pending;job.route_frame=runtime.vm.stack.back();
    runtime.route.reset();runtime.route_stack=odh::RouteStackSnapshot{{job},stack.host.tick,{}};
    const auto stacked=odh::encode_session_bundle(stack);const auto stacked_loaded=odh::decode_session_bundle(stacked);
    require(stacked_loaded.runtime_entities.front().route_stack->jobs.front().carried_target.size()==1&&
        odh::encode_session_bundle(stacked_loaded)==stacked,"full route stack retains carried ancestry without replay");
    const auto reject=[&](auto edit,const char* why){auto invalid=carried;edit(invalid);rejects([&]{odh::encode_session_bundle(invalid);},why);};
    reject([](auto& b){b.runtime_entities.front().route->carried_target.front().child.generation++;},"carried target generation is authenticated");
    reject([](auto& b){b.runtime_entities.front().route->carried_target.front().parent.generation++;},"carried parent generation is authenticated");
    reject([](auto& b){b.runtime_entities.front().route->carried_target.front().parent.id=2;},"carried chain must end at its actor");
    reject([](auto& b){b.runtime_entities.front().route->carried_target.push_back(b.runtime_entities.front().route->carried_target.front());},"duplicate carried children cannot form ancestry");
    reject([](auto& b){auto link=b.runtime_entities.front().route->carried_target.front();b.runtime_entities.front().route->carried_target.assign(4097,link);},"carried link count is bounded");
    reject([](auto& b){b.runtime_entities.front().route->carried_target.front().slot=4096;},"carried slot is bounded");
    reject([](auto& b){b.world.entities[0].slots[0]=0;},"carried ancestry requires reciprocal slots");
    reject([](auto& b){b.world.entities[1].position->x++;},"positioned carried target must follow parent pose");
    reject([](auto& b){b.runtime_entities.front().route->carried_target.front().original.x++;},"carried anchor cannot drift");
    reject([](auto& b){b.runtime_entities.front().route->carried_target.front().radian_direction=std::numeric_limits<float>::quiet_NaN();},"carried heading must be finite");
    auto unpositioned=carried;unpositioned.world.entities[1].position=original;
    unpositioned.world.entities[1].attachment->positioned=false;
    unpositioned.runtime_entities.front().route->carried_target.front().positioned=false;
    const auto waiting=odh::encode_session_bundle(unpositioned);
    require(odh::encode_session_bundle(odh::decode_session_bundle(waiting))==waiting,
        "original unpositioned attachment can be saved before the first carried publication");
    auto archive=odh::decode_session_archive(bytes);
    for(auto& section:archive.sections)if(section.id==std::array<char,4>{{'R','O','U','T'}}){
        auto& data=section.data;require(data.size()>17&&data[10]==1&&data[11]==15&&data[12]==0,"carried route uses canonical v15 envelope");
        const auto size=std::uint32_t(data[13])|(std::uint32_t(data[14])<<8)|(std::uint32_t(data[15])<<16)|(std::uint32_t(data[16])<<24);
        const auto count=17u+size;require(count+2<=data.size(),"carried count follows bounded legacy payload");data[count]=0;data[count+1]=0;
    }
    rejects([&]{odh::decode_session_bundle(odh::encode_session_archive(archive));},"empty carried extension is noncanonical");
}

void architecture_route_event_archive_tests(){
    auto legacy=local_route_fixture();const auto legacy_bytes=odh::encode_session_bundle(legacy);
    auto marked=legacy;marked.runtime_entities.front().route->room_route_invalid=true;
    const auto bytes=odh::encode_session_bundle(marked);auto reopened=odh::decode_session_bundle(bytes);
    require(reopened.runtime_entities.front().route->room_route_invalid&&odh::encode_session_bundle(reopened)==bytes,
        "source RoomRouteInvalid survives complete native archive without executing an event");
    reopened.runtime_entities.front().route->room_route_invalid=false;
    require(odh::encode_session_bundle(reopened)==legacy_bytes,"absent/default architecture route flag preserves historical bytes");
    auto stack=marked;auto&runtime=stack.runtime_entities.front();odh::RouteJobSnapshot job;
    static_cast<odh::LocalRouteSnapshot&>(job)=*runtime.route;job.pending=*runtime.vm.pending;job.route_frame=runtime.vm.stack.back();
    job.frame.state=odh::RouteFrameState::initial;job.room_route_replan_active=true;
    runtime.route.reset();runtime.route_stack=odh::RouteStackSnapshot{{job},stack.host.tick,{}};
    const auto stacked=odh::encode_session_bundle(stack);const auto loaded=odh::decode_session_bundle(stacked);
    require(loaded.runtime_entities.front().route_stack->jobs.front().room_route_replan_active&&
        loaded.runtime_entities.front().route_stack->jobs.front().room_route_invalid&&odh::encode_session_bundle(loaded)==stacked,
        "invalidation plus pending room traversal retain original CurRoute/path under stack3");
    auto malformed=stack;malformed.runtime_entities.front().route_stack->jobs.front().deferred_room_route=true;
    rejects([&]{odh::encode_session_bundle(malformed);},"selected CurRoute room replan cannot impersonate initial unresolved SLOT traversal");
    malformed=stack;malformed.runtime_entities.front().route_stack->jobs.front().actor_generation++;
    rejects([&]{odh::encode_session_bundle(malformed);},"architecture extension cannot weaken original actor/frame ownership");
}

void unplaced_room_failure_archive_tests(){
    auto bundle=local_route_fixture();auto& runtime=bundle.runtime_entities.front();
    odh::RouteJobSnapshot job;static_cast<odh::LocalRouteSnapshot&>(job)=*runtime.route;
    job.pending=*runtime.vm.pending;job.route_frame=runtime.vm.stack.back();runtime.route.reset();
    job.expected={-32768,-32768,1,0};job.path.clear();job.next=1;job.entry_flags=0;job.facing=0;
    job.walking_started=false;job.same_room_verified=false;job.call_failure_trees=false;job.frame={};
    job.failure_pending=true;job.failure_code=2;job.failure_reason="source InitRoutes room0 has no portals";
    job.pending.instruction.instruction.operands[4]=1;runtime.vm.pending=job.pending;
    odh::SourceUnplacedRoomFailure origin;origin.slot_version=10;origin.slot_index=1;
    origin.slot.type=3;origin.target_room=2;job.unplaced_room_failure=origin;
    bundle.world.entities.front().position.reset();bundle.world.entities.front().static_footprint=true;
    bundle.world.entities.front().person_data[62]=2;
    runtime.route_stack=odh::RouteStackSnapshot{{job},bundle.host.tick,{}};
    const auto bytes=odh::encode_session_bundle(bundle);const auto cold=odh::decode_session_bundle(bytes);
    require(odh::encode_session_bundle(cold)==bytes&&
        odh::is_source_unplaced_room_failure(cold.runtime_entities.front().route_stack->jobs.front())&&
        !cold.world.entities.front().position,
        "RSTK5 round trip preserves exact pre-selection failure and true position absence");
    // The same raw27 request has source semantics for an ordinary target;
    // it is the historical UI path only when the actual target is a ghost.
    auto relative=bundle;auto& rr=relative.runtime_entities.front();auto& source=rr.route_stack->jobs.front();
    source.pending.instruction.instruction={27,254,255,{10,0,254,255,10,0,5,0}};source.call_failure_trees=true;
    const auto transient=odh::make_source_goto_relative_slot(source.pending.instruction.instruction);require(transient.has_value(),"reconstruct ordinary same-byte relative SLOT");
    source.unplaced_room_failure->slot_version=transient->version;source.unplaced_room_failure->slot_index=0;source.unplaced_room_failure->slot=transient->records.front();
    rr.vm.pending=source.pending;
    const auto relative_bytes=odh::encode_session_bundle(relative);require(odh::encode_session_bundle(odh::decode_session_bundle(relative_bytes))==relative_bytes,"ordinary target with historical-marker bytes retains exact RSTK5 source roundtrip");
    auto ghost=relative;const auto target_id=source.target;auto target=std::find_if(ghost.world.entities.begin(),ghost.world.entities.end(),[&](const auto& e){return e.id==target_id;});require(target!=ghost.world.entities.end(),"wire fixture target exists");target->ui_ghost=true;
    const auto refuses_legacy=[&](const auto& action){bool refused=false;try{action();}catch(const odh::FormatError& error){refused=std::string(error.what()).find("legacy UI-only marker")!=std::string::npos;}require(refused,"wire guard specifically rejects source NoRoom witness on exact legacy ghost target");};
    refuses_legacy([&]{(void)odh::encode_session_bundle(ghost);});
    auto forged_archive=odh::decode_session_archive(relative_bytes);bool replaced=false;for(auto& section:forged_archive.sections)if(section.id==std::array<char,4>{{'W','R','L','D'}}){section.data=odh::encode_world_snapshot(ghost.world);replaced=true;}require(replaced,"replace only exact World ghost flag in otherwise canonical positive archive");
    refuses_legacy([&]{(void)odh::decode_session_bundle(odh::encode_session_archive(forged_archive));});
    for(unsigned field=0;field<5;++field){auto bad=bundle;auto& route=bad.runtime_entities.front().route_stack->jobs.front();
        if(field==0)route.unplaced_room_failure->slot.flags|=0x0800;
        if(field==1)route.unplaced_room_failure->slot_index++;
        if(field==2)route.unplaced_room_failure->target_room=0;
        if(field==3)route.failure_code=3;
        if(field==4)route.path.push_back({20,20});
        rejects([&]{odh::encode_session_bundle(bad);},"forged nonspatial failure cannot gain spatial or source authority");
    }
}

void route_stack_archive_tests() {
    for(const bool returned:{false,true})for(const auto opcode:{27,45}){
        auto parked=immediate_interaction_fixture(returned);
        const auto local=local_route_fixture();parked.world=local.world;
        auto& runtime=parked.runtime_entities.front();auto& vm=runtime.vm;
        auto& continuation=vm.immediate_continuations.front();auto& pending=*continuation.parent_pending;
        auto& frame=vm.stack.front();frame.key=odh::routine_key(4096);frame.context={1,2,2,1};
        pending.context=frame.context;pending.instruction.key=frame.key;pending.instruction.code_owner=1;
        pending.instruction.instruction={45,254,255,{1,0,2,0,0,0,0,0}};
        odh::RouteJobSnapshot job;static_cast<odh::LocalRouteSnapshot&>(job)=*local.runtime_entities.front().route;
        job.run_generation=vm.run_generation;
        if(opcode==27){
            pending.instruction.instruction={27,254,255,{5,0,254,254,0,0,2,0}};
            job.entry_flags=0;job.facing=0;job.call_failure_trees=false;
            job.path.clear();for(int x=4;x<=28;++x)job.path.push_back({x,12});
        }
        job.pending=pending;job.route_frame=frame;
        runtime.route_stack=odh::RouteStackSnapshot{{job},parked.host.tick,{}};
        const auto bytes=odh::encode_session_bundle(parked);
        require(odh::encode_session_bundle(odh::decode_session_bundle(bytes))==bytes,
            "active and returned immediate children preserve exact parked routing manifest without wire changes");
        auto malformed=parked;malformed.runtime_entities.front().route_stack->jobs.front().pending.context.code_owner=77;
        rejects([&]{odh::encode_session_bundle(malformed);},"parked routing manifest cannot replace its exact parent owner");
        malformed=parked;malformed.runtime_entities.front().route_stack.reset();
        rejects([&]{odh::encode_session_bundle(malformed);},"parked route pending requires its corresponding route manifest");
    }

    const auto legacy=local_route_fixture();const auto legacy_bytes=odh::encode_session_bundle(legacy);
    auto bundle=legacy;auto& runtime=bundle.runtime_entities.front();
    odh::RouteJobSnapshot child;static_cast<odh::LocalRouteSnapshot&>(child)=*runtime.route;
    child.pending=*runtime.vm.pending;child.route_frame=runtime.vm.stack.back();runtime.route.reset();
    runtime.route_stack=odh::RouteStackSnapshot{{child},bundle.host.tick,{}};
    const auto single_bytes=odh::encode_session_bundle(bundle);const auto single=odh::decode_session_bundle(single_bytes);
    require(single.runtime_entities.front().route_stack&&
        single.runtime_entities.front().route_stack->jobs.size()==1&&
        odh::encode_session_bundle(single)==single_bytes,"single stack request uses canonical bounded RSTK continuation");
    const auto reject=[&](const auto& change,const char* description){auto bad=bundle;change(bad);
        rejects([&]{(void)odh::encode_session_bundle(bad);},description);};
    reject([](auto& value){value.runtime_entities.front().route=value.runtime_entities.front().route_stack->jobs.back();},
        "legacy and nested route payloads are mutually exclusive");
    reject([](auto& value){value.runtime_entities.front().route_stack->jobs.back().route_frame.locals.push_back(99);},
        "frozen route frame must match all live frame values");
    reject([](auto& value){value.runtime_entities.front().route_stack->last_tick=value.host.tick+1;},
        "route stack clock cannot run ahead of host");
    reject([](auto& value){value.world.entities.front().position->x++;},"active stack route verifies live pose");
    odh::RouteJobSnapshot parent=child;parent.path.clear();parent.next=1;parent.entry_flags=0;parent.facing=0;
    parent.same_room_verified=false;parent.call_failure_trees=true;parent.frame.state=odh::RouteFrameState::room_portal;
    parent.deferred_room_route=true;parent.room_context_open=true;parent.external_frame_active=true;
    parent.external_frame_owner=1;parent.external_frame_call=odh::VmCall{odh::routine_key(4097),{1,2,2,1},{},odh::VmCallReturnMode::ordinary};
    parent.replay_frame=parent.route_frame;parent.replay_instruction=parent.pending.instruction.instruction;
    parent.portal_steps=1;parent.expected.x=4; // Historical parent pose differs from the active child.
    child.ticket=2;child.pending.ticket=2;child.pending.instruction.depth=2;child.pending.instruction.key=odh::routine_key(4097);
    child.route_frame.key=odh::routine_key(4097);child.route_frame.return_mode=odh::VmCallReturnMode::external_frame_next_tick;
    child.route_frame.invocation_ticket=1;
    runtime.vm.stack.push_back(child.route_frame);runtime.vm.pending=child.pending;runtime.vm.suspended_pending={parent.pending};runtime.vm.ticket_sequence=2;
    odh::RoutePortalContextSnapshot context;context.ticket=1;context.pending_call=parent.external_frame_call;
    context.pending_edge=odh::RoomPortalEdge{1,2,2,child.target_position};context.portal_generation=child.target_generation;
    runtime.route_stack=odh::RouteStackSnapshot{{parent,child},bundle.host.tick,{context}};
    const auto nested_bytes=odh::encode_session_bundle(bundle);const auto nested=odh::decode_session_bundle(nested_bytes);
    for(unsigned phase=0;phase<3;++phase){
        // Make the source topology explicit: ordinary Main -> queued user
        // action -> external route portal -> parked route -> privacy immediate.
        auto mixed=bundle;auto& state=mixed.runtime_entities.front();auto& vm=state.vm;
        auto& queue=state.controllers.interactions;
        vm.stack.insert(vm.stack.begin(),{odh::routine_key(4095),0,{1,1,1,0},{},{},odh::VmCallReturnMode::ordinary,0});
        vm.steps=12;vm.run_generation=1;vm.ticket_sequence=4;
        queue.run_generation=1;queue.suspended_active.push_back(*queue.active);
        queue.sequence=2;queue.next_queue_uid=2;queue.profile=odh::InteractionQueueProfile::ts1_priority;
        for(auto& job:state.route_stack->jobs){
            ++job.ticket;++job.pending.ticket;++job.pending.instruction.depth;job.run_generation=1;
            job.pending.instruction.step=job.ticket;
            if(job.route_frame.invocation_ticket&&job.route_frame.return_mode==odh::VmCallReturnMode::external_frame_next_tick)
                ++job.route_frame.invocation_ticket;
        }
        for(auto& portal:state.route_stack->portal_contexts)++portal.ticket;
        vm.stack[2]=state.route_stack->jobs[1].route_frame;
        vm.suspended_pending={state.route_stack->jobs[0].pending};
        const auto parked=state.route_stack->jobs[1].pending;
        vm.pending.reset();vm.status=phase==2?odh::VmStatus::yielded:phase==1?odh::VmStatus::waiting:odh::VmStatus::running;
        vm.immediate_continuations={{4,3,odh::VmStatus::waiting,parked}};
        state.controllers.schedule_idle_start=0;
        const odh::VmContext privacy_context{1,2,2,1};const auto privacy_key=odh::routine_key(4100);
        queue.actions.push_back({2,mixed.world.entities[1].generation,
            {privacy_key,privacy_context,{},odh::VmCallReturnMode::immediate},1,1,odh::InteractionQueueMode::idle,4});
        queue.active=odh::InteractionActiveSnapshot{2,4};
        if(phase==2)vm.immediate_return=odh::VmImmediateReturn{4,true,1,1};
        else{
            vm.stack.push_back({privacy_key,0,privacy_context,{},{},odh::VmCallReturnMode::immediate,4});
            if(phase==1){vm.ticket_sequence=5;vm.pending=odh::VmPending{5,
                odh::VmTrace{11,4,privacy_key,0,odh::Instruction{42,254,255,{}},1},privacy_context};}
        }
        mixed.scheduler.entities[0].steps=vm.steps;mixed.scheduler.entities[0].run_generation=1;
        mixed.scheduler.entities[0].status=vm.status;
        const auto encoded=odh::encode_session_bundle(mixed);
        require(odh::encode_session_bundle(odh::decode_session_bundle(encoded))==encoded,
            "whole SessionBundle round-trips queued action portal parked route and immediate runnable/waiting/returned child");
        auto malformed=mixed;++malformed.runtime_entities[0].vm.suspended_pending[0].context.code_owner;
        rejects([&]{odh::encode_session_bundle(malformed);},"mixed bundle cannot substitute external parent resource owner");
        malformed=mixed;++malformed.runtime_entities[0].vm.immediate_continuations[0].parent_pending->instruction.depth;
        rejects([&]{odh::encode_session_bundle(malformed);},"mixed bundle cannot move parked route into another action depth");
    }
    const auto& saved=*nested.runtime_entities.front().route_stack;
    require(saved.jobs.size()==2&&saved.jobs.front().path.empty()&&saved.jobs.front().deferred_room_route&&
        saved.jobs.front().replay_instruction&&saved.portal_contexts.front().pending_call&&
        odh::encode_session_bundle(nested)==nested_bytes,"suspended deferred parent, active child and host portal context round-trip");
    {
        // The source creates a transient ranged SLOT before a room portal has
        // selected any goal. Keep that original instruction while its portal
        // child owns the live VM wait, rather than pretending it is OnTopOf.
        for(const auto flags:{0U,2U,3U,6U,0xfdU,0xffU})for(const auto direction:{0U,254U,255U}){
            auto ranged=bundle;auto& value=ranged.runtime_entities.front();
            auto& parent_job=value.route_stack->jobs.front();
            const odh::Instruction request{27,254,255,
                {5,129,0,static_cast<std::uint8_t>(direction),45,255,static_cast<std::uint8_t>(flags),173}};
            parent_job.pending.instruction.instruction=request;parent_job.replay_instruction=request;
            parent_job.face_anywhere=direction==255;parent_job.facing=0;
            parent_job.call_failure_trees=(flags&2U)==0;
            value.vm.suspended_pending.front()=parent_job.pending;
            const auto encoded=odh::encode_session_bundle(ranged);
            const auto opened=odh::decode_session_bundle(encoded,ranged.resources);
            const auto& restored=opened.runtime_entities.front().route_stack->jobs.front();
            require(restored.deferred_room_route&&restored.external_frame_active&&restored.path.empty()&&
                !restored.source_circle&&!restored.source_range&&!restored.source_sparse&&
                restored.replay_instruction->operands==request.operands&&
                restored.face_anywhere==(direction==255)&&restored.call_failure_trees==((flags&2U)==0)&&
                odh::encode_session_bundle(opened)==encoded,
                "transient ranged relative request preserves original operands and portal child without a selected goal");
            auto malformed=ranged;
            malformed.runtime_entities.front().route_stack->jobs.front().face_anywhere=direction!=255;
            rejects([&]{(void)odh::encode_session_bundle(malformed);},"deferred ranged relative keeps its source facing policy");
            malformed=ranged;
            malformed.runtime_entities.front().route_stack->jobs.front().call_failure_trees=(flags&2U)!=0;
            rejects([&]{(void)odh::encode_session_bundle(malformed);},"deferred ranged relative keeps NoFailureTrees independently of geometry");
            malformed=ranged;
            malformed.runtime_entities.front().route_stack->jobs.front().arrival_radian_direction=0;
            rejects([&]{(void)odh::encode_session_bundle(malformed);},"deferred ranged relative cannot invent a selected arrival goal");
            malformed=ranged;
            auto& bad_job=malformed.runtime_entities.front().route_stack->jobs.front();
            bad_job.pending.instruction.instruction.operands[2]=8;
            bad_job.replay_instruction=bad_job.pending.instruction.instruction;
            malformed.runtime_entities.front().vm.suspended_pending.front()=bad_job.pending;
            rejects([&]{(void)odh::encode_session_bundle(malformed);},"deferred relative does not admit unknown source locations");
            malformed=ranged;
            malformed.runtime_entities.front().route_stack->portal_contexts.clear();
            rejects([&]{(void)odh::encode_session_bundle(malformed);},"deferred ranged parent still requires authenticated portal host context");
        }
        auto nested_range=bundle;auto& value=nested_range.runtime_entities.front();
        auto& child_job=value.route_stack->jobs.back();
        child_job.pending.instruction.instruction={27,254,255,{5,0,255,254,0,0,0,0}};
        child_job.replay_frame=child_job.route_frame;child_job.replay_instruction=child_job.pending.instruction.instruction;
        child_job.path.clear();child_job.next=1;child_job.entry_flags=0;child_job.facing=0;child_job.face_anywhere=false;
        child_job.walking_started=false;child_job.same_room_verified=false;child_job.call_failure_trees=false;
        child_job.frame.state=odh::RouteFrameState::room_portal;child_job.deferred_room_route=true;child_job.room_context_open=true;
        value.vm.pending=child_job.pending;
        odh::RoutePortalContextSnapshot child_context;child_context.ticket=child_job.ticket;
        value.route_stack->portal_contexts.push_back(child_context);
        const auto encoded=odh::encode_session_bundle(nested_range);
        require(odh::encode_session_bundle(odh::decode_session_bundle(encoded,nested_range.resources))==encoded,
            "nested transient relative request inherits the parent failure tree even when operand allows failure trees");
        child_job.call_failure_trees=true;
        rejects([&]{(void)odh::encode_session_bundle(nested_range);},"nested transient request cannot create a second failure tree");
    }
    {
        auto parked=bundle;auto& parent_job=parked.runtime_entities.front().route_stack->jobs.front();
        static_cast<odh::LocalRouteSnapshot&>(parent_job)=*legacy.runtime_entities.front().route;
        parent_job.frame.state=odh::RouteFrameState::room_portal;parent_job.deferred_room_route=false;
        parent_job.room_context_open=false;parent_job.replay_frame.reset();parent_job.replay_instruction.reset();
        parked.runtime_entities.front().route_stack->portal_contexts.clear();
        const auto encoded=odh::encode_session_bundle(parked);const auto opened=odh::decode_session_bundle(encoded);
        require(opened.runtime_entities.front().route_stack->jobs.front().frame.state==odh::RouteFrameState::room_portal&&
            !opened.runtime_entities.front().route_stack->jobs.front().path.empty()&&odh::encode_session_bundle(opened)==encoded,
            "nondeferred local parent retains frozen geometry while its verified external frame parks routing");
    }
    reject([](auto& value){value.runtime_entities.front().vm.suspended_pending.clear();},"nested parent requires exact suspended pending");
    reject([](auto& value){value.runtime_entities.front().vm.stack.back().invocation_ticket=2;},"nested child invocation belongs to its parent ticket");
    reject([](auto& value){value.runtime_entities.front().route_stack->portal_contexts.clear();},"open route context requires host context");
    reject([](auto& value){value.runtime_entities.front().route_stack->portal_contexts.front().portal_generation++;},"pending host portal requires live generation");
    reject([](auto& value){value.runtime_entities.front().route_stack->jobs.front().target_generation++;},"suspended target identity must remain live");
    reject([](auto& value){value.runtime_entities.front().route_stack->jobs.front().portal_steps=33;},"nested portal traversal remains bounded");
    require(odh::encode_session_bundle(legacy)==legacy_bytes,"RSTK support leaves legacy ROUT archive bytes unchanged");
}

void set_source_range_route(odh::SessionRuntimeEntity& runtime,bool require_world_position=false,bool face_anywhere=false) {
    auto& route=*runtime.route;
    odh::SlotRecord slot{};slot.type=3;slot.offset_bits={0,0,0};slot.standing=1;
    slot.flags=0x8055;slot.snap_target_slot=-1;slot.proximity=std::array<std::int32_t,3>{16,80,16};
    slot.max_size=0;slot.unknown_i10=7;slot.gradient_bits=0x3E400000;
    slot.height=0;slot.facing=-2;slot.resolution=16;
    if(face_anywhere){slot.flags=0xA0FF;slot.proximity=std::array<std::int32_t,3>{16,64,16};
        slot.unknown_i10=0;slot.gradient_bits=0;slot.facing=-3;slot.resolution=8;}
    odh::SlotTable table{};table.version=10;table.records.push_back(slot);
    const float target_angle=face_anywhere?0.0f:0.31f;
    route.target_position.direction=0;
    const auto goals=odh::enumerate_source_slot_range_candidates(table,0,route.target_position,target_angle);
    const auto start=odh::RoutePoint{route.expected.x,route.expected.y};
    const auto selected=std::find_if(goals.begin(),goals.end(),[&](const auto& goal){
        return !(goal.position==start)&&goal.entry_flags!=0&&
            (!require_world_position||(goal.position.x>=0&&goal.position.y>=0));
    });
    require(selected!=goals.end()&&goals.size()>10,"source range fixture has a bounded multi-radius candidate set");
    route.source_circle.reset();route.look_towards.reset();route.target_anchor_radian_direction.reset();
    route.path={start,selected->position};route.next=1;route.entry_flags=selected->entry_flags;
    route.facing=selected->facing;route.face_anywhere=face_anywhere;
    route.arrival_radian_direction=selected->radian_direction;route.call_failure_trees=true;
    odh::SourceSlotRangeRouteSnapshot range;range.slot_version=10;range.slot_index=face_anywhere?8:3;range.slot=slot;
    range.target_radian_direction=target_angle;range.selected=*selected;
    for(auto it=goals.rbegin();it!=goals.rend()&&range.fallback_goals.size()<10;++it)
        if(!(it->position==selected->position))range.fallback_goals.push_back(*it);
    route.source_range=range;
    runtime.vm.pending->instruction.instruction={45,254,255,{static_cast<std::uint8_t>(face_anywhere?8:3),0,2,0,0,0,0,0}};
}

void set_transient_range_route(odh::SessionRuntimeEntity& runtime,const odh::Instruction& instruction,bool nested=false){
    const auto table=odh::make_source_goto_relative_slot(instruction);
    require(table&&table->records.size()==1&&odh::is_source_slot_range(table->records.front()),
        "transient selected route reconstructs a canonical source range");
    auto& route=*runtime.route;const auto start=odh::RoutePoint{route.expected.x,route.expected.y};
    const auto goals=odh::enumerate_source_slot_range_candidates(*table,0,route.target_position,0);
    const auto selected=std::find_if(goals.begin(),goals.end(),[&](const auto& goal){
        return goal.entry_flags&&goal.position.x>=0&&goal.position.y>=0&&!(goal.position==start);});
    require(selected!=goals.end(),"transient relative source yields a real selected candidate");
    route.path={start,selected->position};route.next=1;route.entry_flags=selected->entry_flags;
    route.facing=selected->facing;route.face_anywhere=instruction.operands[3]==255;
    route.arrival_radian_direction=selected->radian_direction;route.call_failure_trees=!nested&&(instruction.operands[6]&2U)==0;
    route.source_circle.reset();route.look_towards.reset();route.source_sparse.reset();
    odh::SourceSlotRangeRouteSnapshot range;range.slot_version=table->version;range.slot_index=0;range.slot=table->records.front();
    range.selected=*selected;range.target_radian_direction=0;
    for(const auto& goal:goals)if(goal.entry_flags&&!(goal.position==selected->position)){
        range.fallback_goals.push_back(goal);break;}
    route.source_range=std::move(range);runtime.vm.pending->instruction.instruction=instruction;
}
void transient_selected_range_archive_tests(){
    for(const auto location:{std::uint8_t{0},std::uint8_t{7},std::uint8_t{255}})
        for(const auto facing:{std::uint8_t{254},std::uint8_t{255},std::uint8_t{3}})
            for(const auto flags:{std::uint8_t{0},std::uint8_t{2},std::uint8_t{6},std::uint8_t{0xfd},std::uint8_t{0xff}}){
        auto bundle=local_route_fixture();auto& runtime=bundle.runtime_entities.front();
        bundle.world.entities[1].position=odh::EntityPosition{64,64,1,0};
        runtime.route->target_position=*bundle.world.entities[1].position;
        const odh::Instruction raw{27,254,255,{5,129,location,facing,77,255,flags,173}};
        set_transient_range_route(runtime,raw);
        const auto bytes=odh::encode_session_bundle(bundle);const auto opened=odh::decode_session_bundle(bytes,bundle.resources);
        const auto& route=*opened.runtime_entities.front().route;
        require(route.source_range&&route.source_range->slot_index==0&&
            opened.runtime_entities.front().vm.pending->instruction.instruction.operands==raw.operands&&
            route.call_failure_trees==((flags&2U)==0)&&odh::encode_session_bundle(opened)==bytes,
            "selected transient27 preserves canonical SLOT, raw counters, continuous arrival and failure policy");
        auto self=bundle;auto& self_runtime=self.runtime_entities.front();auto& self_route=*self_runtime.route;
        self_route.target=self_route.actor;self_route.target_generation=self_route.actor_generation;
        self_route.target_anchor_radian_direction=self_route.source_range->target_radian_direction;
        self_runtime.vm.pending->context.stack_object=self_route.actor;
        self_runtime.vm.stack.back().context.stack_object=self_route.actor;
        const auto self_bytes=odh::encode_session_bundle(self);
        require(odh::encode_session_bundle(odh::decode_session_bundle(self_bytes,self.resources))==self_bytes,
            "raw27 self-target uses its exact frozen source anchor without substituting the actor's live position");
        self_runtime.route->target_anchor_radian_direction.reset();
        rejects([&]{(void)odh::encode_session_bundle(self);},"raw27 self-target requires its frozen angular anchor");
        const auto reject=[&](const auto& change,const char* text){auto malformed=bundle;change(malformed.runtime_entities.front());
            rejects([&]{(void)odh::encode_session_bundle(malformed);},text);};
        reject([](auto& value){value.route->source_range->slot_index=5;},"transient SLOT index is zero rather than OldTrapCount");
        reject([](auto& value){value.route->source_range->slot.max_size=101;},"transient manifest authenticates ignored but exact generated metadata");
        reject([](auto& value){value.route->source_range->slot.proximity->at(2)++;},"transient manifest cannot alter source OptimalProximity");
        reject([](auto& value){value.vm.pending->instruction.instruction.operands[2]=8;},"selected transient route refuses unknown source location");
        reject([](auto& value){value.vm.pending->instruction.instruction.operands[6]^=2;},"selected transient route keeps original NoFailureTrees policy");
        reject([](auto& value){value.vm.pending->instruction.instruction.operands[3]=8;},"selected transient route rejects an undefined source direction");
        reject([](auto& value){value.vm.pending->context.code_owner=99;},"selected transient route cannot replace its authenticated VM owner");
        reject([](auto& value){value.route->source_range->selected.radian_direction+=0.01f;},"selected transient route cannot alter continuous arrival after scoring");
        auto counters=bundle;counters.runtime_entities.front().vm.pending->instruction.instruction.operands[0]=250;
        counters.runtime_entities.front().vm.pending->instruction.instruction.operands[4]=100;
        const auto ignored_bytes=odh::encode_session_bundle(counters);
        require(odh::encode_session_bundle(odh::decode_session_bundle(ignored_bytes,counters.resources))==ignored_bytes,
            "source ignored counters remain raw data without becoming SLOT selectors");
    }
    auto nested=local_route_fixture();auto& runtime=nested.runtime_entities.front();
    nested.world.entities[1].position=odh::EntityPosition{64,64,1,0};
    runtime.route->target_position=*nested.world.entities[1].position;
    odh::RouteJobSnapshot parent;static_cast<odh::LocalRouteSnapshot&>(parent)=*runtime.route;
    parent.pending=*runtime.vm.pending;parent.route_frame=runtime.vm.stack.back();parent.path.clear();parent.next=1;
    parent.entry_flags=0;parent.facing=0;parent.same_room_verified=false;parent.call_failure_trees=true;
    parent.frame.state=odh::RouteFrameState::room_portal;parent.deferred_room_route=true;parent.room_context_open=true;
    parent.external_frame_active=true;parent.external_frame_owner=1;
    parent.external_frame_call=odh::VmCall{odh::routine_key(4097),{1,2,2,1},{},odh::VmCallReturnMode::ordinary};
    parent.replay_frame=parent.route_frame;parent.replay_instruction=parent.pending.instruction.instruction;parent.portal_steps=1;
    set_transient_range_route(runtime,{27,254,255,{5,0,0,254,0,0,0,0}},true);
    odh::RouteJobSnapshot child;static_cast<odh::LocalRouteSnapshot&>(child)=*runtime.route;
    child.pending=*runtime.vm.pending;child.ticket=2;child.pending.ticket=2;
    child.pending.instruction.key=odh::routine_key(4097);child.pending.instruction.depth=2;
    child.route_frame=runtime.vm.stack.back();child.route_frame.key=odh::routine_key(4097);
    child.route_frame.return_mode=odh::VmCallReturnMode::external_frame_next_tick;child.route_frame.invocation_ticket=1;
    runtime.vm.stack.push_back(child.route_frame);runtime.vm.pending=child.pending;
    runtime.vm.suspended_pending={parent.pending};runtime.vm.ticket_sequence=2;runtime.route.reset();
    odh::RoutePortalContextSnapshot context;context.ticket=1;context.pending_call=parent.external_frame_call;
    context.pending_edge=odh::RoomPortalEdge{1,2,2,child.target_position};context.portal_generation=child.target_generation;
    runtime.route_stack=odh::RouteStackSnapshot{{parent,child},nested.host.tick,{context}};
    const auto bytes=odh::encode_session_bundle(nested);
    require(odh::encode_session_bundle(odh::decode_session_bundle(bytes,nested.resources))==bytes,
        "selected transient27 child and suspended portal parent round-trip with exact owner/ticket/context links");
    auto missing=nested;missing.runtime_entities.front().vm.suspended_pending.clear();
    rejects([&]{(void)odh::encode_session_bundle(missing);},"selected transient child cannot detach from its suspended parent");
    missing=nested;missing.runtime_entities.front().route_stack->jobs.back().call_failure_trees=true;
    rejects([&]{(void)odh::encode_session_bundle(missing);},"selected transient child cannot duplicate its parent failure tree");
}
void source_slot_chair_choices_archive_tests(){
    const auto legacy=local_route_fixture();const auto legacy_bytes=odh::encode_session_bundle(legacy);
    auto bundle=legacy;odh::World world;require(world.restore_snapshot(bundle.world)&&world.set_static_footprint(1,true)&&
        world.create(3,odh::EntityKind::object)&&world.define_type(3,0x1234)&&world.set_position(3,{28,12,1,2}),"typed chair source World");
    std::vector<odh::EntryPoint> entries(27);entries[26].action=4100;require(world.define_entry_points(3,entries),"actual chair EP26 table");
    bundle.world=world.snapshot();auto& runtime=bundle.runtime_entities.front();auto& route=*runtime.route;
    route.path={{route.expected.x,route.expected.y}};route.next=1;route.frame.state=odh::RouteFrameState::initial;route.walking_started=false;
    route.entry_flags=0;route.facing=2;route.face_anywhere=false;route.call_failure_trees=true;route.arrival_radian_direction=1.25f;
    odh::SourceSlotChoicesSnapshot choices;choices.slot_version=10;choices.slot_index=1;choices.slot.type=3;
    choices.slot.standing=1;choices.slot.sitting=1;choices.slot.facing=-2;choices.slot.snap_target_slot=-1;choices.target_radian_direction=0;
    choices.selected={{28,12},0,2,1.25f,false,odh::LiveAttemptWalkIdentity{3,world.find(3)->generation}};
    choices.remaining={{{28,12},4,3,0.75f,false,odh::LiveAttemptWalkIdentity{3,world.find(3)->generation}},{{40,12},4,1,0.5f,false,std::nullopt}};
    choices.selected.level=1;for(auto& goal:choices.remaining)goal.level=1;
    route.source_choices=choices;
    const auto bytes=odh::encode_session_bundle(bundle);const auto opened=odh::decode_session_bundle(bytes);
    const auto& saved=*opened.runtime_entities.front().route->source_choices;
    require(saved.selected.chair&&saved.selected.chair->generation==world.find(3)->generation&&!saved.chair_attempted&&
        saved.remaining.size()==2&&saved.remaining[0].position==saved.selected.position&&saved.remaining[0].facing==3&&
        saved.remaining[1].position.x==40&&odh::encode_session_bundle(opened)==bytes,"chair selected before AttemptWalk preserves duplicate-position ranked alternatives and exact order");
    for(const auto version:{4U,5U,6U,7U,8U,9U,10U}){auto old=bundle;old.runtime_entities.front().route->source_choices->slot_version=version;
        const auto encoded=odh::encode_session_bundle(old);require(odh::encode_session_bundle(odh::decode_session_bundle(encoded))==encoded,"all source SLOT layouts keep typed choices");}
    auto second_floor=bundle;for(auto& entity:second_floor.world.entities)if(entity.position)entity.position->level=2;
    auto& upper=*second_floor.runtime_entities.front().route;upper.expected.level=2;upper.target_position.level=2;
    upper.source_choices->selected.level=2;for(auto& goal:upper.source_choices->remaining)goal.level=2;
    const auto upper_bytes=odh::encode_session_bundle(second_floor);const auto upper_opened=odh::decode_session_bundle(upper_bytes);
    require(upper_opened.runtime_entities.front().route->source_choices->selected.level==2&&
        upper_opened.runtime_entities.front().route->source_choices->remaining[1].level==2&&
        odh::encode_session_bundle(upper_opened)==upper_bytes,"typed choices preserve actual second-floor level rather than assuming ground floor");
    const auto reject=[&](auto mutate,const char* why){auto bad=bundle;mutate(*bad.runtime_entities.front().route);rejects([&]{(void)odh::encode_session_bundle(bad);},why);};
    reject([](auto& r){r.path.push_back(r.source_choices->selected.position);},"chair cannot pretend it walked to seat before EP26");
    reject([](auto& r){r.source_choices->selected.chair->generation++;},"chair generation cannot be rebound on reopen");
    reject([](auto& r){r.source_choices->remaining[0].chair->generation++;},"remaining chair generation remains authenticated");
    reject([](auto& r){r.source_choices->slot_index++;},"typed choices index must match authored Route45");
    reject([](auto& r){r.source_choices->selected.level=0;},"typed choice cannot invent a zero floor");
    reject([](auto& r){r.source_choices->remaining[0].level=128;},"typed remaining choice level must fit source signed byte");
    reject([](auto& r){r.source_choices->remaining[0].level=2;},"native same-floor profile preserves foreign floor as unsupported, not flattened");
    reject([](auto& r){r.source_choices->chair_attempted=true;r.frame.state=odh::RouteFrameState::room_portal;},"chair attempt cannot survive without real external call");
    reject([](auto& r){r.source_choices->remaining.resize(4096);},"remaining choices bounded before allocation");
    reject([](auto& r){r.source_choices->target_radian_direction=std::numeric_limits<float>::quiet_NaN();},"typed SLOT frozen transform must be finite");
    // A genuine selected NoPath has no successful same-room/motion proof.
    // Only the full stack's authenticated pre398 failure authority admits it.
    for(const bool relative:{false,true}){
        auto pending_failure=bundle;auto& pending_actor=pending_failure.runtime_entities.front();
        auto& pending_route=*pending_actor.route;
        pending_route.same_room_verified=false;pending_route.source_choices->selected.chair.reset();
        pending_route.source_choices->remaining.clear();
        if(relative){
            const odh::Instruction instruction{27,254,255,{0,0,0,254,0,0,0,0}};
            pending_actor.vm.pending->instruction.instruction=instruction;
            const auto transient=odh::make_source_goto_relative_slot(instruction);
            require(transient&&transient->records.size()==1,"exact source27 canonical temporary SLOT for failure archive");
            pending_route.source_choices->slot_version=transient->version;
            pending_route.source_choices->slot_index=0;pending_route.source_choices->slot=transient->records.front();
            pending_route.facing=4;pending_route.arrival_radian_direction=3.14159274f;
            pending_route.source_choices->selected.facing=4;pending_route.source_choices->selected.radian_direction=3.14159274f;
        }
        odh::RouteJobSnapshot failed_choice;static_cast<odh::LocalRouteSnapshot&>(failed_choice)=pending_route;
        failed_choice.pending=*pending_actor.vm.pending;failed_choice.route_frame=pending_actor.vm.stack.back();
        failed_choice.failure_pending=true;failed_choice.failure_code=3;failed_choice.failure_reason="source AttemptWalk found no reachable goal";
        pending_actor.route.reset();pending_actor.route_stack=odh::RouteStackSnapshot{{failed_choice},pending_failure.host.tick,{}};
        const auto pending_bytes=odh::encode_session_bundle(pending_failure);
        const auto pending_opened=odh::decode_session_bundle(pending_bytes);
        const auto& restored=pending_opened.runtime_entities.front().route_stack->jobs.front();
        require(restored.failure_pending&&!restored.same_room_verified&&!restored.source_sparse&&
            restored.path==std::vector<odh::RoutePoint>{{5,12}}&&restored.source_choices&&
            odh::encode_session_bundle(pending_opened)==pending_bytes,
            "full selected INITIAL NoPath roundtrips without fabricated successful room/motion proof");
        auto forged=pending_failure;forged.runtime_entities.front().route_stack->jobs.front().failure_pending=false;
        rejects([&]{(void)odh::encode_session_bundle(forged);},"failed room-proof exception cannot be borrowed without pending source failure authority");
        forged=pending_failure;forged.runtime_entities.front().route_stack->jobs.front().failure_code=0;
        rejects([&]{(void)odh::encode_session_bundle(forged);},"failed room-proof exception requires positive source failure code");
        forged=pending_failure;forged.runtime_entities.front().route_stack->jobs.front().pending.ticket++;
        rejects([&]{(void)odh::encode_session_bundle(forged);},"failed room-proof exception never relaxes exact pending ticket");
        forged=pending_failure;forged.runtime_entities.front().route_stack->jobs.front().source_choices.reset();
        rejects([&]{(void)odh::encode_session_bundle(forged);},"old failed archive without selected manifest does not obtain room-proof exception");
        forged=pending_failure;forged.runtime_entities.front().route_stack->jobs.front().target_position.level=2;
        rejects([&]{(void)odh::encode_session_bundle(forged);},"selected failed same-floor profile does not invent cross-floor placement support");
    }
    auto child=bundle;auto& state=child.runtime_entities.front();auto& vm=state.vm;
    odh::RouteJobSnapshot job;static_cast<odh::LocalRouteSnapshot&>(job)=*state.route;
    job.pending=*vm.pending;job.route_frame=vm.stack.back();job.selected_sitting_object=3;
    for(const auto& goal:choices.remaining)job.fallback_goals.push_back({goal.position,goal.entry_flags,goal.facing,
        goal.chair?std::optional<std::int16_t>{goal.chair->id}:std::nullopt,std::nullopt,goal.radian_direction});
    job.external_frame_active=true;job.external_frame_sitting=true;job.external_frame_owner=1;
    job.frame.state=odh::RouteFrameState::room_portal;job.source_choices->chair_attempted=true;
    job.external_frame_call=odh::VmCall{odh::routine_key(4100),{1,3,3,1},{0,0,0,0,0,0},odh::VmCallReturnMode::ordinary,true};
    vm.suspended_pending={job.pending};vm.pending.reset();vm.status=odh::VmStatus::running;
    const auto& call=*job.external_frame_call;vm.stack.push_back({call.key,0,call.context,call.arguments,{},odh::VmCallReturnMode::external_frame_next_tick,job.ticket,true});
    state.route.reset();state.route_stack=odh::RouteStackSnapshot{{job},child.host.tick,{}};child.scheduler.entities.front().status=vm.status;
    const auto active_bytes=odh::encode_session_bundle(child);const auto active=odh::decode_session_bundle(active_bytes);
    require(active.runtime_entities.front().route_stack->jobs.front().source_choices->chair_attempted&&odh::encode_session_bundle(active)==active_bytes,"actual sitting external frame freezes source choice before any walking");
    auto turned=child;auto& chair_parent=turned.runtime_entities.front().route_stack->jobs.front().frame;
    odh::RouteFrameContext nested_turn;
    require(odh::consume_route_portal_turn(nested_turn,&chair_parent)&&chair_parent.portal_turns==1&&
        !odh::consume_route_portal_turn(nested_turn,&chair_parent)&&chair_parent.portal_turns==2,
        "source nested routes consume the EP26 ROOM_PORTAL parent turn counter");
    const auto turned_bytes=odh::encode_session_bundle(turned);
    const auto turned_opened=odh::decode_session_bundle(turned_bytes);
    require(turned_opened.runtime_entities.front().route_stack->jobs.front().frame.portal_turns==2&&
        odh::encode_session_bundle(turned_opened)==turned_bytes,
        "authenticated external EP26 preserves parent portal-turn counters without fabricated sparse motion");
    auto invalid_turn=turned;invalid_turn.runtime_entities.front().route_stack->jobs.front().frame.wait_time=1;
    rejects([&]{(void)odh::encode_session_bundle(invalid_turn);},"chair parent turn proof cannot admit an invented walking wait");
    invalid_turn=turned;invalid_turn.runtime_entities.front().route_stack->jobs.front().external_frame_call.reset();
    rejects([&]{(void)odh::encode_session_bundle(invalid_turn);},"chair parent turns require the genuine external call");
    auto stand_before=bundle;odh::World stand_world;
    require(stand_world.restore_snapshot(stand_before.world)&&stand_world.create(4,odh::EntityKind::object,{0,0,1})&&
        stand_world.define_type(4,0x1235)&&stand_world.set_position(4,stand_before.runtime_entities.front().route->expected),
        "stand phase current physical container");
    auto stand_entries=*stand_world.find(3)->entry_points;stand_entries.resize(28);stand_entries[27].action=4101;
    require(stand_world.define_entry_points(4,stand_entries)&&stand_world.move_to_slot(4,0,1)&&stand_world.set_person_data(1,0,1),
        "source positioned seated actor before standing");
    stand_before.world=stand_world.snapshot();stand_before.runtime_entities.front().route->source_choices->selected.chair.reset();
    const auto stand_before_bytes=odh::encode_session_bundle(stand_before);
    require(odh::encode_session_bundle(odh::decode_session_bundle(stand_before_bytes))==stand_before_bytes,
        "unstarted source standing phase preserves frozen goal and physical chair without fake walking");
    auto standing=child;standing.world=stand_before.world;
    auto& stand_actor=standing.runtime_entities.front();auto& stand_job=stand_actor.route_stack->jobs.front();
    stand_job.source_choices->selected.chair.reset();stand_job.source_choices->chair_attempted=false;stand_job.selected_sitting_object.reset();
    stand_job.external_frame_sitting=false;stand_job.frame.state=odh::RouteFrameState::stand_func;
    stand_job.external_frame_purpose=odh::RouteExternalFramePurpose::standing;
    stand_job.external_frame_callee_generation=stand_world.find(4)->generation;
    stand_job.external_frame_call->key=odh::routine_key(4101);stand_job.external_frame_call->context.callee=4;stand_job.external_frame_call->context.stack_object=4;
    auto& stand_frame=stand_actor.vm.stack.back();stand_frame.key=stand_job.external_frame_call->key;stand_frame.context=stand_job.external_frame_call->context;
    const auto standing_bytes=odh::encode_session_bundle(standing);const auto standing_opened=odh::decode_session_bundle(standing_bytes);
    require(standing_opened.runtime_entities.front().route_stack->jobs.front().external_frame_purpose==odh::RouteExternalFramePurpose::standing&&
        odh::encode_session_bundle(standing_opened)==standing_bytes,"active genuine EP27 call preserves stand purpose generation and ABI in existing optional stack extension");
    auto malformed_stand=standing;++malformed_stand.runtime_entities.front().route_stack->jobs.front().external_frame_callee_generation;
    rejects([&]{(void)odh::encode_session_bundle(malformed_stand);},"stand cannot borrow a recycled container generation");
    malformed_stand=standing;malformed_stand.runtime_entities.front().route_stack->jobs.front().external_frame_call->arguments[0]=1;
    rejects([&]{(void)odh::encode_session_bundle(malformed_stand);},"stand requires exact metadata zeros at EP27 entry");
    malformed_stand=standing;malformed_stand.runtime_entities.front().route_stack->jobs.front().external_frame_call.reset();
    rejects([&]{(void)odh::encode_session_bundle(malformed_stand);},"stand cannot retain phase without real external call");
    for(const bool result:{false,true}){auto stand_returned=standing;auto& resident=stand_returned.runtime_entities.front();
        resident.vm.stack.pop_back();resident.vm.suspended_pending.clear();resident.vm.pending=stand_job.pending;
        resident.vm.status=odh::VmStatus::yielded;resident.vm.external_frame_return=odh::VmExternalFrameReturn{stand_job.ticket,result,stand_job.external_frame_owner,stand_job.run_generation};
        stand_returned.scheduler.entities.front().status=resident.vm.status;
        const auto returned_stand_bytes=odh::encode_session_bundle(stand_returned);
        require(odh::encode_session_bundle(odh::decode_session_bundle(returned_stand_bytes))==returned_stand_bytes,
            "stand true/false external return preserves source result until authenticated routing acknowledgement");}
    auto sleeping_stand=standing;auto& sleeper=sleeping_stand.runtime_entities.front();
    sleeper.vm.status=odh::VmStatus::waiting;sleeper.vm.ticket_sequence=2;sleeper.vm.stack.back().arguments[0]=4;
    sleeper.vm.pending=odh::VmPending{2,odh::VmTrace{0,sleeper.vm.stack.size(),sleeper.vm.stack.back().key,0,{0,254,255,{}},1},sleeper.vm.stack.back().context};
    sleeper.controllers.sleep.actor=odh::SleepActorSnapshot{1,stand_world.find(1)->generation,sleeper.vm.run_generation};
    sleeper.controllers.sleep.job=odh::SleepJobSnapshot{2,0,5,0};sleeping_stand.scheduler.entities.front().status=sleeper.vm.status;
    const auto sleeping_stand_bytes=odh::encode_session_bundle(sleeping_stand);
    require(odh::encode_session_bundle(odh::decode_session_bundle(sleeping_stand_bytes))==sleeping_stand_bytes,
        "actual child Sleep controller is not misclassified as overlapping its suspended stand parent");
    auto wrong=child;wrong.runtime_entities.front().route_stack->jobs.front().fallback_goals[0].facing++;
    rejects([&]{(void)odh::encode_session_bundle(wrong);},"legacy fallback must retain exact typed order/facing");
    wrong=child;wrong.runtime_entities.front().route_stack->jobs.front().external_frame_call->arguments[5]=-1;
    rejects([&]{(void)odh::encode_session_bundle(wrong);},"EP26 uses metadata-count zeros, not default-four/-1 arguments");
    auto returned=child;auto& actor=returned.runtime_entities.front();actor.vm.stack.pop_back();actor.vm.suspended_pending.clear();actor.vm.pending=job.pending;
    actor.vm.status=odh::VmStatus::yielded;actor.vm.external_frame_return=odh::VmExternalFrameReturn{job.ticket,false,1,job.run_generation};returned.scheduler.entities.front().status=actor.vm.status;
    const auto returned_bytes=odh::encode_session_bundle(returned);require(odh::encode_session_bundle(odh::decode_session_bundle(returned_bytes))==returned_bytes,"false sitting return preserves remaining choice order until source fallback consumes it");
    auto failure=child;auto& failed=failure.runtime_entities.front();auto& failed_job=failed.route_stack->jobs.front();
    failed_job.external_frame_sitting=false;failed_job.source_choices->chair_attempted=false;failed_job.failure_tree_invoked=true;
    failed_job.failure_code=3;failed_job.failure_blocker=0;
    failed_job.external_frame_call=odh::VmCall{odh::routine_key(398),failed_job.pending.context,{3,0,0,0}};
    const auto& failure_call=*failed_job.external_frame_call;
    failed.vm.stack.back()={failure_call.key,0,failure_call.context,failure_call.arguments,{},odh::VmCallReturnMode::external_frame_next_tick,failed_job.ticket};
    const auto failure_bytes=odh::encode_session_bundle(failure);
    require(odh::encode_session_bundle(odh::decode_session_bundle(failure_bytes))==failure_bytes,"exhausted chair CurRoute survives exact Global398 child without inventing a walk");
    auto standing_failure=failure;auto& standing_job=standing_failure.runtime_entities.front().route_stack->jobs.front();
    standing_job.selected_sitting_object.reset();standing_job.entry_flags=0;standing_job.source_choices->selected.chair.reset();
    standing_job.source_choices->slot.standing=0;standing_job.source_choices->slot.sitting=0;
    standing_job.source_choices->remaining.erase(standing_job.source_choices->remaining.begin());standing_job.fallback_goals.erase(standing_job.fallback_goals.begin());
    const auto standing_failure_bytes=odh::encode_session_bundle(standing_failure);
    require(odh::encode_session_bundle(odh::decode_session_bundle(standing_failure_bytes))==standing_failure_bytes,
        "exhausted general nonchair choice retains CurRoute during failure tree without fabricated motion");
    wrong=failure;wrong.runtime_entities.front().route_stack->jobs.front().failure_tree_invoked=false;
    rejects([&]{(void)odh::encode_session_bundle(wrong);},"no-motion failure phase needs exact failure-tree ownership");
    wrong=failure;wrong.runtime_entities.front().route_stack->jobs.front().external_frame_call->arguments[0]=4;
    rejects([&]{(void)odh::encode_session_bundle(wrong);},"failure callback arguments must preserve original route failure code");
    require(odh::encode_session_bundle(legacy)==legacy_bytes,"absence of typed choices retains exact old archive bytes");
}
void sparse_locomotion_archive_tests(){
    // Value-level archive fixture, with motion counters produced by the native
    // kernel. This does not claim a live VM action or commercial-resource run.
    const auto legacy=local_route_fixture();const auto legacy_bytes=odh::encode_session_bundle(legacy);
    auto bundle=legacy;auto& runtime=bundle.runtime_entities.front();auto& route=*runtime.route;
    runtime.vm.pending->instruction.instruction={27,254,255,{0,0,254,0,0,0,2,0}};
    route.entry_flags=0;route.facing=0;route.face_anywhere=false;route.call_failure_trees=false;
    constexpr auto hip=odh::walk_path_hip;
    const odh::WalkSegmentGeometry cubic{odh::WalkSegmentKind::cubic,{5*hip,12*hip},{8*hip,9*hip},
        {12*hip,15*hip},{16*hip,12*hip}};
    const odh::WalkSegmentGeometry line{odh::WalkSegmentKind::line,{16*hip,12*hip},{},{},{28*hip,12*hip}};
    odh::WalkMotion motion;const odh::WalkMotionProfile profile{false,0,false,0};
    require(motion.initialize({cubic,line},{{5,12},1,route.expected_radian_direction},profile,true,4)==
        odh::WalkMotionStatus::success,"initialize bounded cubic-plus-line archive motion");
    auto frame=route.frame;frame.state=odh::RouteFrameState::walking;
    const auto prepared=motion.prepare_step(frame);
    require(prepared.status==odh::WalkMotionStatus::success&&prepared.step,"prepare pure native motion archive step");
    const auto committed=motion.commit_step(*prepared.step);
    require(committed.status==odh::WalkMotionStatus::success&&committed.effects,
        "archive motion step ends at a capturable walking boundary");
    const auto captured=motion.snapshot();require(captured.snapshot.has_value(),"capture native movement counters without a placement lease");
    odh::World world;require(world.restore_snapshot(bundle.world)&&world.set_position(1,
        {prepared.step->data().sample.position.x,prepared.step->data().sample.position.y,1,route.expected.direction})&&
        world.set_radian_direction(1,committed.effects->radian_direction),"reflect pure motion sample in archive World expected pose");
    bundle.world=world.snapshot();route.expected=*world.find(1)->position;
    route.expected_radian_direction=world.find(1)->radian_direction;route.frame=frame;
    route.path={{route.expected.x,route.expected.y},{28,12}};route.next=1;
    odh::SourceSparseRouteSnapshot sparse;sparse.room=1;sparse.profile=profile;sparse.motion=*captured.snapshot;
    sparse.avatars_to_consider={{2,world.find(2)->generation}};sparse.target_radian_direction=0;
    sparse.face_anywhere=false;sparse.hard_start=false;sparse.locomotion_started=true;route.source_sparse=sparse;
    bundle.host.tick=7;bundle.scheduler.last_tick=7;
    auto& playback=runtime.playback;playback.last_tick=7;
    const auto lane=[&](std::uint16_t index,float position,float speed,std::uint32_t frames,std::size_t low,std::size_t high){
        odh::PlaybackClipSnapshot value;value.resource.kind=odh::PlaybackResourceKey::Kind::source_locomotion;
        value.resource.source=0;value.resource.id=index;value.resource.caller=1;value.resource.callee=2;
        value.resource.stack_object=2;value.resource.code_owner=1;value.actor=1;value.actor_generation=world.find(1)->generation;
        value.vm_generation=runtime.vm.run_generation;value.frame=position;value.speed=speed;value.frames=frames;
        value.loop=true;value.low=low;value.high=high;value.ticks=7;return value;
    };
    playback.state=lane(3,2,1,5,1,4);playback.state->queue={17,102};playback.state->events_run=2;
    playback.source_walk.emplace();playback.source_walk->secondary={lane(20,2.5f,1.5f,8,0,3),lane(25,4.5f,1.5f,6,2,5)};
    playback.source_walk->weights=prepared.step->data().animation_weights;
    playback.hands=odh::PlaybackHandsSnapshot{{},1,world.find(1)->generation,runtime.vm.run_generation};
    auto carry=playback.state->resource;carry.kind=odh::PlaybackResourceKey::Kind::animation;carry.source=1;carry.id=7;
    playback.carry=carry;
    {
        auto ordinary=bundle;auto& saved=*ordinary.runtime_entities.front().route;
        auto& instruction=ordinary.runtime_entities.front().vm.pending->instruction.instruction;
        instruction={27,254,255,{23,1,254,255,45,2,5,0}};saved.face_anywhere=true;saved.call_failure_trees=true;
        saved.source_sparse->face_anywhere=true;saved.source_sparse->target_radian_direction=0;
        require(odh::is_source_ordinary_go_here_instruction(instruction),"shared ordinary GoHere predicate admits ignored modified counters");
        const auto encoded=odh::encode_session_bundle(ordinary);const auto opened=odh::decode_session_bundle(encoded,ordinary.resources);
        require(opened.runtime_entities.front().route->source_sparse&&opened.runtime_entities.front().route->face_anywhere&&
            opened.runtime_entities.front().vm.pending->instruction.instruction.operands==instruction.operands&&odh::encode_session_bundle(opened)==encoded,
            "ordinary GoHere source motion with original counter values round-trips without a new wire shape");
        auto ghost=ordinary;auto& target=ghost.world.entities[1];ghost.world.word_count-=target.person_data.size()+target.motives.size();
        target.person_data.clear();target.motives.clear();target.kill_timeout.reset();target.kind=odh::EntityKind::object;target.ui_ghost=true;
        target.definition=odh::EntityType{0x7c4,std::nullopt};target.static_footprint=false;
        rejects([&]{(void)odh::encode_session_bundle(ghost);},"sparse flags5 cannot use historical UI ghost target");
        auto malformed=ordinary;malformed.runtime_entities.front().vm.pending->instruction.instruction.operands[6]=7;
        rejects([&]{(void)odh::encode_session_bundle(malformed);},"ordinary GoHere archive rejects changed NoFailureTrees policy");
        auto ignored=ordinary;ignored.runtime_entities.front().vm.pending->instruction.instruction.operands[6]=0xfd;
        ignored.runtime_entities.front().vm.pending->instruction.instruction.operands[7]=255;
        const auto ignored_bytes=odh::encode_session_bundle(ignored);
        require(odh::encode_session_bundle(odh::decode_session_bundle(ignored_bytes,ignored.resources))==ignored_bytes,
            "ordinary source sparse route preserves ignored flags and tail without granting a new route capability");
        auto ignored_ghost=ignored;auto& ignored_target=ignored_ghost.world.entities[1];
        ignored_ghost.world.word_count-=ignored_target.person_data.size()+ignored_target.motives.size();
        ignored_target.person_data.clear();ignored_target.motives.clear();ignored_target.kill_timeout.reset();
        ignored_target.kind=odh::EntityKind::object;ignored_target.ui_ghost=true;
        ignored_target.definition=odh::EntityType{0x7c4,std::nullopt};ignored_target.static_footprint=false;
        rejects([&]{(void)odh::encode_session_bundle(ignored_ghost);},
            "ignored flags cannot disguise the same historical UI ghost as an ordinary source sparse target");
        malformed=ordinary;malformed.runtime_entities.front().vm.pending->instruction.instruction.operands[2]=253;
        rejects([&]{(void)odh::encode_session_bundle(malformed);},"ordinary GoHere archive rejects changed source location");
    }
    {
        auto failure=bundle;auto& runtime=failure.runtime_entities.front();auto& vm=runtime.vm;
        runtime.route->call_failure_trees=true;vm.pending->instruction.instruction.operands[6]=0;
        odh::RouteJobSnapshot job;static_cast<odh::LocalRouteSnapshot&>(job)=*runtime.route;
        job.pending=*vm.pending;job.route_frame=vm.stack.back();job.frame.state=odh::RouteFrameState::room_portal;
        job.external_frame_active=true;job.failure_tree_invoked=true;job.failure_code=3;job.failure_blocker=0;
        job.external_frame_owner=job.pending.context.code_owner;
        job.external_frame_call=odh::VmCall{odh::routine_key(398),job.pending.context,{3,0,0,0}};
        job.source_sparse->motion.phase=odh::WalkMotionPhase::collision_required;
        odh::SourceRouteCollisionSnapshot policy;policy.phase=odh::SourceRouteCollisionPhase::soft_failure;policy.retries=0;
        for(auto member:job.source_sparse->avatars_to_consider)policy.considered.push_back({member.id,member.generation});
        job.source_sparse->collision=policy;job.frame.wait_time=0;
        vm.suspended_pending.push_back(*vm.pending);vm.pending.reset();vm.status=odh::VmStatus::running;
        vm.stack.push_back({odh::routine_key(398),0,job.pending.context,{3,0,0,0},{0,0,0,0},odh::VmCallReturnMode::external_frame_next_tick,job.ticket});
        runtime.route.reset();runtime.route_stack=odh::RouteStackSnapshot{{job}};
        failure.scheduler.entities.front().status=vm.status;
        const auto encoded=odh::encode_session_bundle(failure);const auto opened=odh::decode_session_bundle(encoded,failure.resources);
        require(odh::is_source_failure_tree_continuation(opened.runtime_entities.front().route_stack->jobs.front())&&odh::encode_session_bundle(opened)==encoded,
            "authenticated failure tree preserves rejected motion and soft-failure policy byte-identically");
        auto wrong=failure;wrong.runtime_entities.front().route_stack->jobs.front().failure_tree_invoked=false;
        rejects([&]{(void)odh::encode_session_bundle(wrong);},"nonfailure child cannot serialize a soft-failure rejected path");
        wrong=failure;wrong.runtime_entities.front().route_stack->jobs.front().external_frame_call->arguments[0]=8;
        rejects([&]{(void)odh::encode_session_bundle(wrong);},"failure snapshot cannot change exact failure arguments");
    }
    for(const auto flags:{0U,2U,5U,6U,0xfdU,0xffU}){
        auto deferred=local_route_fixture();auto& runtime=deferred.runtime_entities.front();auto& vm=runtime.vm;
        vm.pending->instruction.instruction={27,254,255,{23,129,254,255,45,255,static_cast<std::uint8_t>(flags),173}};
        odh::RouteJobSnapshot job;static_cast<odh::LocalRouteSnapshot&>(job)=*runtime.route;
        job.pending=*vm.pending;job.route_frame=vm.stack.back();job.path.clear();job.next=1;job.entry_flags=0;job.facing=0;
        job.face_anywhere=true;job.call_failure_trees=(flags&2U)==0;job.walking_started=false;job.same_room_verified=false;
        job.frame.state=odh::RouteFrameState::room_portal;job.deferred_room_route=true;job.room_context_open=true;
        job.replay_frame=job.route_frame;job.replay_instruction=job.pending.instruction.instruction;
        runtime.route.reset();odh::RoutePortalContextSnapshot context;context.ticket=job.ticket;
        runtime.route_stack=odh::RouteStackSnapshot{{job},deferred.host.tick,{context}};
        const auto encoded=odh::encode_session_bundle(deferred);const auto opened=odh::decode_session_bundle(encoded,deferred.resources);
        require(opened.runtime_entities.front().route_stack->jobs.front().face_anywhere&&odh::encode_session_bundle(opened)==encoded,
            "deferred exact/ordinary GoHere with ignored counters preserves source FaceAnywhere before sparse plan exists");
        auto moved=deferred;moved.world.entities[1].position->y++;
        const auto moved_bytes=odh::encode_session_bundle(moved);
        const auto moved_opened=odh::decode_session_bundle(moved_bytes,moved.resources);
        require(moved_opened.runtime_entities.front().route_stack->jobs.front().target_position==job.target_position&&
            odh::encode_session_bundle(moved_opened)==moved_bytes,
            "deferred source route preserves its request while its exact target moves before goal generation");
        auto replaced=moved;replaced.runtime_entities.front().route_stack->jobs.front().target_generation++;
        rejects([&]{(void)odh::encode_session_bundle(replaced);},
            "deferred source route still rejects replaced target identity");
        auto wrong=deferred;wrong.runtime_entities.front().route_stack->jobs.front().face_anywhere=false;
        rejects([&]{(void)odh::encode_session_bundle(wrong);},"deferred relative cannot forge source arrival policy");
        wrong=deferred;wrong.runtime_entities.front().route_stack->jobs.front().call_failure_trees=!job.call_failure_trees;
        rejects([&]{(void)odh::encode_session_bundle(wrong);},"deferred relative retains exact source NoFailureTrees flag");
    }
    {
        auto typed=bundle;auto& actor=typed.runtime_entities.front();auto& selected=*actor.route;
        actor.vm.pending->instruction.instruction={45,254,255,{1,0,2,0,0,0,0,0}};
        selected.call_failure_trees=true;selected.entry_flags=4;selected.arrival_radian_direction=0;
        odh::SourceSlotChoicesSnapshot choices;choices.slot_version=10;choices.slot_index=1;choices.slot.type=3;
        choices.slot.standing=1;choices.slot.snap_target_slot=-1;choices.slot.facing=-2;
        choices.selected={selected.path.back(),4,selected.facing,0,false,std::nullopt};
        choices.remaining={{{40,12},2,1,0.5f,false,std::nullopt},{{40,12},4,3,0.75f,false,std::nullopt}};
        choices.selected.level=selected.expected.level;for(auto& goal:choices.remaining)goal.level=selected.expected.level;
        selected.source_choices=choices;
        const auto encoded=odh::encode_session_bundle(typed);const auto opened=odh::decode_session_bundle(encoded);
        require(opened.runtime_entities.front().route->source_choices&&opened.runtime_entities.front().route->source_sparse&&
            opened.runtime_entities.front().route->source_choices->remaining[1].facing==3&&odh::encode_session_bundle(opened)==encoded,
            "general typed standing choices coexist with sparse motion and retain ranked equal-position goals");
        for(const auto weight:{0,-1}){auto zero=typed;auto& r=*zero.runtime_entities.front().route;
            r.entry_flags=0;r.source_choices->selected.entry_flags=0;r.source_choices->slot.standing=weight;r.source_choices->slot.ground=-7;
            const auto encoded_zero=odh::encode_session_bundle(zero);
            require(odh::encode_session_bundle(odh::decode_session_bundle(encoded_zero))==encoded_zero,
                "source general choices retain zero entry mask and defined zero/negative standing scores");}
        auto wrong=typed;wrong.runtime_entities.front().route->source_choices->selected.position.x++;
        rejects([&]{(void)odh::encode_session_bundle(wrong);},"typed standing selected goal must match actual motion path");
        wrong=typed;wrong.runtime_entities.front().route->source_choices->chair_attempted=true;
        rejects([&]{(void)odh::encode_session_bundle(wrong);},"standing fallback cannot retain chair attempt state");
        wrong=typed;wrong.runtime_entities.front().vm.pending->instruction.instruction={27,254,255,{1,0,0,254,2,0,0,0}};
        rejects([&]{(void)odh::encode_session_bundle(wrong);},"transient raw27 choices must carry exact canonical SLOT, not an archive row");
    }
    const auto bytes=odh::encode_session_bundle(bundle);const auto reopened=odh::decode_session_bundle(bytes,bundle.resources);
    const auto& saved=*reopened.runtime_entities.front().route->source_sparse;
    const auto& saved_playback=reopened.runtime_entities.front().playback;
    const auto same_geometry=[](const auto& a,const auto& b){return a.kind==b.kind&&a.a.x==b.a.x&&a.a.y==b.a.y&&
        a.b.x==b.b.x&&a.b.y==b.b.y&&a.c.x==b.c.x&&a.c.y==b.c.y&&a.d.x==b.d.x&&a.d.y==b.d.y;};
    require(saved.motion.current&&same_geometry(*saved.motion.current,cubic)&&saved.motion.remaining.size()==1&&
        same_geometry(saved.motion.remaining.front(),line)&&saved.motion.move_frames==sparse.motion.move_frames&&
        saved.motion.move_total_frames==sparse.motion.move_total_frames&&saved.motion.velocity==sparse.motion.velocity&&
        saved.motion.turn_frames==sparse.motion.turn_frames&&saved.motion.walk_direction==sparse.motion.walk_direction&&
        saved.motion.target_direction==sparse.motion.target_direction&&saved.motion.phase==sparse.motion.phase&&
        saved.motion.previous_position.position.x==sparse.motion.previous_position.position.x&&
        saved.motion.previous_position.position.y==sparse.motion.previous_position.position.y&&
        saved.motion.previous_position.level==sparse.motion.previous_position.level&&
        saved.motion.previous_position.radians==sparse.motion.previous_position.radians&&
        saved.profile.walk_style==profile.walk_style&&saved.profile.in_pool==profile.in_pool&&
        saved.profile.person_type==profile.person_type&&saved.profile.is_pet==profile.is_pet&&
        saved.room==1&&saved.avatars_to_consider.size()==1&&saved.avatars_to_consider.front().id==2&&
        saved.avatars_to_consider.front().generation==world.find(2)->generation&&
        saved.locomotion_started&&!saved.hard_start&&saved.lifecycle==0,
        "sparse archive preserves HiP geometry, all movement counters/directions, lifecycle and avatar identities");
    const auto same_clip=[](const auto& a,const auto& b){return a.resource==b.resource&&a.actor==b.actor&&
        a.actor_generation==b.actor_generation&&a.vm_generation==b.vm_generation&&a.frame==b.frame&&a.speed==b.speed&&
        a.frames==b.frames&&a.low==b.low&&a.high==b.high&&a.queue==b.queue&&a.events_run==b.events_run&&
        a.ticks==b.ticks&&a.end==b.end&&a.loop==b.loop&&a.backwards==b.backwards&&a.wait.has_value()==b.wait.has_value();};
    require(saved_playback.source_walk&&same_clip(*saved_playback.state,*playback.state)&&
        same_clip(saved_playback.source_walk->secondary[0],playback.source_walk->secondary[0])&&
        same_clip(saved_playback.source_walk->secondary[1],playback.source_walk->secondary[1])&&
        saved_playback.source_walk->weights==playback.source_walk->weights&&saved_playback.carry==playback.carry&&
        odh::encode_session_bundle(reopened)==bytes,
        "all three clip frames/speeds/event cursors/queues/weights and independent carry round-trip byte-identically");
    {
        auto shoo=bundle;odh::World shoo_world;
        require(shoo_world.restore_snapshot(shoo.world)&&shoo_world.create(3,odh::EntityKind::object)&&
            shoo_world.define_type(3,0x7c4)&&shoo_world.set_position(3,
                {route.expected.x,route.expected.y,route.expected.level,0}),"create exact fresh Destination archive participant");
        shoo.world=shoo_world.snapshot();auto& actor=shoo.runtime_entities.front();
        odh::RouteJobSnapshot job;static_cast<odh::LocalRouteSnapshot&>(job)=*actor.route;
        job.frame.state=odh::RouteFrameState::shooed;job.source_sparse->walk_to_pending=true;
        job.source_sparse->walk_to_purpose=odh::SourceWalkToPurpose::shoo;
        odh::SourceRouteCollisionSnapshot policy;policy.considered={{2,shoo_world.find(2)->generation}};
        job.source_sparse->collision=policy;job.sparse_consideration=job.source_sparse->avatars_to_consider;
        job.pending=*actor.vm.pending;job.route_frame=actor.vm.stack.back();
        job.external_frame_active=true;job.external_frame_owner=1;
        job.external_frame_call=odh::VmCall{odh::routine_key(4107),{1,3,3,1},{0,0,0,0,-1,-1},
            odh::VmCallReturnMode::ordinary,true};
        job.external_frame_purpose=odh::RouteExternalFramePurpose::shoo;
        job.external_frame_callee_generation=shoo_world.find(3)->generation;
        const auto& call=*job.external_frame_call;
        actor.vm.suspended_pending={job.pending};actor.vm.pending.reset();actor.vm.status=odh::VmStatus::running;
        actor.vm.stack.push_back({call.key,0,call.context,call.arguments,std::vector<std::int16_t>(6),
            odh::VmCallReturnMode::external_frame_next_tick,job.ticket,true});
        actor.route.reset();actor.route_stack=odh::RouteStackSnapshot{{job},shoo.host.tick,{}};
        shoo.scheduler.entities.front().status=actor.vm.status;
        const auto shoo_bytes=odh::encode_session_bundle(shoo);const auto opened=odh::decode_session_bundle(shoo_bytes);
        const auto& restored=opened.runtime_entities.front().route_stack->jobs.front();
        require(restored.external_frame_purpose==odh::RouteExternalFramePurpose::shoo&&
            restored.external_frame_callee_generation==shoo_world.find(3)->generation&&
            restored.frame.state==odh::RouteFrameState::shooed&&restored.source_sparse->walk_to_purpose==odh::SourceWalkToPurpose::shoo&&
            restored.source_sparse->motion.move_frames==sparse.motion.move_frames&&
            restored.external_frame_call->arguments==std::vector<std::int16_t>{0,0,0,0,-1,-1}&&
            odh::encode_session_bundle(opened)==shoo_bytes,
            "typed source SHOOED child preserves original motion, source default argument extent and exact Destination generation");
        auto wrong=shoo;
        wrong.runtime_entities.front().route_stack->jobs.front().external_frame_callee_generation++;
        rejects([&]{(void)odh::encode_session_bundle(wrong);},"shoo archive cannot bind a reused Destination identity");
        wrong=shoo;wrong.runtime_entities.front().route_stack->jobs.front().external_frame_purpose=odh::RouteExternalFramePurpose::portal;
        rejects([&]{(void)odh::encode_session_bundle(wrong);},"source shoo cannot alias the legacy portal-return policy");
        wrong=shoo;wrong.runtime_entities.front().route_stack->jobs.front().external_frame_call->arguments[4]=0;
        rejects([&]{(void)odh::encode_session_bundle(wrong);},"source shoo defaults beyond the fourth argument must stay minus one");
        wrong=shoo;wrong.runtime_entities.front().route_stack->jobs.front().source_sparse->walk_to_purpose=
            odh::SourceWalkToPurpose::ranked_alternative;
        rejects([&]{(void)odh::encode_session_bundle(wrong);},"exact-point shoo cannot borrow a ranked alternative's authority");
        auto returned=shoo;auto& resumed=returned.runtime_entities.front();resumed.vm.stack.pop_back();
        resumed.vm.pending=job.pending;resumed.vm.suspended_pending.clear();resumed.vm.status=odh::VmStatus::yielded;
        resumed.vm.external_frame_return=odh::VmExternalFrameReturn{job.ticket,false,1,job.run_generation};
        returned.scheduler.entities.front().status=resumed.vm.status;
        odh::World removed;require(removed.restore_snapshot(returned.world)&&removed.remove(3),
            "completed source shoo may have removed its temporary Destination");returned.world=removed.snapshot();
        const auto returned_bytes=odh::encode_session_bundle(returned);
        require(odh::encode_session_bundle(odh::decode_session_bundle(returned_bytes))==returned_bytes,
            "authenticated false shoo return survives removal of its exact callee without a portal retry");
        auto local=returned;auto& local_runtime=local.runtime_entities.front();
        local_runtime.route=static_cast<const odh::LocalRouteSnapshot&>(job);local_runtime.route_stack.reset();
        local_runtime.vm.external_frame_return.reset();local_runtime.vm.status=odh::VmStatus::waiting;
        local.scheduler.entities.front().status=local_runtime.vm.status;
        const auto local_bytes=odh::encode_session_bundle(local);const auto local_opened=odh::decode_session_bundle(local_bytes);
        const auto archive=odh::decode_session_archive(local_bytes);
        const auto section=std::find_if(archive.sections.begin(),archive.sections.end(),[](const auto& value){
            return value.id==std::array<char,4>{'R','O','U','T'};});
        require(section!=archive.sections.end()&&section->data[11]==11&&
            local_opened.runtime_entities.front().route->source_sparse->walk_to_pending&&
            local_opened.runtime_entities.front().route->source_sparse->walk_to_purpose==odh::SourceWalkToPurpose::shoo&&
            odh::encode_session_bundle(local_opened)==local_bytes,
            "ROUT11 preserves source exact-point SHOOED/WalkTo-null values without introducing a ranked manifest");
        // Mutate the extension itself, recomputing the archive envelope so that
        // rejection exercises the route readers rather than a checksum failure.
        const auto malformed_extension=[&](const auto& encoded,std::array<char,4> id,auto mutate,const char* message){
            auto malformed=odh::decode_session_archive(encoded);
            auto found=std::find_if(malformed.sections.begin(),malformed.sections.end(),[&](const auto& value){return value.id==id;});
            require(found!=malformed.sections.end()&&found->data.size()>11,"find source route extension roster");
            auto& data=found->data;
            const auto size=std::uint32_t(data[6])|(std::uint32_t(data[7])<<8)|(std::uint32_t(data[8])<<16)|(std::uint32_t(data[9])<<24);
            require(size<=data.size()-10,"source route first payload is bounded");
            mutate(data,std::size_t(10+size));
            rejects([&]{(void)odh::decode_session_bundle(odh::encode_session_archive(malformed));},message);
        };
        for(auto purpose:{std::uint8_t{0},std::uint8_t{255}})
            malformed_extension(local_bytes,{'R','O','U','T'},[&](auto& data,auto end){data[end-1]=purpose;},
                "ROUT11 rejects absent or unknown shoo purpose on the wire");
        malformed_extension(local_bytes,{'R','O','U','T'},[](auto& data,auto end){
            require(data[end-2]==0&&data[end-3]==1,"ROUT11 fixture has pending WalkTo and no Rooms");data[end-3]=0;},
            "ROUT11 rejects a shoo purpose without pending WalkTo");
        malformed_extension(shoo_bytes,{'R','S','T','K'},[](auto& data,auto end){
            require(data[end-38]==1&&data[end-37]==3,"RSTK2 fixture has one typed shoo extension");data[end-37]=255;},
            "RSTK2 rejects an unknown external purpose on the wire");
        malformed_extension(shoo_bytes,{'R','S','T','K'},[](auto& data,auto end){
            const auto marker=end-38;data[marker]=0;
            data.erase(data.begin()+marker+1,data.begin()+marker+10);
            const auto size=std::uint32_t(end-10-9);
            for(unsigned i=0;i<4;++i)data[6+i]=std::uint8_t(size>>(8*i));},
            "RSTK2 rejects its noncanonical extension version when no purpose is present");
        require(odh::encode_session_bundle(bundle)==bytes,"shoo schema extensions leave preexisting sparse archive bytes unchanged");
    }
    {
        auto collision=bundle;auto& state=*collision.runtime_entities.front().route->source_sparse;
        odh::SourceRouteCollisionSnapshot policy;policy.considered={{2,world.find(2)->generation}};
        state.collision=policy;
        const auto encoded=odh::encode_session_bundle(collision);const auto opened=odh::decode_session_bundle(encoded);
        require(opened.runtime_entities.front().route->source_sparse->collision&&
            opened.runtime_entities.front().route->source_sparse->collision->considered.front().generation==world.find(2)->generation&&
            odh::encode_session_bundle(opened)==encoded,"conditional collision continuation preserves exact policy and identities");
        state.collision->phase=odh::SourceRouteCollisionPhase::waiting;state.collision->wait_time=12;
        collision.runtime_entities.front().route->frame.wait_time=12;
        state.motion.phase=odh::WalkMotionPhase::collision_required;
        const auto waiting_bytes=odh::encode_session_bundle(collision);
        require(odh::decode_session_bundle(waiting_bytes).runtime_entities.front().route->source_sparse->collision->wait_time==12,
            "known collision wait persists alongside rejected motion");
        state.collision->considered.front().generation++;
        rejects([&]{(void)odh::encode_session_bundle(collision);},"collision consideration must name exact live avatar");
        state.collision->considered.front().generation--;state.collision->phase=odh::SourceRouteCollisionPhase::reroute_pending;
        rejects([&]{(void)odh::encode_session_bundle(collision);},"in-flight collision callback cannot be serialized");
    }
    {
        auto pending=bundle;auto& pending_runtime=pending.runtime_entities.front();
        set_source_range_route(pending_runtime,true);auto& progress=*pending_runtime.route;auto& retained=*progress.source_sparse;
        retained.walk_to_pending=true;retained.target_radian_direction=*progress.arrival_radian_direction;
        odh::SourceRouteCollisionSnapshot policy;policy.considered={{2,world.find(2)->generation}};
        retained.collision=policy;
        const auto old_motion=retained.motion;const auto new_goal=progress.path.back();
        require(!(new_goal==odh::RoutePoint{old_motion.remaining.back().d.x/hip,old_motion.remaining.back().d.y/hip}),
            "pending fallback fixture selects a new goal distinct from the retained old integrator endpoint");
        const auto check_pending=[&](const auto& value,const char* description){
            const auto encoded=odh::encode_session_bundle(value);const auto opened=odh::decode_session_bundle(encoded);
            const auto& route=*opened.runtime_entities.front().route;const auto& state=*route.source_sparse;
            const auto& expected_motion=value.runtime_entities.front().route->source_sparse->motion;
            require(state.walk_to_pending&&route.path.back()==new_goal&&state.motion.current->a.x==expected_motion.current->a.x&&
                state.motion.current->a.y==expected_motion.current->a.y&&state.motion.current->d.x==expected_motion.current->d.x&&
                state.motion.current->d.y==expected_motion.current->d.y&&state.motion.move_frames==expected_motion.move_frames&&
                state.motion.move_total_frames==expected_motion.move_total_frames&&
                state.collision->retries==retained.collision->retries&&state.collision->timeout==retained.collision->timeout&&
                odh::encode_session_bundle(opened)==encoded,description);
            const auto archive=odh::decode_session_archive(encoded);const auto section=std::find_if(archive.sections.begin(),archive.sections.end(),[](const auto& entry){
                return entry.id==std::array<char,4>{'R','O','U','T'};});
            require(section!=archive.sections.end()&&section->data[11]==10,"pending fallback uses conditional ROUT10 envelope");
        };
        check_pending(pending,"pending fallback preserves new selected goal separately from old walking geometry and counters");
        {
            auto anywhere=pending;auto& actor=anywhere.runtime_entities.front();set_source_range_route(actor,true,true);
            auto& route=*actor.route;route.target=route.actor;route.target_generation=route.actor_generation;
            route.target_anchor_radian_direction=route.source_range->target_radian_direction;
            actor.vm.pending->context.stack_object=route.actor;actor.vm.stack.back().context.stack_object=route.actor;
            route.source_sparse->face_anywhere=true;route.source_sparse->target_radian_direction=0;
            const auto encoded=odh::encode_session_bundle(anywhere);const auto opened=odh::decode_session_bundle(encoded);
            const auto& saved=*opened.runtime_entities.front().route;
            require(saved.face_anywhere&&saved.source_range&&saved.source_range->slot_index==8&&saved.source_range->slot.facing==-3&&
                saved.facing==0&&*saved.arrival_radian_direction==0&&saved.source_sparse->walk_to_pending&&
                saved.source_sparse->motion.current->a.x==old_motion.current->a.x&&odh::encode_session_bundle(opened)==encoded,
                "actual FaceAnywhere range SLOT shape preserves self anchor, frozen alternatives and pending ROUT10 old integrator");
            for(const auto& goal:saved.source_range->fallback_goals)require(goal.facing==0&&goal.radian_direction==0,
                "FaceAnywhere range save preserves neutral direction on every frozen fallback");
            auto plain=anywhere;plain.runtime_entities.front().route->source_sparse.reset();
            const auto plain_bytes=odh::encode_session_bundle(plain);const auto plain_opened=odh::decode_session_bundle(plain_bytes);
            const auto plain_archive=odh::decode_session_archive(plain_bytes);
            const auto plain_section=std::find_if(plain_archive.sections.begin(),plain_archive.sections.end(),[](const auto& section){
                return section.id==std::array<char,4>{'R','O','U','T'};});
            require(plain_opened.runtime_entities.front().route->source_range&&plain_section!=plain_archive.sections.end()&&
                plain_section->data[11]==7&&odh::encode_session_bundle(plain_opened)==plain_bytes,
                "FaceAnywhere source range uses canonical existing codec7 without sparse fallback state");
            auto generic=anywhere;auto& old_route=*generic.runtime_entities.front().route;
            old_route.source_sparse->choices=*old_route.source_range;old_route.source_range.reset();
            old_route.source_sparse->walk_to_pending=false;
            const auto generic_bytes=odh::encode_session_bundle(generic);
            require(odh::encode_session_bundle(odh::decode_session_bundle(generic_bytes))==generic_bytes,
                "older generic FaceAnywhere choice archives still restore without profile conversion");
            auto bad=anywhere;bad.runtime_entities.front().route->face_anywhere=false;
            rejects([&]{(void)odh::encode_session_bundle(bad);},"FaceAnywhere range row requires its exact arrival policy");
            bad=anywhere;bad.runtime_entities.front().route->source_range->selected.radian_direction=0.1f;
            bad.runtime_entities.front().route->arrival_radian_direction=0.1f;
            rejects([&]{(void)odh::encode_session_bundle(bad);},"FaceAnywhere range cannot carry a synthesized FaceTowards heading");
        }
        retained.motion.phase=odh::WalkMotionPhase::collision_required;
        check_pending(pending,"pending fallback permits ready policy with the retained rejected old path");
        retained.collision->phase=odh::SourceRouteCollisionPhase::waiting;retained.collision->wait_time=17;
        retained.collision->timeout=-2;progress.frame.wait_time=17;
        check_pending(pending,"pending fallback retains exhausted timeout and wait without counter replenishment");
        retained.motion.phase=odh::WalkMotionPhase::walking;
        check_pending(pending,"pending fallback wait is independent of the retained motion phase");
        auto finished_pending=pending;auto& finished_motion=finished_pending.runtime_entities.front().route->source_sparse->motion;
        finished_motion.current=line;finished_motion.remaining.clear();finished_motion.move_total_frames=-1;
        finished_motion.turn_frames=0;finished_motion.phase=odh::WalkMotionPhase::finished;
        check_pending(finished_pending,"pending fallback wait retains an old completed integrator independently from its new goal");
        auto shortcut=pending;auto& shortcut_route=*shortcut.runtime_entities.front().route;
        shortcut_route.expected.x=new_goal.x;shortcut_route.expected.y=new_goal.y;shortcut_route.path={new_goal};
        shortcut_route.frame.state=odh::RouteFrameState::turn_only;
        odh::World shortcut_world;require(shortcut_world.restore_snapshot(shortcut.world)&&
            shortcut_world.set_position(1,shortcut_route.expected),"set selected start-equals-goal shortcut pose");
        shortcut.world=shortcut_world.snapshot();
        check_pending(shortcut,"WalkTo-null turn-only shortcut preserves old current path and new selected pose");
        auto& shortcut_sparse=*shortcut_route.source_sparse;shortcut_sparse.lifecycle=2;
        shortcut_route.frame.state=odh::RouteFrameState::end_turn;
        odh::SourceLookTowardsRouteSnapshot shortcut_turn;shortcut_turn.mode=2;shortcut_turn.animation_index=7;
        shortcut_turn.animation_num_frames=12;shortcut_turn.animation_speed=1;
        shortcut_turn.start_radian_direction=shortcut_route.expected_radian_direction;
        shortcut_turn.target_radian_direction=shortcut_sparse.target_radian_direction;shortcut_sparse.turn=shortcut_turn;
        check_pending(shortcut,"WalkTo-null end-turn shortcut preserves old integrator through source EndWalk polling");
        odh::World portal_world;require(portal_world.restore_snapshot(pending.world)&&
            portal_world.create(3,odh::EntityKind::object)&&portal_world.set_position(3,{10,12,1,0})&&
            portal_world.create(4,odh::EntityKind::object)&&portal_world.set_position(4,{11,12,1,0}),
            "construct value-only fallback Rooms identities");
        pending.world=portal_world.snapshot();retained.fallback_room_route={{0,1,3,{10,12,1,0}},{1,2,4,{11,12,1,0}}};
        check_pending(pending,"pending fallback preserves exact ordered Rooms including room zero");
        auto planned=pending;auto& planned_sparse=*planned.runtime_entities.front().route->source_sparse;
        planned_sparse.walk_to_pending=false;planned_sparse.collision->phase=odh::SourceRouteCollisionPhase::ready;
        planned_sparse.collision->wait_time=0;planned.runtime_entities.front().route->frame.wait_time=0;
        odh::WalkMotion replacement;const odh::WalkSegmentGeometry replacement_line{odh::WalkSegmentKind::line,
            {progress.expected.x*hip,progress.expected.y*hip},{},{},{new_goal.x*hip,new_goal.y*hip}};
        require(replacement.initialize({replacement_line},{{progress.expected.x,progress.expected.y},progress.expected.level,
            progress.expected_radian_direction},planned_sparse.profile,false,planned_sparse.motion.velocity)==odh::WalkMotionStatus::success,
            "successful future plan constructs its replacement integrator from the live pose");
        planned_sparse.motion=*replacement.snapshot().snapshot;
        const auto planned_bytes=odh::encode_session_bundle(planned);const auto planned_opened=odh::decode_session_bundle(planned_bytes);
        require(!planned_opened.runtime_entities.front().route->source_sparse->walk_to_pending&&
            planned_opened.runtime_entities.front().route->source_sparse->fallback_room_route.size()==2&&
            odh::encode_session_bundle(planned_opened)==planned_bytes,
            "successful future plan retains source Rooms after WalkTo becomes available");
        auto unranked_rooms=planned;
        unranked_rooms.runtime_entities.front().route->source_range.reset();
        unranked_rooms.runtime_entities.front().route->entry_flags=0;
        rejects([&]{(void)odh::encode_session_bundle(unranked_rooms);},
            "retained Rooms require ranked SLOT provenance even after pending WalkTo clears");
        const auto reject_pending=[&](const auto& change,const char* description){auto bad=pending;change(bad);
            rejects([&]{(void)odh::encode_session_bundle(bad);},description);};
        reject_pending([](auto& value){value.runtime_entities.front().route->source_sparse->walk_to_pending=false;},
            "independent waiting policy and walking old motion require explicit WalkTo-null provenance");
        reject_pending([](auto& value){value.runtime_entities.front().route->source_sparse->motion={};},
            "pending fallback requires the supported retained old current path");
        reject_pending([](auto& value){value.runtime_entities.front().route->source_sparse->fallback_room_route.front().position.x++;},
            "pending fallback Rooms require exact live portal geometry");
        reject_pending([](auto& value){value.runtime_entities.front().route->source_sparse->fallback_room_route.back().source_room=7;},
            "pending fallback Rooms require contiguous execution order");
        reject_pending([](auto& value){value.runtime_entities.front().route->path.back().x++;},
            "pending fallback selected goal remains authenticated independently from old motion");
    }
    const auto archive=odh::decode_session_archive(bytes);
    const auto old_archive=odh::decode_session_archive(legacy_bytes);
    const auto route_section=std::find_if(archive.sections.begin(),archive.sections.end(),[](const auto& value){
        return value.id==std::array<char,4>{'R','O','U','T'};});
    const auto playback_section=std::find_if(archive.sections.begin(),archive.sections.end(),[](const auto& value){
        return value.id==std::array<char,4>{'P','L','A','Y'};});
    require(route_section!=archive.sections.end()&&route_section->data.size()>12&&route_section->data[11]==8&&
        playback_section!=archive.sections.end()&&playback_section->data.size()>11&&playback_section->data[10]==2,
        "source movement emits ROUT8 wrapping the old route and conditional PLAY2 collection");
    auto truncated=archive;auto truncated_route=std::find_if(truncated.sections.begin(),truncated.sections.end(),[](const auto& value){
        return value.id==std::array<char,4>{'R','O','U','T'};});
    auto& data=truncated_route->data;
    const auto record_size=static_cast<std::uint32_t>(data[6])|(static_cast<std::uint32_t>(data[7])<<8)|
        (static_cast<std::uint32_t>(data[8])<<16)|(static_cast<std::uint32_t>(data[9])<<24);
    require(record_size>1&&10+record_size<=data.size(),"locate sparse route entry for bounded truncation check");
    data.erase(data.begin()+static_cast<std::ptrdiff_t>(10+record_size-1));
    for(unsigned n=0;n<4;++n)data[6+n]=static_cast<std::uint8_t>((record_size-1)>>(8*n));
    rejects([&]{(void)odh::decode_session_bundle(odh::encode_session_archive(truncated));},
        "truncated sparse motion tail is rejected after updating outer section and roster lengths");
    for(const auto& section:old_archive.sections)if(section.id==std::array<char,4>{'M','E','T','A'}||
        section.id==std::array<char,4>{'H','H','R','D'}||section.id==std::array<char,4>{'R','E','L','A'}){
        const auto found=std::find_if(archive.sections.begin(),archive.sections.end(),[&](const auto& value){return value.id==section.id;});
        require(found!=archive.sections.end()&&found->data==section.data,"unrelated legacy section bytes remain unchanged by sparse locomotion");
    }
    require(odh::encode_session_bundle(legacy)==legacy_bytes,"absent sparse/locomotion extensions preserve original archive bytes");
    auto turning=bundle;auto& turn_runtime=turning.runtime_entities.front();auto& turn_route=*turn_runtime.route;
    auto& turn_sparse=*turn_route.source_sparse;auto finished=sparse.motion;
    finished.current=line;finished.remaining.clear();finished.previous_position={{16,12},1,route.expected_radian_direction};
    finished.move_frames=12;finished.move_total_frames=-1;finished.turn_frames=0;finished.phase=odh::WalkMotionPhase::finished;
    turn_sparse.motion=finished;turn_sparse.lifecycle=2;turn_route.frame.state=odh::RouteFrameState::end_turn;
    constexpr double pi=3.141592653589793238462643383279502884;
    odh::SourceLookTowardsRouteSnapshot turn;turn.mode=2;turn.start_radian_direction=static_cast<float>(pi/2);
    turn.target_radian_direction=0;turn.animation_index=7;turn.animation_num_frames=12;turn.animation_speed=1;
    turn.turn_tweak=-static_cast<float>(static_cast<double>(turn.start_radian_direction)-pi/2)/12.0f;
    turn_sparse.turn=turn;
    odh::World turn_world;require(turn_world.restore_snapshot(turning.world)&&turn_world.set_position(1,{28,12,1,2})&&
        turn_world.set_radian_direction(1,turn.start_radian_direction),"prepare frozen native end-turn archive pose");
    turning.world=turn_world.snapshot();turn_route.expected=*turn_world.find(1)->position;
    turn_route.expected_radian_direction=turn.start_radian_direction;turn_route.path={{28,12}};turn_route.next=1;
    turn_runtime.playback.source_walk.reset();turn_runtime.playback.state->resource.kind=odh::PlaybackResourceKey::Kind::route_walk;
    turn_runtime.playback.state->resource.id=7;turn_runtime.playback.state->loop=false;turn_runtime.playback.state->frame=3;
    turn_runtime.playback.state->frames=12;
    const auto turn_bytes=odh::encode_session_bundle(turning);const auto turn_reopened=odh::decode_session_bundle(turn_bytes);
    require(turn_reopened.runtime_entities.front().route->source_sparse->turn->animation_index==7&&
        turn_reopened.runtime_entities.front().route->source_sparse->turn->turn_tweak==turn.turn_tweak&&
        turn_reopened.runtime_entities.front().route->source_sparse->motion.phase==odh::WalkMotionPhase::finished&&
        odh::encode_session_bundle(turn_reopened)==turn_bytes,"finished sparse motion and active end-turn state survive the same archive codec");
    auto turn_only=bundle;auto& only_runtime=turn_only.runtime_entities.front();auto& only_route=*only_runtime.route;
    only_route.source_sparse->motion={};only_route.source_sparse->locomotion_started=false;only_route.source_sparse->hard_start=true;
    only_route.frame.state=odh::RouteFrameState::turn_only;only_route.walking_started=false;
    odh::World only_world;require(only_world.restore_snapshot(turn_only.world)&&only_world.set_position(1,{28,12,1,2})&&
        only_world.set_static_footprint(1,true),"prepare source same-XY TURN_ONLY checkpoint before motion initialization");
    turn_only.world=only_world.snapshot();only_route.expected=*only_world.find(1)->position;
    only_route.expected_radian_direction=only_world.find(1)->radian_direction;only_route.path={{28,12}};only_route.next=1;
    only_runtime.playback.source_walk.reset();only_runtime.playback.state.reset();
    const auto only_bytes=odh::encode_session_bundle(turn_only);const auto only_reopened=odh::decode_session_bundle(only_bytes);
    require(!only_reopened.runtime_entities.front().route->source_sparse->motion.current&&
        only_reopened.runtime_entities.front().route->frame.state==odh::RouteFrameState::turn_only&&
        odh::encode_session_bundle(only_reopened)==only_bytes,"uninitialized motion is preserved only as an explicit sparse TURN_ONLY boundary");
    const auto reject=[&](auto change,const char* why){auto wrong=bundle;change(wrong.runtime_entities.front());
        rejects([&]{(void)odh::encode_session_bundle(wrong);},why);};
    reject([](auto& value){value.route->source_sparse->motion.phase=odh::WalkMotionPhase::awaiting_placement;},
        "an unresolved source placement lease cannot be archived as completed motion");
    reject([](auto& value){value.route->source_sparse->motion.remaining.front().b.x=1;},
        "line segment canonical geometry remains strict inside a sparse route envelope");
    reject([](auto& value){value.route->source_sparse->avatars_to_consider.front().generation++;},
        "sparse consideration identities remain generation-bound");
    reject([](auto& value){value.playback.source_walk->secondary[0].speed=1;},"locomotion secondary speed must retain the source profile");
    reject([](auto& value){value.playback.source_walk->secondary[0].queue.push_back(17);},
        "secondary locomotion xevt queues cannot diverge from source primary-queue ownership");
    reject([](auto& value){value.playback.source_walk->weights[0]=std::numeric_limits<float>::quiet_NaN();},
        "locomotion weights must be finite");
    reject([](auto& value){value.route->source_sparse->motion={};},"walking state requires motion rather than an uninitialized turn-only placeholder");
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
void route_cache_archive_tests() {
    auto bundle=generic_action_fixture();auto& runtime=bundle.runtime_entities.front();
    const auto& owner=runtime.resource_owners.front();odh::RouteStackSnapshot cache;
    cache.portal_owners.push_back({owner.source,owner.asset,4097,owner.owner});
    cache.portal_planned_calls=2;cache.portal_returns=1;cache.portal_transitions=1;runtime.route_stack=cache;
    const auto bytes=odh::encode_session_bundle(bundle);const auto reopened=odh::decode_session_bundle(bytes);
    const auto& saved=*reopened.runtime_entities.front().route_stack;
    require(saved.jobs.empty()&&saved.portal_contexts.empty()&&saved.portal_owners.size()==1&&saved.portal_planned_calls==2&&
        saved.portal_returns==1&&saved.portal_transitions==1&&odh::encode_session_bundle(reopened)==bytes,
        "cache-only route state preserves authenticated portal banks and statistics without a live route");
    auto malformed=bundle;malformed.runtime_entities.front().route_stack->portal_owners.front().owner++;
    rejects([&]{(void)odh::encode_session_bundle(malformed);},"cache-only portal owner requires exact installed resource provenance");
    malformed=bundle;malformed.runtime_entities.front().route_stack->portal_contexts.push_back({});
    rejects([&]{(void)odh::encode_session_bundle(malformed);},"cache-only route state cannot contain an active portal context");
    malformed=bundle;malformed.runtime_entities.front().route_stack=odh::RouteStackSnapshot{};
    rejects([&]{(void)odh::encode_session_bundle(malformed);},"empty route stack must retain meaningful host cache or statistics");
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

odh::SessionBundle npc_avatar_fixture(bool reuse_original=false) {
    auto bundle=fixture();bundle.generic_execution=true;
    for(auto& actor:bundle.world.entities) {
        const auto guid=bundle.host.resident_guid+static_cast<std::uint32_t>(actor.id-1);
        actor.definition=odh::EntityType{guid,std::nullopt};
        odh::SessionHouseholdMember member;member.id=actor.id;member.generation=actor.generation;
        member.guid=guid;member.neighbor_id=static_cast<std::int16_t>(41+actor.id-1);
        member.object_definition_id=128;member.character_asset="UserData/Characters/User"+std::to_string(actor.id)+".iff";
        member.character_fingerprint.fill(0x31);bundle.household_roster.push_back(member);
    }
    odh::World world;require(world.restore_snapshot(bundle.world),"restore NPC fixture lot");
    constexpr std::uint32_t guid=0xfc72c0c0U;
    if(reuse_original){
        require(world.create_runtime_object(3,0x12345678u,{},{}),"original object occupies eventual NPC numeric ID");
        bundle.removed_original_objects.push_back({3,world.find(3)->generation,0x12345678u});
        require(world.remove(3),"original generation is removed before the ID is reused");
        bundle.object_history=odh::SessionObjectHistory{};bundle.object_history->reusable_entity_ids=true;
    }
    require(world.create(3,odh::EntityKind::avatar,{1,88,1,16})&&world.define_type(3,guid)&&
        world.set_person_data(3,31,40)&&world.set_position(3,{12,20,1,2}),"create independent visiting avatar");
    odh::EntityThreadControllers controllers(world,3);
    odh::Bhav main{0x8002,{{0,254,255,{}}},{},odh::Bhav::Metadata{0,4,4,0}};
    odh::RoutineSet programs{{odh::routine_key(4096),main}};
    odh::Vm vm({},world.vm_memory(),{},controllers.sleep_primitive(),{{guid,programs}});
    require(vm.start(odh::routine_key(4096),{3,3,3,guid},{5})==odh::VmStatus::running&&controllers.bind(vm)&&
        vm.run(1)==odh::VmStatus::waiting,"NPC own Main reaches saved Sleep wait");
    odh::SessionNpcAvatar npc;npc.origin.id=3;npc.origin.generation=world.find(3)->generation;
    npc.origin.guid=guid;npc.origin.neighbor_id=40;npc.origin.source="GameData/Objects/WorldNPC.far";
    npc.origin.asset="Pizza.iff";npc.origin.object_definition_id=128;npc.origin.character_fingerprint.fill(0x43);
    npc.primary_owner=guid;npc.runtime={3,vm.snapshot(),controllers.snapshot()};npc.selected_name="Synthetic Pizza";
    auto owner=resource_owner(guid,guid,4096);owner.identity=odh::SessionCodeOwnerIdentity::resource_guid;
    owner.bank_kind=odh::SessionCodeBankKind::complete_private;owner.source=npc.origin.source;
    owner.asset=npc.origin.asset;owner.definition_chunk_id=npc.origin.object_definition_id;
    owner.routine_fingerprint=odh::session_loaded_bank_fingerprint(programs);npc.runtime.resource_owners={owner};
    npc.decay.last_two_minute_bucket=7;npc.decay.fractions[0]=23;
    npc.decay.fractions[2]=std::numeric_limits<std::int16_t>::min();
    npc.decay.fractions[3]=std::numeric_limits<std::int16_t>::max();
    bundle.npc_avatars.push_back(npc);bundle.world=world.snapshot();
    bundle.scheduler.entities.push_back({3,vm.snapshot().run_generation,vm.snapshot().steps,
        vm.snapshot().status,false,false,false});
    return bundle;
}
void npc_avatar_cases() {
    const auto bundle=npc_avatar_fixture();const auto bytes=odh::encode_session_bundle(bundle);
    const auto decoded=odh::decode_session_bundle(bytes);
    require(decoded.npc_avatars.size()==1&&decoded.household_roster.size()==2&&decoded.runtime_entities.size()==2&&
        decoded.object_threads.empty()&&decoded.created_objects.empty()&&decoded.scheduler.entities.size()==3&&
        decoded.npc_avatars.front().origin.guid==0xfc72c0c0U&&decoded.npc_avatars.front().origin.neighbor_id==40&&
        decoded.npc_avatars.front().primary_owner==0xfc72c0c0U&&decoded.npc_avatars.front().selected_name=="Synthetic Pizza"&&
        decoded.npc_avatars.front().decay.fractions[0]==23&&
        decoded.npc_avatars.front().decay.fractions[2]==std::numeric_limits<std::int16_t>::min()&&
        decoded.npc_avatars.front().decay.fractions[3]==std::numeric_limits<std::int16_t>::max()&&
        odh::encode_session_bundle(decoded)==bytes,
        "independent NPC identity, Main, owners, body choice and motives round-trip");
    for(const std::int16_t neighbor_id:{40,0}){
        auto reused=npc_avatar_fixture(true);reused.npc_avatars.front().origin.neighbor_id=neighbor_id;
        const auto live=std::find_if(reused.world.entities.begin(),reused.world.entities.end(),
            [](const auto& entity){return entity.id==3;});
        live->person_data[31]=neighbor_id;
        const auto& original=reused.removed_original_objects.front();const auto& current=reused.npc_avatars.front().origin;
        require(original.id==current.id&&original.generation<current.generation,
            "recycled fixture has actual original and NPC constructor generations, not a renamed identity");
        const auto encoded=odh::encode_session_bundle(reused);const auto reopened=odh::decode_session_bundle(encoded);
        require(reopened.npc_avatars.front().origin.generation==current.generation&&
            reopened.removed_original_objects.front().generation==original.generation&&
            reopened.object_history->reusable_entity_ids&&odh::encode_session_bundle(reopened)==encoded,
            "live NPC preserves the newer generation while retaining removed original provenance at the same numeric ID");
        auto wrong=reused;wrong.object_history->reusable_entity_ids=false;
        rejects([&]{(void)odh::encode_session_bundle(wrong);},"NPC reuses removed original ID without explicit reusable history");
        wrong=reused;wrong.object_history.reset();
        rejects([&]{(void)odh::encode_session_bundle(wrong);},"NPC reuses removed original ID without history");
        wrong=reused;wrong.removed_original_objects.front().generation=current.generation;
        rejects([&]{(void)odh::encode_session_bundle(wrong);},"NPC removed-original provenance aliases its live generation");
        wrong=reused;wrong.removed_original_objects.front().generation=current.generation+1;
        wrong.world.generation_sequence=current.generation+1;
        rejects([&]{(void)odh::encode_session_bundle(wrong);},"NPC removed-original provenance is newer than the reused live generation");
    }
    // A missing NBRS person bank preserves the constructor's101-word bank;
    // saved NBRS data produces exactly88. Neither profile is padded on load.
    auto absent_person=bundle;odh::World absent_world;
    require(absent_world.restore_snapshot(bundle.world),"restore absent-NBRS NPC fixture");
    auto constructor_person=absent_world.find(3)->person_data;constructor_person.resize(101);
    require(absent_world.replace_person_data(3,constructor_person),"preserve constructor101 without NBRS bank");
    absent_person.world=absent_world.snapshot();
    const auto absent_bytes=odh::encode_session_bundle(absent_person);
    require(odh::decode_session_bundle(absent_bytes).world.entities.back().person_data.size()==101&&
        odh::encode_session_bundle(odh::decode_session_bundle(absent_bytes))==absent_bytes,
        "NPC constructor101 person bank survives without fake padding or truncation");
    for(const std::size_t count:{69u,80u,100u}) {
        auto malformed=bundle;odh::World malformed_world;
        require(malformed_world.restore_snapshot(bundle.world),"restore malformed-NBRS NPC fixture");
        auto person=malformed_world.find(3)->person_data;person.resize(count);
        require(malformed_world.replace_person_data(3,person),"construct bounded but unsupported NPC person bank");
        malformed.world=malformed_world.snapshot();
        rejects([&]{odh::encode_session_bundle(malformed);},"unsupported NPC69/80/100 person profile accepted");
    }
    auto linked=bundle;auto& linked_primary=linked.npc_avatars.front().runtime.resource_owners.front();
    linked_primary.bank_kind=odh::SessionCodeBankKind::private_and_semiglobal;
    linked_primary.linked_semiglobal=odh::SessionLinkedSemiglobal{"PersonGlobals","GameData/Global/Global.far","PersonGlobals.iff"};
    const auto linked_bytes=odh::encode_session_bundle(linked);
    require(odh::decode_session_bundle(linked_bytes).npc_avatars.front().runtime.resource_owners.front().bank_kind==
        odh::SessionCodeBankKind::private_and_semiglobal,"NPC combined private/GLOB primary bank round-trip");
    auto archive_entry=bundle;
    // FAR entry names are opaque catalog identities; commercial entries retain
    // their exact backslashes rather than acting as filesystem paths.
    archive_entry.npc_avatars.front().origin.asset="People\\\\PizzaDude.iff";
    archive_entry.npc_avatars.front().runtime.resource_owners.front().asset=archive_entry.npc_avatars.front().origin.asset;
    const auto entry_bytes=odh::encode_session_bundle(archive_entry);
    require(odh::decode_session_bundle(entry_bytes).npc_avatars.front().origin.asset=="People\\\\PizzaDude.iff"&&
        odh::encode_session_bundle(odh::decode_session_bundle(entry_bytes))==entry_bytes,
        "exact archive entry backslashes survive NPC provenance round-trip");
    auto base=bundle;base.npc_avatars.clear();odh::World base_world;
    require(base_world.restore_snapshot(base.world)&&base_world.remove(3),"remove NPC from old profile fixture");
    base.world=base_world.snapshot();base.scheduler.entities.pop_back();
    // Unchanged profiles retain their exact section bytes when a visitor appears.
    const auto base_archive=odh::decode_session_archive(odh::encode_session_bundle(base));
    const auto npc_archive=odh::decode_session_archive(bytes);
    for(const auto& section:base_archive.sections) {
        if(section.id==std::array<char,4>{'W','R','L','D'}||section.id==std::array<char,4>{'S','C','H','D'})continue;
        const auto found=std::find_if(npc_archive.sections.begin(),npc_archive.sections.end(),
            [&](const auto& value){return value.id==section.id;});
        require(found!=npc_archive.sections.end()&&found->data==section.data&&found->required==section.required,
            "NPC changed an existing resident profile payload");
    }
    require(std::none_of(base_archive.sections.begin(),base_archive.sections.end(),[](const auto& section){
        return section.id==std::array<char,4>{'N','P','C','A'};}),"empty NPC section must be omitted");
    // A visiting avatar uses the same Route45/playback validators and wire
    // values as residents, without becoming part of their execution roster.
    auto routed=bundle;auto& runtime=routed.npc_avatars.front().runtime;
    auto& vm=runtime.vm;const odh::VmContext context{3,1,1,0xfc72c0c0U};
    vm.stack.front().context=context;vm.pending->context=context;
    vm.pending->instruction.instruction={45,254,255,{1,0,2,0,0,0,0,0}};
    runtime.controllers.sleep.job.reset();runtime.route.emplace();auto& route=*runtime.route;
    routed.world.entities.front().position=odh::EntityPosition{28,20,1,0};
    route.ticket=vm.pending->ticket;route.actor=3;route.target=1;
    route.actor_generation=routed.world.entities.back().generation;
    route.target_generation=routed.world.entities.front().generation;route.run_generation=vm.run_generation;
    route.expected=*routed.world.entities.back().position;route.target_position=*routed.world.entities.front().position;
    route.expected_radian_direction=routed.world.entities.back().radian_direction;
    route.frame.state=odh::RouteFrameState::initial;route.path={{12,20},{13,20}};route.next=1;
    route.entry_flags=4;route.facing=2;route.same_room_verified=true;
    odh::PlaybackResourceKey carry;carry.source=1;carry.id=7;carry.caller=3;carry.callee=3;
    carry.stack_object=3;carry.code_owner=0xfc72c0c0U;runtime.playback.carry=carry;
    const auto routed_bytes=odh::encode_session_bundle(routed);
    const auto route_roundtrip=odh::decode_session_bundle(routed_bytes);
    require(route_roundtrip.npc_avatars.front().runtime.route&&
        route_roundtrip.npc_avatars.front().runtime.route->actor==3&&
        route_roundtrip.npc_avatars.front().runtime.playback.carry==carry&&
        odh::encode_session_bundle(route_roundtrip)==routed_bytes,"NPC route and playback resource continuation round-trip");
    {
        auto stacked=routed;auto& state=stacked.npc_avatars.front().runtime;
        odh::RouteJobSnapshot job;static_cast<odh::LocalRouteSnapshot&>(job)=*state.route;
        job.pending=*state.vm.pending;job.route_frame=state.vm.stack.back();state.route.reset();
        state.route_stack=odh::RouteStackSnapshot{{job},stacked.host.tick,{}};
        const auto encoded=odh::encode_session_bundle(stacked);const auto reopened=odh::decode_session_bundle(encoded);
        const auto archive=odh::decode_session_archive(encoded);const auto section=std::find_if(archive.sections.begin(),archive.sections.end(),[](const auto& value){
            return value.id==std::array<char,4>{'N','P','C','A'};});
        require(section!=archive.sections.end()&&section->data.front()==2&&reopened.npc_avatars.front().runtime.route_stack&&
            reopened.npc_avatars.front().runtime.route_stack->jobs.front().actor==3&&odh::encode_session_bundle(reopened)==encoded,
            "NPC uses conditional NPCA2 for a canonical stack continuation");
    }
    auto bad_route=routed;bad_route.npc_avatars.front().runtime.route->actor_generation++;
    rejects([&]{odh::encode_session_bundle(bad_route);},"stale NPC route actor accepted");
    bad_route=routed;bad_route.npc_avatars.front().runtime.playback.carry->caller=1;
    rejects([&]{odh::encode_session_bundle(bad_route);},"NPC playback aliases resident actor");
    for(const auto point_profile:{false,true}){
        auto self=routed;auto& state=self.npc_avatars.front().runtime;auto& progress=*state.route;
        progress.target=progress.actor;progress.target_generation=progress.actor_generation;
        progress.target_position={progress.path.front().x,progress.path.front().y,progress.expected.level,0};
        progress.target_anchor_radian_direction=0.31F;
        state.vm.stack.back().context.stack_object=progress.actor;state.vm.pending->context.stack_object=progress.actor;
        if(point_profile){progress.arrival_radian_direction=1.2345F;progress.entry_flags=0;progress.call_failure_trees=true;}
        const auto encoded=odh::encode_session_bundle(self);const auto decoded=odh::decode_session_bundle(encoded);
        require(decoded.npc_avatars.front().runtime.route->target_anchor_radian_direction==std::optional<float>{0.31F}&&
            odh::encode_session_bundle(decoded)==encoded,"self-target visitor route preserves its frozen transform independently of progress");
        auto wrong=self;wrong.npc_avatars.front().runtime.route->target_anchor_radian_direction.reset();
        rejects([&]{odh::encode_session_bundle(wrong);},"self-target route requires its original anchor");
        wrong=self;wrong.npc_avatars.front().runtime.route->target_anchor_radian_direction=std::numeric_limits<float>::quiet_NaN();
        rejects([&]{odh::encode_session_bundle(wrong);},"self-target route rejects non-finite anchor angle");
        wrong=self;wrong.npc_avatars.front().runtime.route->target_position.x++;
        rejects([&]{odh::encode_session_bundle(wrong);},"self-target frozen position must match the original path start");
        wrong=self;wrong.npc_avatars.front().runtime.route->target_generation++;
        rejects([&]{odh::encode_session_bundle(wrong);},"self-target identity cannot name a different actor lifetime");
    }
    auto foreign_anchor=routed;foreign_anchor.npc_avatars.front().runtime.route->target_anchor_radian_direction=0;
    rejects([&]{odh::encode_session_bundle(foreign_anchor);},"non-self route cannot claim a self-target anchor");
    for(const auto no_failure:std::array<std::uint8_t,2>{0,1}){
        auto point=routed;auto& state=point.npc_avatars.front().runtime;
        state.vm.pending->instruction.instruction.operands[4]=no_failure;
        state.route->entry_flags=0;state.route->arrival_radian_direction=1.2345F;
        state.route->call_failure_trees=no_failure==0;
        const auto encoded=odh::encode_session_bundle(point);
        const auto decoded=odh::decode_session_bundle(encoded);
        require(decoded.npc_avatars.front().runtime.route->arrival_radian_direction==std::optional<float>{1.2345F}&&
            odh::encode_session_bundle(decoded)==encoded,
            "NPC source exact SLOT route preserves continuous arrival and authored failure policy");
        for(const auto retain_angle:{false,true}){
            auto moved=point;
            auto& captured=*moved.npc_avatars.front().runtime.route;
            if(retain_angle)captured.target_anchor_radian_direction=0.31F;
            moved.world.entities.front().position->y++;
            moved.world.entities.front().position->direction=1;
            moved.world.entities.front().radian_direction=0.85F;
            if(!retain_angle){
                rejects([&]{odh::encode_session_bundle(moved);},
                    "old source point without captured target angle cannot accept a moved and rotated target");
                continue;
            }
            const auto moved_bytes=odh::encode_session_bundle(moved);
            const auto reopened=odh::decode_session_bundle(moved_bytes);
            const auto& saved=*reopened.npc_avatars.front().runtime.route;
            require(saved.target_position==state.route->target_position&&
                saved.target_anchor_radian_direction==captured.target_anchor_radian_direction&&
                saved.path==captured.path&&odh::encode_session_bundle(reopened)==moved_bytes,
                "source point archive retains captured goals and angle after its exact target moves");
            auto stale=moved;stale.npc_avatars.front().runtime.route->target_generation++;
            rejects([&]{odh::encode_session_bundle(stale);},
                "captured source point still rejects a replaced target generation");
        }
        auto wrong=point;wrong.npc_avatars.front().runtime.route->call_failure_trees=!state.route->call_failure_trees;
        rejects([&]{odh::encode_session_bundle(wrong);},"SLOT route failure policy differs from its source instruction");
        wrong=point;wrong.npc_avatars.front().runtime.route->arrival_radian_direction=std::numeric_limits<float>::infinity();
        rejects([&]{odh::encode_session_bundle(wrong);},"continuous SLOT arrival rejects non-finite direction");
        wrong=point;wrong.npc_avatars.front().runtime.route->entry_flags=4;
        rejects([&]{odh::encode_session_bundle(wrong);},"exact SLOT profile cannot declare different entry geometry");
        wrong=point;wrong.npc_avatars.front().runtime.vm.pending->instruction.instruction={27,254,255,{0,0,254,255,0,0,0,0}};
        rejects([&]{odh::encode_session_bundle(wrong);},"relative route cannot carry a source SLOT profile marker");
    }
    {
        const auto social_slot=[](std::uint32_t flags=255,std::int32_t standing=1,
                                  std::int32_t radius=16) {
            odh::SlotRecord slot{};slot.type=3;slot.offset_bits={0,0,0};
            slot.standing=standing;slot.sitting=0;slot.ground=0;slot.flags=flags;
            slot.snap_target_slot=-1;slot.proximity=std::array<std::int32_t,3>{radius,radius,16};
            slot.max_size=0;slot.unknown_i10=0;slot.gradient_bits=0x3E400000;
            slot.height=0;slot.facing=-2;slot.resolution=16;return slot;
        };
        odh::SlotTable table{0,10,std::vector<odh::SlotRecord>(5),0};
        for(auto& slot:table.records)slot.type=3;
        table.records[4]=social_slot();
        auto circle=routed;auto& state=circle.npc_avatars.front().runtime;
        auto& route=*state.route;
        state.vm.pending->instruction.instruction={45,254,255,{4,0,2,0,0,0,0,0}};
        const auto candidates=odh::enumerate_source_slot_circle_candidates(table,4,
            route.target_position,0.0f);
        const auto start=odh::RoutePoint{route.expected.x,route.expected.y};
        const auto selected=std::find_if(candidates.begin(),candidates.end(),[&](const auto& goal){
            return !(goal.position==start)&&goal.entry_flags!=0;
        });
        require(selected!=candidates.end()&&candidates.size()>=2,
            "source Social circle fixture obtains distinct candidates from the frozen global SLOT row");
        route.path={start,selected->position};route.next=1;route.entry_flags=selected->entry_flags;
        route.facing=selected->facing;route.arrival_radian_direction=selected->radian_direction;
        route.call_failure_trees=true;
        odh::SourceSlotCircleRouteSnapshot profile;profile.slot_version=10;profile.slot_index=4;
        profile.slot=social_slot();profile.target_radian_direction=0.0f;profile.selected=*selected;
        for(const auto& candidate:candidates)
            if(candidate.entry_flags&&!(candidate.position==selected->position)&&profile.fallback_goals.size()<2)
                profile.fallback_goals.push_back(candidate);
        route.source_circle=profile;
        // The VM keeps its original choices if this non-self StackObject moves.
        circle.world.entities.front().position=odh::EntityPosition{31,25,1,4};
        const auto circle_bytes=odh::encode_session_bundle(circle);
        const auto circle_archive=odh::decode_session_archive(circle_bytes);
        const auto circle_section=std::find_if(circle_archive.sections.begin(),circle_archive.sections.end(),
            [](const auto& section){return section.id==std::array<char,4>{{'N','P','C','A'}};});
        const auto npc_route_wire_offsets=[](const std::vector<std::uint8_t>& data) {
            const auto u16=[&](std::size_t& offset){
                if(offset+2>data.size())throw std::runtime_error("truncated NPC text wire fixture");
                const auto value=static_cast<std::uint16_t>(data[offset]|(data[offset+1]<<8));
                offset+=2;return value;
            };
            const auto u32=[&](std::size_t& offset){
                if(offset+4>data.size())throw std::runtime_error("truncated NPC route wire fixture");
                const auto value=static_cast<std::uint32_t>(data[offset])|
                    (static_cast<std::uint32_t>(data[offset+1])<<8)|
                    (static_cast<std::uint32_t>(data[offset+2])<<16)|
                    (static_cast<std::uint32_t>(data[offset+3])<<24);
                offset+=4;return value;
            };
            std::size_t offset=4;const auto entry_size=u32(offset);
            const auto entry_end=offset+entry_size;
            if(entry_end>data.size())throw std::runtime_error("invalid NPC entry fixture bounds");
            offset+=16;
            for(unsigned string=0;string<2;++string){const auto size=u16(offset);offset+=size;}
            offset+=2+32+4+1+2*odh::ts1_need_count;
            if(offset>=entry_end)throw std::runtime_error("truncated NPC selected-name marker");
            const auto has_name=data[offset++];
            if(has_name>1)throw std::runtime_error("invalid NPC selected-name marker");
            if(has_name){const auto size=u16(offset);offset+=size;}
            for(unsigned blob=0;blob<3;++blob){const auto size=u32(offset);offset+=size;}
            if(offset>=entry_end||data[offset++]!=1)
                throw std::runtime_error("NPC route is absent from its NPCA entry");
            const auto route_size=u32(offset);
            if(route_size<2||offset+route_size>entry_end)
                throw std::runtime_error("NPC route payload has invalid bounds");
            return std::pair<std::size_t,std::size_t>{offset,offset-4};
        };
        auto look=routed;auto& look_runtime=look.npc_avatars.front().runtime;
        auto& look_vm=look_runtime.vm;auto& look_route=*look_runtime.route;
        const auto look_actor=look.world.entities.back();
        const auto start_angle=look_actor.radian_direction;
        const auto notch=[](float radians){
            constexpr double pi=3.141592653589793238462643383279502884;
            auto value=std::fmod(std::nearbyint(static_cast<double>(radians)/(pi/4)),8.0);
            if(value<0)value+=8;
            return static_cast<std::uint8_t>(value);
        };
        look_vm.pending->instruction.instruction={22,254,255,{2,0,0,0,0,0,0,0}};
        look_route.target=look_route.actor;look_route.target_generation=look_route.actor_generation;
        look_route.target_position=*look_actor.position;
        look_route.target_position.direction=notch(start_angle);
        look_route.expected=*look_actor.position;look_route.expected_radian_direction=start_angle;
        look_route.frame={};look_route.frame.state=odh::RouteFrameState::turn_only;
        look_route.path={{look_route.expected.x,look_route.expected.y}};look_route.next=1;
        look_route.entry_flags=1;look_route.facing=notch(start_angle+1.57079632679F);
        look_route.face_anywhere=false;look_route.walking_started=false;
        look_route.same_room_verified=true;look_route.call_failure_trees=false;
        odh::SourceLookTowardsRouteSnapshot look_profile;
        look_profile.mode=2;look_profile.start_radian_direction=start_angle;
        look_profile.target_radian_direction=start_angle+1.57079632679F;
        look_profile.animation_index=0;look_profile.animation_speed=1;
        look_route.look_towards=look_profile;
        const auto look_bytes=odh::encode_session_bundle(look);
        const auto look_archive=odh::decode_session_archive(look_bytes);
        const auto look_section=std::find_if(look_archive.sections.begin(),look_archive.sections.end(),
            [](const auto& section){return section.id==std::array<char,4>{{'N','P','C','A'}};});
        require(look_section!=look_archive.sections.end(),"LookTowards fixture has an NPCA section");
        const auto look_offsets=npc_route_wire_offsets(look_section->data);
        const auto look_restored=odh::decode_session_bundle(look_bytes);
        require(look_section->data[look_offsets.first]==6&&
            look_restored.npc_avatars.front().runtime.route->look_towards->mode==2&&
            odh::encode_session_bundle(look_restored)==look_bytes,
            "LookTowards TURN_ONLY uses v6 and retains its frozen angle and actor route");
        auto invalid_look=look;invalid_look.npc_avatars.front().runtime.route->look_towards->mode=3;
        rejects([&]{odh::encode_session_bundle(invalid_look);},
            "LookTowards source mode differing from pending opcode 22 is rejected");
        invalid_look=look;invalid_look.npc_avatars.front().runtime.route.reset();
        rejects([&]{odh::encode_session_bundle(invalid_look);},
            "pending body LookTowards without its native route is rejected");
        invalid_look=look;invalid_look.npc_avatars.front().runtime.route->look_towards->animation_index=5;
        rejects([&]{odh::encode_session_bundle(invalid_look);},
            "LookTowards cannot claim an unsupported turn stage");
        invalid_look=look;invalid_look.npc_avatars.front().runtime.vm.pending->context.callee++;
        rejects([&]{odh::encode_session_bundle(invalid_look);},
            "LookTowards pending callee differing from its protected VM frame is rejected");
        auto started_look=look;
        auto& started_runtime=started_look.npc_avatars.front().runtime;
        auto& started_route=*started_runtime.route;
        auto& started_profile=*started_route.look_towards;
        constexpr double look_pi=3.141592653589793238462643383279502884;
        started_profile.target_radian_direction=start_angle+2.0F;
        const auto diff=std::fmod(static_cast<double>(started_profile.target_radian_direction)-start_angle,look_pi*2);
        const auto magnitude=std::abs(diff>look_pi?diff-look_pi*2:diff< -look_pi?diff+look_pi*2:diff);
        const auto selected_index=static_cast<std::uint8_t>((diff>0?0:1)+6);
        started_profile.animation_index=selected_index;started_profile.animation_num_frames=12;
        started_profile.animation_speed=1;
        auto residual=static_cast<float>(magnitude-look_pi/2);
        if(selected_index&1U)residual=-residual;
        started_profile.turn_tweak=residual/12.0F;
        started_route.frame.state=odh::RouteFrameState::end_turn;
        started_route.facing=notch(started_profile.target_radian_direction);
        odh::PlaybackClipSnapshot turn_clip{};
        turn_clip.resource.kind=odh::PlaybackResourceKey::Kind::animation;
        turn_clip.resource.source=1;turn_clip.resource.id=17;turn_clip.resource.caller=3;
        turn_clip.resource.callee=look_vm.pending->context.callee;
        turn_clip.resource.stack_object=look_vm.pending->context.stack_object;
        turn_clip.resource.code_owner=look_vm.pending->context.code_owner;
        turn_clip.resource.actor_age=look_actor.person_data.size()>58?look_actor.person_data[58]:0;
        turn_clip.actor=3;turn_clip.actor_generation=look_actor.generation;
        turn_clip.vm_generation=look_vm.run_generation;turn_clip.frame=2;turn_clip.speed=1;
        turn_clip.frames=20;turn_clip.loop=false;
        started_runtime.playback.state=turn_clip;
        started_runtime.playback.hands=odh::PlaybackHandsSnapshot{{},3,look_actor.generation,look_vm.run_generation};
        const auto started_bytes=odh::encode_session_bundle(started_look);
        require(odh::encode_session_bundle(odh::decode_session_bundle(started_bytes))==started_bytes,
            "LookTowards END_TURN accepts the current shared primary clip after source replacement");
        auto missing_turn=started_look;missing_turn.npc_avatars.front().runtime.playback.state.reset();
        rejects([&]{odh::encode_session_bundle(missing_turn);},
            "started LookTowards without a shared current primary clip is rejected");
        auto waiting_turn=started_look;
        waiting_turn.npc_avatars.front().runtime.playback.state->wait=odh::PlaybackWaitSnapshot{
            waiting_turn.npc_avatars.front().runtime.vm.pending->ticket,
            odh::VmFrameBank::arguments,0,0};
        rejects([&]{odh::encode_session_bundle(waiting_turn);},
            "started LookTowards cannot overlap a blocking animation wait");
        const auto restored=odh::decode_session_bundle(circle_bytes);
        require(circle_section!=circle_archive.sections.end()&&
                circle_section->data[npc_route_wire_offsets(circle_section->data).first]==5,
            "source-circle route selects the v5 payload");
        require(restored.npc_avatars.front().runtime.route->source_circle.has_value(),
            "source-circle route profile survives reopening");
        const auto& restored_circle=*restored.npc_avatars.front().runtime.route->source_circle;
        require(restored_circle.slot.flags==255&&restored_circle.slot.offset_bits==
                std::array<std::uint32_t,3>{0,0,0}&&restored_circle.slot.proximity==
                std::optional<std::array<std::int32_t,3>>{{16,16,16}},
            "source-circle exact row survives reopening");
        require(restored_circle.fallback_goals.size()==2,
            "source-circle ordered fallback list survives reopening");
        require(restored.npc_avatars.front().runtime.route->arrival_radian_direction==
                std::optional<float>{selected->radian_direction},
            "source-circle continuous arrival angle survives reopening");
        require(odh::encode_session_bundle(restored)==circle_bytes,
            "source-circle route and frozen moved-target choices re-encode byte-identically");
        auto out_of_world=circle;
        const auto frozen_target=route.target;
        auto moved_target=std::find_if(out_of_world.world.entities.begin(),out_of_world.world.entities.end(),
            [&](const auto& entity){return entity.id==frozen_target;});
        require(moved_target!=out_of_world.world.entities.end(),"frozen circle target retains World identity");
        moved_target->position.reset();
        const auto out_of_world_bytes=odh::encode_session_bundle(out_of_world);
        require(odh::encode_session_bundle(odh::decode_session_bundle(out_of_world_bytes))==out_of_world_bytes,
            "circle keeps its frozen choices when the same target temporarily leaves the lot geometry");
        auto self_retry=circle;auto& self_runtime=self_retry.npc_avatars.front().runtime;
        auto& self_route=*self_runtime.route;
        auto actor=std::find_if(self_retry.world.entities.begin(),self_retry.world.entities.end(),
            [&](const auto& entity){return entity.id==self_route.actor;});
        require(actor!=self_retry.world.entities.end()&&actor->position,
            "source-circle self retry fixture has its actor entity");
        self_route.target=self_route.actor;self_route.target_generation=self_route.actor_generation;
        self_route.target_position=*actor->position;
        self_route.target_anchor_radian_direction=actor->radian_direction;
        self_runtime.vm.stack.back().context.stack_object=self_route.actor;
        self_runtime.vm.pending->context.stack_object=self_route.actor;
        const auto self_goals=odh::enumerate_source_slot_circle_candidates(table,4,
            self_route.target_position,actor->radian_direction);
        const auto self_start=std::find_if(self_goals.begin(),self_goals.end(),[](const auto& goal){
            return goal.entry_flags!=0;
        });
        const auto self_selected=self_start==self_goals.end()?self_goals.end():
            std::find_if(std::next(self_start),self_goals.end(),[](const auto& goal){return goal.entry_flags!=0;});
        require(self_start!=self_goals.end()&&self_selected!=self_goals.end(),
            "source-circle self retry fixture has a distinct frozen fallback goal");
        actor->position=odh::EntityPosition{self_start->position.x,self_start->position.y,
            self_route.expected.level,self_route.expected.direction};
        self_route.expected=*actor->position;self_route.path={self_start->position,self_selected->position};
        self_route.next=1;self_route.entry_flags=self_selected->entry_flags;
        self_route.facing=self_selected->facing;
        self_route.arrival_radian_direction=self_selected->radian_direction;
        auto& self_profile=*self_route.source_circle;self_profile.target_radian_direction=actor->radian_direction;
        self_profile.selected=*self_selected;self_profile.fallback_goals.clear();
        for(auto it=std::next(self_selected);it!=self_goals.end()&&self_profile.fallback_goals.size()<2;++it)
            if(it->entry_flags)self_profile.fallback_goals.push_back(*it);
        const auto self_retry_bytes=odh::encode_session_bundle(self_retry);
        const auto self_retry_restored=odh::decode_session_bundle(self_retry_bytes);
        require(self_retry_restored.npc_avatars.front().runtime.route->target_anchor_radian_direction==
                std::optional<float>{actor->radian_direction}&&
            !(self_retry_restored.npc_avatars.front().runtime.route->path.front()==
              odh::RoutePoint{self_route.target_position.x,self_route.target_position.y})&&
            odh::encode_session_bundle(self_retry_restored)==self_retry_bytes,
            "self-circle retry retains its frozen anchor while the new path starts at the actor's current fallback position");
        const auto profile_roundtrip=[&](std::size_t index,std::uint16_t scope,
                                         std::uint32_t flags,std::int32_t standing,
                                         std::int32_t radius) {
            auto profile_bundle=circle;auto& runtime=profile_bundle.npc_avatars.front().runtime;
            auto& saved_route=*runtime.route;
            odh::SlotTable slots{0,10,std::vector<odh::SlotRecord>(index+1),0};
            for(auto& value:slots.records)value.type=3;
            slots.records[index]=social_slot(flags,standing,radius);
            if(index==13){slots.records[index].max_size=42;slots.records[index].unknown_i10=3;
                slots.records[index].gradient_bits=0x12345678;slots.records[index].height=2;
                slots.records[index].resolution=8;}
            const auto choices=odh::enumerate_source_slot_circle_candidates(slots,index,
                saved_route.target_position,0.0f);
            const auto choice=std::find_if(choices.begin(),choices.end(),[&](const auto& goal){
                return goal.entry_flags&&!(goal.position==start);
            });
            require(choice!=choices.end(),"generic source-circle fixture has a routable candidate");
            const auto data=scope==0?std::uint16_t{0}:static_cast<std::uint16_t>(index);
            runtime.vm.pending->instruction.instruction={45,254,255,
                {static_cast<std::uint8_t>(data),static_cast<std::uint8_t>(data>>8),
                 static_cast<std::uint8_t>(scope),static_cast<std::uint8_t>(scope>>8),0,0,0,0}};
            if(scope==0)runtime.vm.stack.back().arguments={static_cast<std::int16_t>(index)};
            saved_route.path={start,choice->position};saved_route.next=1;
            saved_route.entry_flags=choice->entry_flags;saved_route.facing=choice->facing;
            saved_route.arrival_radian_direction=choice->radian_direction;
            odh::SourceSlotCircleRouteSnapshot value;value.slot_version=10;value.slot_index=index;
            value.slot=slots.records[index];value.target_radian_direction=0.0f;value.selected=*choice;
            for(auto it=std::next(choice);it!=choices.end()&&value.fallback_goals.size()<2;++it)
                if(it->entry_flags)value.fallback_goals.push_back(*it);
            saved_route.source_circle=value;
            const auto encoded=odh::encode_session_bundle(profile_bundle);
            const auto reopened=odh::decode_session_bundle(encoded);
            require(reopened.npc_avatars.front().runtime.route->source_circle->slot_index==index&&
                reopened.npc_avatars.front().runtime.route->source_circle->slot.flags==flags&&
                reopened.npc_avatars.front().runtime.route->source_circle->slot.max_size==
                    slots.records[index].max_size&&
                odh::encode_session_bundle(reopened)==encoded,
                "source-circle DTO supports generic slot index, mask, scope and row metadata");
        };
        profile_roundtrip(13,2,0x8055,2,32);
        profile_roundtrip(8,1,255,1,16);
        profile_roundtrip(7,0,0x55,2,32);
        const auto mutate_route_payload=[&](std::vector<std::uint8_t> bytes,auto mutation){
            auto archive=odh::decode_session_archive(bytes);
            auto section=std::find_if(archive.sections.begin(),archive.sections.end(),[](const auto& value){
                return value.id==std::array<char,4>{{'N','P','C','A'}};});
            require(section!=archive.sections.end(),"NPCA section exists for nested route wire-boundary fixtures");
            const auto [route_begin,route_size_field]=npc_route_wire_offsets(section->data);
            const auto old_size=static_cast<std::uint32_t>(section->data[route_size_field])|
                (static_cast<std::uint32_t>(section->data[route_size_field+1])<<8)|
                (static_cast<std::uint32_t>(section->data[route_size_field+2])<<16)|
                (static_cast<std::uint32_t>(section->data[route_size_field+3])<<24);
            std::vector<std::uint8_t> route_data(section->data.begin()+static_cast<std::ptrdiff_t>(route_begin),
                section->data.begin()+static_cast<std::ptrdiff_t>(route_begin+old_size));
            mutation(route_data);
            section->data.erase(section->data.begin()+static_cast<std::ptrdiff_t>(route_begin),
                section->data.begin()+static_cast<std::ptrdiff_t>(route_begin+old_size));
            section->data.insert(section->data.begin()+static_cast<std::ptrdiff_t>(route_begin),
                route_data.begin(),route_data.end());
            const auto write_u32=[&](std::size_t field,std::uint32_t value){
                for(unsigned n=0;n<4;++n)section->data[field+n]=static_cast<std::uint8_t>(value>>(8*n));
            };
            const auto delta=static_cast<std::int64_t>(route_data.size())-old_size;
            write_u32(route_size_field,static_cast<std::uint32_t>(route_data.size()));
            const auto entry_size=static_cast<std::uint32_t>(section->data[4])|
                (static_cast<std::uint32_t>(section->data[5])<<8)|
                (static_cast<std::uint32_t>(section->data[6])<<16)|
                (static_cast<std::uint32_t>(section->data[7])<<24);
            write_u32(4,static_cast<std::uint32_t>(static_cast<std::int64_t>(entry_size)+delta));
            return odh::encode_session_archive(archive);
        };
        auto ranged=circle;
        set_source_range_route(ranged.npc_avatars.front().runtime);
        const auto range_bytes=odh::encode_session_bundle(ranged);
        const auto range_archive=odh::decode_session_archive(range_bytes);
        const auto range_section=std::find_if(range_archive.sections.begin(),range_archive.sections.end(),[](const auto& value){
            return value.id==std::array<char,4>{{'N','P','C','A'}};});
        require(range_section!=range_archive.sections.end()&&
            range_section->data[npc_route_wire_offsets(range_section->data).first]==7,
            "source range selects codec7 without widening codec5 circle");
        const auto range_reopened=odh::decode_session_bundle(range_bytes);
        const auto& saved_range=*range_reopened.npc_avatars.front().runtime.route->source_range;
        require(saved_range.slot_version==10&&saved_range.slot_index==3&&saved_range.slot.flags==0x8055&&
            saved_range.slot.proximity==std::optional<std::array<std::int32_t,3>>{{16,80,16}}&&
            saved_range.slot.unknown_i10==7&&saved_range.slot.gradient_bits==0x3E400000&&
            saved_range.target_radian_direction==0.31f&&saved_range.fallback_goals.size()==10&&
            saved_range.fallback_goals.front().position==
                ranged.npc_avatars.front().runtime.route->source_range->fallback_goals.front().position&&
            odh::encode_session_bundle(range_reopened)==range_bytes,
            "NPC range preserves full SLOT row, continuous transform and ranked fallback order byte-identically");
        auto range_oow=ranged;range_oow.world.entities.front().position.reset();
        const auto range_oow_bytes=odh::encode_session_bundle(range_oow);
        require(odh::encode_session_bundle(odh::decode_session_bundle(range_oow_bytes))==range_oow_bytes,
            "source range retains frozen geometry after its live target leaves the lot");
        auto range_self=ranged;auto& range_self_runtime=range_self.npc_avatars.front().runtime;
        auto& range_self_route=*range_self_runtime.route;
        range_self_route.target=range_self_route.actor;
        range_self_route.target_generation=range_self_route.actor_generation;
        range_self_route.target_anchor_radian_direction=range_self_route.source_range->target_radian_direction;
        range_self_runtime.vm.pending->context.stack_object=range_self_route.actor;
        range_self_runtime.vm.stack.back().context.stack_object=range_self_route.actor;
        const auto self_range_bytes=odh::encode_session_bundle(range_self);
        require(odh::encode_session_bundle(odh::decode_session_bundle(self_range_bytes))==self_range_bytes,
            "self range retains its original anchor while the current actor is elsewhere on its path");
        auto missing_anchor=range_self;missing_anchor.npc_avatars.front().runtime.route->target_anchor_radian_direction.reset();
        rejects([&]{(void)odh::encode_session_bundle(missing_anchor);},"self range requires its exact frozen angular anchor");
        auto changed_anchor=range_self;changed_anchor.npc_avatars.front().runtime.route->target_anchor_radian_direction=0.32f;
        rejects([&]{(void)odh::encode_session_bundle(changed_anchor);},"self range anchor must equal its frozen target angle bits");
        auto missing_pending=ranged;missing_pending.npc_avatars.front().runtime.vm.pending.reset();
        rejects([&]{(void)odh::encode_session_bundle(missing_pending);},"range marker requires a real pending Route45 before reading its operands");
        auto range_scope=ranged;auto& scope_runtime=range_scope.npc_avatars.front().runtime;
        scope_runtime.vm.pending->instruction.instruction.operands={0,0,0,0,1,0,0,0};
        scope_runtime.vm.stack.back().arguments={3};scope_runtime.route->call_failure_trees=false;
        const auto scope_bytes=odh::encode_session_bundle(range_scope);
        require(odh::encode_session_bundle(odh::decode_session_bundle(scope_bytes))==scope_bytes,
            "source range resolves parameter SLOT index and preserves NoFailureTrees policy");
        const auto reject_range=[&](auto mutation,const char* message){
            auto wrong=ranged;mutation(*wrong.npc_avatars.front().runtime.route);
            rejects([&]{(void)odh::encode_session_bundle(wrong);},message);
        };
        reject_range([](auto& r){r.source_range->selected.position.x+=1;r.path.back().x+=1;},
            "range selected position must belong to the frozen lattice");
        reject_range([](auto& r){r.source_range->fallback_goals.front().position.y+=1;},
            "range fallback must belong to the frozen lattice");
        reject_range([](auto& r){r.source_range->fallback_goals.front()=r.source_range->selected;},
            "range selected and fallback choices cannot duplicate positions");
        reject_range([](auto& r){r.source_range->fallback_goals[1]=r.source_range->fallback_goals.front();},
            "range fallback positions must be unique");
        reject_range([](auto& r){r.source_range->selected.radian_direction+=0.1f;
            r.arrival_radian_direction=r.source_range->selected.radian_direction;},
            "range continuous arrival is re-derived from the exact frozen geometry");
        reject_range([](auto& r){r.source_range->fallback_goals.front().entry_flags^=1;},
            "range fallback entry mask must match source geometry");
        reject_range([](auto& r){r.source_range->fallback_goals.front().facing^=1;},
            "range fallback facing must match source continuous geometry");
        reject_range([](auto& r){r.source_range->target_radian_direction=std::numeric_limits<float>::quiet_NaN();},
            "range target angle must be finite");
        reject_range([](auto& r){r.source_range->slot.flags=0;},"range requires a supported nonempty direction mask");
        reject_range([](auto& r){r.source_range->slot.proximity=std::array<std::int32_t,3>{16,16,16};},
            "range marker cannot claim the equal-radius circle profile");
        reject_range([](auto& r){r.source_range->slot_version=9;},"range requires an exact supported SLOT version");
        reject_range([](auto& r){r.source_range->slot.proximity=std::array<std::int32_t,3>{16,4096,16};
            r.source_range->slot.resolution=1;},"range rejects unbounded candidate geometry before enumeration");
        reject_range([](auto& r){r.source_range->slot_index=65536;},"range index must fit the source operand");
        reject_range([](auto& r){r.source_range->slot_index=4;},"range index must match pending Route45");
        reject_range([](auto& r){r.arrival_radian_direction.reset();},"range requires continuous arrival metadata");
        reject_range([](auto& r){r.source_range->fallback_goals.resize(4096,r.source_range->selected);},
            "range bounds total saved choices before encoding");
        reject_range([&](auto& r){r.source_circle=profile;},"range cannot combine with a circle marker");
        reject_range([](auto& r){r.look_towards.emplace();},"range cannot combine with a body-turn marker");
        rejects([&]{(void)odh::decode_session_bundle(mutate_route_payload(range_bytes,
            [](auto& data){data.pop_back();}));},"range decoder rejects truncated fallback goals");
        rejects([&]{(void)odh::decode_session_bundle(mutate_route_payload(range_bytes,
            [](auto& data){data.push_back(0);}));},"range decoder rejects trailing bytes");
        rejects([&]{(void)odh::decode_session_bundle(mutate_route_payload(range_bytes,[](auto& data){
            const auto count=data.size()-10*14-4;for(unsigned n=0;n<4;++n)data[count+n]=255;
        }));},"range decoder rejects oversized count before allocating its saved choices");
        rejects([&]{(void)odh::decode_session_bundle(mutate_route_payload(range_bytes,[](auto& data){
            std::copy(data.end()-14,data.end(),data.end()-28);
        }));},"range decoder rejects duplicate fallback identities from corrupted wire data");
        require(odh::encode_session_bundle(circle)==circle_bytes&&odh::encode_session_bundle(routed)==routed_bytes,
            "adding codec7 preserves absent-range legacy circle and ordinary route bytes");
        rejects([&]{(void)odh::decode_session_bundle(mutate_route_payload(circle_bytes,
            [](auto& data){data.pop_back();}));},
            "truncated source-circle tail is rejected by the bounded route decoder");
        rejects([&]{(void)odh::decode_session_bundle(mutate_route_payload(circle_bytes,
            [](auto& data){data.push_back(0);}));},
            "source-circle v5 rejects unclaimed trailing payload bytes");
        const auto reject_circle=[&](auto mutation,const char* message){
            auto wrong=circle;mutation(*wrong.npc_avatars.front().runtime.route);
            rejects([&]{odh::encode_session_bundle(wrong);},message);
        };
        reject_circle([](auto& value){value.source_circle->slot_version=9;},
            "source circle refuses a different SLOT version");
        reject_circle([](auto& value){value.source_circle->slot_index=3;},
            "source circle refuses a different type-3 index");
        reject_circle([](auto& value){value.source_circle->slot.flags=0x8000;},
            "source circle refuses unsupported SLOT mask bits");
        reject_circle([](auto& value){value.source_circle->slot.facing=-3;},
            "source circle refuses a different facing profile");
        reject_circle([](auto& value){value.source_circle->slot.gradient_bits.reset();},
            "source circle requires the complete v10 row identity");
        reject_circle([](auto& value){value.source_circle->target_radian_direction=
                std::numeric_limits<float>::quiet_NaN();},
            "source circle rejects non-finite frozen target angle");
        reject_circle([](auto& value){value.source_circle->selected.radian_direction=
                std::numeric_limits<float>::infinity();},
            "source circle rejects non-finite selected arrival angle");
        reject_circle([](auto& value){value.source_circle->fallback_goals.front().position=
                value.source_circle->selected.position;},
            "source circle rejects duplicate fallback positions");
        reject_circle([](auto& value){value.source_circle->fallback_goals.front().entry_flags=0;},
            "source circle rejects SLOT mask-excluded fallback goals");
        reject_circle([](auto& value){value.source_circle->slot_index=UINT32_MAX;},
            "oversized source circle index is rejected without index-sized allocation");
        reject_circle([](auto& value){value.source_circle->fallback_goals.resize(8);},
            "source circle bounds its full choice list to eight goals");
        reject_circle([](auto& value){value.source_circle->selected.position.x++;},
            "source circle selected target is tied to the live route path");
        reject_circle([](auto& value){value.entry_flags=0;},
            "source circle selected entry flags are tied to the VM route result");
        reject_circle([](auto& value){value.source_circle->selected.radian_direction+=0.01f;},
            "source circle selected continuous facing is bit-exact with arrival state");
        auto wrong_instruction=circle;
        wrong_instruction.npc_avatars.front().runtime.vm.pending.reset();
        rejects([&]{odh::encode_session_bundle(wrong_instruction);},
            "source circle without a pending VM request is rejected safely");
        wrong_instruction=circle;
        wrong_instruction.npc_avatars.front().runtime.vm.pending->instruction.instruction.operands[0]=3;
        rejects([&]{odh::encode_session_bundle(wrong_instruction);},
            "source circle payload cannot be attached to a different global slot operand");
        auto wrong_failure=circle;wrong_failure.npc_avatars.front().runtime.route->call_failure_trees=false;
        rejects([&]{odh::encode_session_bundle(wrong_failure);},
            "source circle preserves Route45 failure-tree policy");
        auto no_failure=circle;no_failure.npc_avatars.front().runtime.vm.pending->instruction.instruction.operands[4]=1;
        no_failure.npc_avatars.front().runtime.route->call_failure_trees=false;
        require(odh::encode_session_bundle(odh::decode_session_bundle(odh::encode_session_bundle(no_failure)))==
                odh::encode_session_bundle(no_failure),
            "source circle v5 supports and retains the authored NoFailureTrees bit");
    }
    for(const auto direction:{std::uint8_t{254},std::uint8_t{255},std::uint8_t{0},std::uint8_t{1},
        std::uint8_t{2},std::uint8_t{3},std::uint8_t{4},std::uint8_t{5},std::uint8_t{6},std::uint8_t{7}})
    for(const auto flags:{std::uint8_t{0},std::uint8_t{1},std::uint8_t{2},std::uint8_t{3},
        std::uint8_t{4},std::uint8_t{6},std::uint8_t{8},std::uint8_t{16},std::uint8_t{32},
        std::uint8_t{64},std::uint8_t{128},std::uint8_t{0xfd},std::uint8_t{0xff}}){
        auto relative=routed;auto& state=relative.npc_avatars.front().runtime;
        state.vm.pending->instruction.instruction={27,254,255,{5,129,254,direction,7,255,flags,173}};
        auto& progress=*state.route;progress.entry_flags=0;progress.face_anywhere=direction==255;
        progress.call_failure_trees=(flags&2U)==0;progress.facing=direction<=7?direction:0;
        progress.path.clear();for(std::int32_t x=12;x<=28;++x)progress.path.push_back({x,20});
        const auto encoded=odh::encode_session_bundle(relative);
        require(odh::encode_session_bundle(odh::decode_session_bundle(encoded))==encoded,
            "source OnTopOf directions preserve raw ignored counters, flags and tail in independent NPC runtime");
        auto wrong=relative;wrong.npc_avatars.front().runtime.route->call_failure_trees=!progress.call_failure_trees;
        rejects([&]{odh::encode_session_bundle(wrong);},"relative NoFailureTrees differs from saved route policy");
        wrong=relative;wrong.npc_avatars.front().runtime.route->face_anywhere=!progress.face_anywhere;
        rejects([&]{odh::encode_session_bundle(wrong);},"relative direction differs from saved face-anywhere policy");
        wrong=relative;wrong.npc_avatars.front().runtime.route->entry_flags=4;
        rejects([&]{odh::encode_session_bundle(wrong);},"exact relative route declares SLOT entry flags");
        for(const auto field:std::array<std::pair<std::size_t,std::uint8_t>,2>{{{2,255},{3,253}}}){
            wrong=relative;wrong.npc_avatars.front().runtime.vm.pending->instruction.instruction.operands[field.first]=field.second;
            rejects([&]{odh::encode_session_bundle(wrong);},"changed relative geometry or undefined direction accepted");
        }
        auto ignored=relative;
        ignored.npc_avatars.front().runtime.vm.pending->instruction.instruction.operands[6]^=4;
        ignored.npc_avatars.front().runtime.vm.pending->instruction.instruction.operands[7]^=1;
        const auto ignored_bytes=odh::encode_session_bundle(ignored);
        require(odh::encode_session_bundle(odh::decode_session_bundle(ignored_bytes))==ignored_bytes,
            "source-unused relative flag and tail changes preserve the same saved route semantics");
    }
    const auto reject_value=[&](auto mutation,const char* why){auto bad=bundle;mutation(bad);
        rejects([&]{odh::encode_session_bundle(bad);},why);};
    {
        auto direct=npc_avatar_fixture();auto& origin=direct.npc_avatars.front().origin;
        origin.neighbor_id=0;
        auto entity=std::find_if(direct.world.entities.begin(),direct.world.entities.end(),
            [&](const auto& e){return e.id==origin.id;});
        entity->person_data[31]=0;
        const auto bytes=odh::encode_session_bundle(direct);
        auto archive=odh::decode_session_archive(bytes);
        auto section=std::find_if(archive.sections.begin(),archive.sections.end(),[](const auto& s){
            return s.id==std::array<char,4>{{'N','P','C','A'}};});
        require(section!=archive.sections.end()&&section->data[0]==3&&
            odh::encode_session_bundle(odh::decode_session_bundle(bytes))==bytes,
            "resource-only NPC zero NeighborID uses explicit NPCA3 and roundtrips with exact resource/World identity");
        section->data[0]=1;
        rejects([&]{(void)odh::decode_session_bundle(odh::encode_session_archive(archive));},
            "legacy NPCA cannot silently reinterpret NeighborID zero as a resource-only visitor");
        auto mismatch=direct;mismatch.world.entities[static_cast<std::size_t>(entity-direct.world.entities.begin())].person_data[31]=99;
        rejects([&]{(void)odh::encode_session_bundle(mismatch);},"resource-only origin cannot conceal a different live NeighborID");
    }
    reject_value([](auto& b){b.npc_avatars.front().origin.id=1;},"NPC collides with household actor");
    reject_value([](auto& b){b.npc_avatars.front().origin.neighbor_id=41;},"NPC claims household NeighborID");
    reject_value([](auto& b){b.npc_avatars.front().origin.character_fingerprint.fill(0);},"NPC resource is unbound");
    reject_value([](auto& b){b.npc_avatars.front().origin.source="../WorldNPC.far";},"NPC resource traverses root");
    reject_value([](auto& b){b.npc_avatars.front().origin.asset=std::string("Pizza\0.iff",10);},"NPC archive entry contains NUL");
    reject_value([](auto& b){b.npc_avatars.front().primary_owner=7;},"NPC lacks exact primary bank");
    reject_value([](auto& b){b.npc_avatars.front().runtime.resource_owners.front().asset="Other.iff";},"NPC Main resource differs from origin");
    reject_value([](auto& b){b.npc_avatars.front().runtime.vm.steps++;},"NPC scheduler row is stale");
    reject_value([](auto& b){auto& vm=b.npc_avatars.front().runtime.vm;vm.stack.front().context.code_owner=3;
        vm.pending->context.code_owner=3;vm.pending->instruction.code_owner=3;},"NPC uses implicit unbound household-style private owner");
    reject_value([](auto& b){b.npc_avatars.front().runtime.controllers.interactions.actor_generation++;},"NPC controller generation is stale");
    reject_value([](auto& b){b.npc_avatars.front().selected_name=std::string(4097,'x');},"NPC body name exceeds limit");
    reject_value([](auto& b){b.npc_avatars.front().decay.last_two_minute_bucket=30;},"NPC decay state is invalid");
    reject_value([](auto& b){b.npc_avatars.push_back(b.npc_avatars.front());},"NPC origin is duplicated");
    reject_value([](auto& b){b.generic_execution=false;},"NPC accepted outside generic execution");
    reject_value([](auto& b){b.household_roster.clear();},"NPC accepted in legacy HOST profile");
    reject_value([](auto& b){b.scheduler.entities.pop_back();},"NPC omitted from scheduler");
    reject_value([](auto& b){b.world.entities.back().kind=odh::EntityKind::object;},"NPC replaced by ordinary object");
    reject_value([](auto& b){b.ui_state=odh::SessionUiState{3,b.npc_avatars.front().origin.generation,0xfc72c0c0U};},"NPC gains household UI selection authority");
    odh::SessionBundleLimits limits;limits.max_npc_avatars=0;
    rejects([&]{odh::encode_session_bundle(bundle,limits);},"NPC count limit ignored by writer");
    rejects([&]{odh::decode_session_bundle(bytes,std::nullopt,limits);},"NPC count limit ignored by reader");
    auto archive=npc_archive;
    const auto mutate_section=[&](auto mutation,const char* why){auto a=archive;
        for(auto& section:a.sections)if(section.id==std::array<char,4>{'N','P','C','A'})mutation(section);
        rejects([&]{odh::decode_session_bundle(odh::encode_session_archive(a));},why);};
    mutate_section([](auto& s){s.required=false;},"optional NPCA accepted");
    mutate_section([](auto& s){s.data[0]=4;},"unknown NPCA version accepted");
    mutate_section([](auto& s){s.data.push_back(0);},"NPCA trailing bytes accepted");
    mutate_section([](auto& s){s.id={'X','N','P','C'};},"unknown required NPC section ignored");
    mutate_section([](auto& s){s.data[2]=65;},"NPCA count beyond portable ceiling accepted");
}
void named_resource_owner_cases() {
    const auto original=generic_action_fixture();
    for(const bool semi:{false,true})for(const std::uint16_t id:{std::uint16_t{0},std::uint16_t{65535}}){
        auto bundle=original;auto owner=resource_owner(0x80000001U,123,id);
        owner.bank_kind=semi?odh::SessionCodeBankKind::complete_named_semiglobal:
            odh::SessionCodeBankKind::complete_named_private;
        if(semi)owner.linked_semiglobal=odh::SessionLinkedSemiglobal{"Linked","Global.far","Linked.iff"};
        const auto root=odh::session_resource_root_key(owner);
        require(root.id==owner.root_routine&&root.scope==(semi?odh::RoutineScope::named_semiglobal:odh::RoutineScope::named_private),
            "named owner pins physical resource, independent of numeric ID class");
        const auto tree=odh::Bhav{0x8002,{{0,254,255,{}}},{},odh::Bhav::Metadata{0,4,4,0}};
        owner.routine_fingerprint=odh::session_loaded_bank_fingerprint({{root,tree}});
        const odh::RoutineKey other{semi?odh::RoutineScope::named_private:odh::RoutineScope::named_semiglobal,root.id};
        require(owner.routine_fingerprint!=odh::session_loaded_bank_fingerprint({{other,tree}}),
            "named loaded-bank fingerprint includes physical scope, not only authored ID and body");
        bundle.runtime_entities.front().resource_owners.push_back(owner);++bundle.next_code_owner;
        const auto bytes=odh::encode_session_bundle(bundle);const auto decoded=odh::decode_session_bundle(bytes);
        require(decoded.runtime_entities.front().resource_owners.back().bank_kind==owner.bank_kind&&
            odh::session_resource_root_key(decoded.runtime_entities.front().resource_owners.back()).scope==root.scope&&
            odh::session_resource_root_key(decoded.runtime_entities.front().resource_owners.back()).id==root.id&&
            odh::encode_session_bundle(decoded)==bytes,"named bank manifest round-trips in existing record shape");
        if(semi) {
            auto physical=bundle;
            auto& link=*physical.runtime_entities.front().resource_owners.back().linked_semiglobal;
            link.name="CarGlobals";link.entry="CarGlobals.IFF";
            const auto uppercase=odh::encode_session_bundle(physical);
            const auto copy=odh::decode_session_bundle(uppercase);
            require(copy.runtime_entities.front().resource_owners.back().linked_semiglobal->entry=="CarGlobals.IFF"&&
                odh::encode_session_bundle(copy)==uppercase,"resource GLOB uppercase physical extension preserves exact bytes");
            for(const auto* entry:{"Other.IFF","carglobals.IFF","CarGlobals.IFX","CarGlobals.IFF/"}) {
                link.entry=entry;
                rejects([&]{odh::encode_session_bundle(physical);},"resource GLOB stem and flat physical entry remain exact");
            }
        }
        if(semi){bundle.runtime_entities.front().resource_owners.back().linked_semiglobal.reset();
            rejects([&]{odh::encode_session_bundle(bundle);},"named semiglobal root accepted without exact linked resource");}
    }
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
    malformed=archive;malformed.sections[section_index].data[0]=99;
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
    {
        auto inert=objects;auto& object=inert.object_threads.front();
        object.vm.stack.clear();object.vm.pending.reset();object.vm.suspended_pending.clear();
        object.vm.interaction_return.reset();object.vm.external_frame_return.reset();
        object.vm.immediate_continuations.clear();object.vm.immediate_return.reset();
        object.vm.status=odh::VmStatus::returned_false;
        for(auto& scheduled:inert.scheduler.entities)if(scheduled.id==object.id)scheduled.status=object.vm.status;
        object.sleep.job.reset();object.sleep.interrupted=false;object.sleep.notify_resume=false;
        auto& owner=object.resource_owners.front();owner.root_routine=0;
        owner.routine_fingerprint=odh::session_loaded_bank_fingerprint(odh::RoutineSet{});
        const auto reopened=odh::decode_session_bundle(odh::encode_session_bundle(inert));
        require(reopened.object_threads.front().resource_owners.front().root_routine==0&&
            reopened.object_threads.front().vm.stack.empty(),"authenticated empty native bank owns a persisted Thread without fabricated root");
        owner.routine_fingerprint.fill(0x53);
        rejects([&]{odh::encode_session_bundle(inert);},"root zero requires the exact empty-bank fingerprint");
        owner.routine_fingerprint=odh::session_loaded_bank_fingerprint(odh::RoutineSet{});
        owner.bank_kind=odh::SessionCodeBankKind::private_closure;
        rejects([&]{odh::encode_session_bundle(inert);},"an empty bank is not a fabricated zero-root closure");
    }
    {
        auto imported=objects;
        odh::World world;require(world.restore_snapshot(imported.world),"restore world for object capture");
        // The fixture includes legacy resident records. Bring those and their
        // queue proofs to the same native state on both sides of this comparison.
        odh::bind_legacy_session_entity_references(imported,world);
        auto& thread=imported.object_threads.front();
        require(!thread.vm.stack.empty(),"fixture has an imported object frame");
        thread.vm.action_tree_known=false;
        const auto legacy_bytes=odh::encode_session_bundle(imported);
        odh::resolve_object_thread_action_tree(thread);
        require(thread.vm.action_tree_known&&!thread.vm.stack.front().action_tree,
            "first native capture resolves an ordinary imported object Main");
        const auto first_bytes=odh::encode_session_bundle(imported);
        for(const auto* bytes:{&legacy_bytes,&first_bytes}){
            auto reopened=odh::decode_session_bundle(*bytes);
            odh::bind_legacy_session_entity_references(reopened,world);
            require(odh::encode_session_bundle(reopened)==first_bytes,
                "old object record upgrades and a fresh capture reopens byte-identically without a tick");
        }
        thread.vm.action_tree_known=false;
        thread.vm.stack.front().action_tree=true;
        odh::resolve_object_thread_action_tree(thread);
        require(thread.vm.action_tree_known&&thread.vm.stack.front().action_tree,
            "action pushed after raw import keeps its explicit ancestry on first capture");
        odh::resolve_object_thread_action_tree(thread);
        require(thread.vm.stack.front().action_tree,"already known object action ancestry survives capture");

        auto resident=generic_action_fixture();
        odh::World resident_world;require(resident_world.restore_snapshot(resident.world),"restore queued resident");
        auto& vm=resident.runtime_entities.front().vm;
        vm.action_tree_known=false;
        for(auto& frame:vm.stack)frame.action_tree=false;
        odh::bind_legacy_session_entity_references(resident,resident_world);
        require(vm.action_tree_known&&!vm.stack.front().action_tree&&vm.stack.back().action_tree,
            "resident ancestry still requires and follows the active interaction ticket");
    }
    {
        auto headlined=objects;
        odh::BalloonHeadlineState headline;
        headline.operand={0,0,1,7,4,0,2,1};headline.target=objects.object_threads[0].id;
        headline.target_generation=objects.object_threads[0].generation;
        headline.icon_target=objects.world.entities[0].id;
        headline.icon_generation=objects.world.entities[0].generation;
        headline.index=1;headline.duration=4;headline.anim=17;
        headlined.headlines.push_back(headline);
        const auto encoded=odh::encode_session_bundle(headlined);
        const auto restored=odh::decode_session_bundle(encoded);
        require(restored.headlines.size()==1 && restored.headlines[0].anim==17 &&
            restored.headlines[0].duration==4 && odh::encode_session_bundle(restored)==encoded,
            "headline state survives bundle reopening without a graphics backend");
        auto archive=odh::decode_session_archive(encoded);
        const auto section=std::find_if(archive.sections.begin(),archive.sections.end(),[](const auto& s){
            return s.id==std::array<char,4>{{'H','L','D','N'}};});
        require(section!=archive.sections.end()&&section->required,"headline state requires an understood reader");
        section->required=false;
        rejects([&]{odh::decode_session_bundle(odh::encode_session_archive(archive));},"optional headline continuation accepted");
        headlined.headlines[0].target_generation++;
        rejects([&]{odh::encode_session_bundle(headlined);},"headline reused target identity accepted");
        auto scheduled=objects;
        scheduled.object_threads[0].sleep.job->next_evaluation=1;
        const auto scheduled_bytes=odh::encode_session_bundle(scheduled);
        const auto reopened=odh::decode_session_bundle(scheduled_bytes);
        require(reopened.object_threads[0].sleep.job->next_evaluation==1 &&
            odh::encode_session_bundle(reopened)==scheduled_bytes,
            "object wait retains the already scheduled next-frame evaluation even without a live headline");
        scheduled.object_threads[0].sleep.job->next_evaluation=0;
        rejects([&]{odh::encode_session_bundle(scheduled);},"expired object wait override accepted");
        scheduled.object_threads[0].sleep.job->next_evaluation=scheduled.object_threads[0].sleep.job->deadline;
        rejects([&]{odh::encode_session_bundle(scheduled);},"non-early object wait override accepted");
    }
    auto lot_objects=objects;
    lot_objects.object_threads[0].resource_owners[0].bank_kind=odh::SessionCodeBankKind::private_and_semiglobal;
    lot_objects.object_threads[0].resource_owners[0].linked_semiglobal=odh::SessionLinkedSemiglobal{
        "SyntheticGlobal","GameData/Global/Global.far","SyntheticGlobal.iff"};
    odh::World lot_world;
    require(lot_world.restore_snapshot(lot_objects.world),"restore synthetic lot before adding saved object threads");
    const auto prototype=lot_objects.object_threads[0];
    for(std::int16_t id=208;id<387;++id) {
        require(lot_world.create_runtime_object(id,prototype.guid,{},{}),"add independent instance sharing object code");
        auto instance=prototype;instance.id=id;instance.generation=lot_world.find(id)->generation;
        for(auto& frame:instance.vm.stack){frame.context={id,id,id,frame.context.code_owner};require(lot_world.bind_context_live(frame.context),"clone instance frame binds its own generation");}
        instance.vm.pending->context={id,id,id,instance.vm.pending->context.code_owner};
        require(lot_world.bind_context_live(instance.vm.pending->context),"clone instance pending binds its own generation");
        instance.sleep.actor->id=id;instance.sleep.actor->generation=instance.generation;
        lot_objects.scheduler.entities.push_back({id,instance.vm.run_generation,instance.vm.steps,
            instance.vm.status,false,false,false});
        auto origin=lot_objects.created_objects[0];origin.id=id;origin.generation=instance.generation;
        lot_objects.created_objects.push_back(origin);
        lot_objects.object_threads.push_back(std::move(instance));
    }
    lot_objects.world=lot_world.snapshot();lot_objects.scheduler.entity_limit=4096;
    const auto lot_bytes=odh::encode_session_bundle(lot_objects);
    const auto reopened_lot=odh::decode_session_bundle(lot_bytes);
    require(reopened_lot.object_threads.size()==180&&
        reopened_lot.object_threads.back().id==386&&
        reopened_lot.object_threads.back().primary_owner==prototype.primary_owner&&
        reopened_lot.object_threads.back().resource_owners[0].bank_kind==
            odh::SessionCodeBankKind::private_and_semiglobal&&
        odh::encode_session_bundle(reopened_lot)==lot_bytes,
        "180 independent object continuations with a shared private/semiglobal owner round-trip exactly");
    odh::SessionBundleLimits restricted_lot;restricted_lot.max_object_threads=64;
    rejects([&]{odh::decode_session_bundle(lot_bytes,{},restricted_lot);},
        "explicit small object-thread budget must still reject a larger lot");
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
    for(auto& frame:twin.vm.stack){frame.context={208,208,208,frame.context.code_owner};require(twin_world.bind_context_live(frame.context),"bind new twin exact caller/callee/stack generations");}
    twin.vm.pending->context={208,208,208,twin.vm.pending->context.code_owner};
    require(twin_world.bind_context_live(twin.vm.pending->context),"bind new twin pending exact generations");
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

void object_history_cases() {
    using namespace odh;
    const std::array<char,4> id{{'O','H','S','T'}};
    auto source=fixture();source.generic_execution=true;source.world.generation_sequence=6;
    source.removed_original_objects={{3,3,99}};
    SessionObjectHistory history;history.complete=true;
    history.retired_created_objects={{4,4,100,"objects.far","objects.iff",128}};
    history.reserved_entity_ids={3,4,8};source.object_history=history;
    const auto bytes=encode_session_bundle(source);
    const auto decoded=decode_session_bundle(bytes);
    require(decoded.object_history && decoded.object_history->complete &&
        decoded.object_history->retired_created_objects.size()==1 &&
        decoded.object_history->retired_created_objects[0].source=="objects.far" &&
        decoded.object_history->reserved_entity_ids==history.reserved_entity_ids &&
        encode_session_bundle(decoded)==bytes,"complete object history byte-stable roundtrip");
    {
        auto master=source;master.object_history->retired_created_objects.front().master_guid=0xe1234567U;
        const auto encoded=encode_session_bundle(master);const auto copy=decode_session_bundle(encoded);
        require(!copy.object_history->reusable_entity_ids&&copy.object_history->complete&&
            copy.object_history->retired_created_objects.front().master_guid==0xe1234567U&&
            encode_session_bundle(copy)==encoded,"OHST4 preserves legacy reservation policy explicitly");
        auto malformed=decode_session_archive(encoded);
        for(auto& section:malformed.sections)if(section.id==id)section.data[3]=2;
        rejects([&]{decode_session_bundle(encode_session_archive(malformed));},"nonboolean OHST4 reuse marker rejected");
        master.object_history->retired_created_objects.front().master_guid.reset();
        require(encode_session_bundle(master)==bytes,"absent retired master preserves OHST1 exact bytes");
    }
    auto incomplete=source;incomplete.object_history->complete=false;
    const auto incomplete_bytes=encode_session_bundle(incomplete);
    const auto incomplete_decoded=decode_session_bundle(incomplete_bytes);
    require(incomplete_decoded.object_history && !incomplete_decoded.object_history->complete &&
        encode_session_bundle(incomplete_decoded)==incomplete_bytes,"incomplete object history never promoted");
    auto legacy=source;legacy.object_history.reset();
    require(!decode_session_bundle(encode_session_bundle(legacy)).object_history,"old GEXP archive has unknown history");
    require(!decode_session_bundle(encode_session_bundle(fixture())).object_history,"legacy non-GEXP has no invented history");
    const auto reject_value=[&](auto mutate,const char* text){auto bad=source;mutate(bad);rejects([&]{encode_session_bundle(bad);},text);};
    reject_value([](auto& b){b.generic_execution=false;},"object history without GEXP accepted");
    reject_value([](auto& b){b.object_history->reserved_entity_ids={0,3,4};},"zero reserved ID accepted");
    reject_value([](auto& b){b.object_history->reserved_entity_ids={3,3,4};},"duplicate reserved ID accepted");
    reject_value([](auto& b){b.object_history->reserved_entity_ids={4,3};},"reordered reservations accepted");
    reject_value([](auto& b){b.object_history->reserved_entity_ids={1,3,4};},"live reserved ID accepted");
    reject_value([](auto& b){b.object_history->reserved_entity_ids={4};},"unreserved original removal accepted");
    reject_value([](auto& b){b.object_history->reserved_entity_ids={3};},"unreserved retired creation accepted");
    reject_value([](auto& b){b.object_history->reserved_entity_ids.assign(4097,9);},"reservation count overflow accepted");
    reject_value([](auto& b){b.object_history->retired_created_objects.push_back(b.object_history->retired_created_objects.front());},"duplicate retired ID accepted");
    reject_value([](auto& b){b.object_history->retired_created_objects.front().generation=1;},"retired generation aliases live World");
    reject_value([](auto& b){b.object_history->retired_created_objects.front().generation=3;},"retired generation aliases removed original");
    reject_value([](auto& b){b.object_history->retired_created_objects.front().generation=7;},"retired future generation accepted");
    reject_value([](auto& b){b.object_history->retired_created_objects.front().guid=0;},"zero retired GUID accepted");
    reject_value([](auto& b){b.object_history->retired_created_objects.front().definition_chunk_id=0;},"zero retired definition accepted");
    reject_value([](auto& b){b.object_history->retired_created_objects.front().source.clear();},"empty retired source accepted");
    reject_value([](auto& b){b.object_history->retired_created_objects.front().asset.assign(4097,'x');},"oversized retired asset accepted");
    reject_value([](auto& b){b.object_history->retired_created_objects.front().source=std::string("bad\0source",10);},"NUL retired source accepted");
    reject_value([](auto& b){b.object_history->retired_created_objects.front().id=3;},"retired ID aliases removed original");
    auto two=source;two.object_history->retired_created_objects.push_back({5,4,101,"objects.far","objects.iff",129});
    two.object_history->reserved_entity_ids={3,4,5,8};
    rejects([&]{encode_session_bundle(two);},"duplicate retired generation accepted");
    const auto mutate_section=[&](auto mutate){auto archive=decode_session_archive(bytes);
        for(auto& section:archive.sections)if(section.id==id)mutate(section);
        return encode_session_archive(archive);};
    const auto reject_wire=[&](auto mutate,const char* text){rejects([&]{decode_session_bundle(mutate_section(mutate));},text);};
    reject_wire([](auto& s){s.required=false;},"optional OHST accepted");
    reject_wire([](auto& s){s.data[0]=99;},"unknown OHST version accepted");
    reject_wire([](auto& s){s.data[2]=2;},"nonboolean OHST completeness accepted");
    reject_wire([](auto& s){s.data.push_back(0);},"OHST trailing bytes accepted");
    reject_wire([](auto& s){s.data.pop_back();},"truncated OHST accepted");
    reject_wire([](auto& s){s.data[3]=1;s.data[4]=0x10;},"oversized retired count accepted");
    reject_wire([](auto& s){s.data[7]=0;s.data[8]=0;},"wire zero retired ID accepted");
    reject_wire([](auto& s){s.data[7]=1;},"wire live retired ID accepted");
    reject_wire([](auto& s){s.data[21]=1;s.data[22]=0x10;},"oversized wire history text accepted");
    reject_wire([](auto& s){s.data[23]=0;},"wire NUL history text accepted");
    const auto reservation_count=std::size_t{7+2+8+4+2+11+2+11+2};
    reject_wire([&](auto& s){s.data[reservation_count]=1;s.data[reservation_count+1]=0x10;},"oversized wire reservation count accepted");
    reject_wire([&](auto& s){s.data[reservation_count+6]=3;},"wire duplicate reservations accepted");
    reject_wire([&](auto& s){s.data[reservation_count+6]=5;},"wire missing retired reservation accepted");
    auto archive=decode_session_archive(bytes);
    archive.sections.erase(std::remove_if(archive.sections.begin(),archive.sections.end(),[](const auto& s){
        return s.id==std::array<char,4>{{'G','E','X','P'}};}),archive.sections.end());
    rejects([&]{decode_session_bundle(encode_session_archive(archive));},"wire OHST without GEXP accepted");
    archive=decode_session_archive(bytes);
    const auto section=*std::find_if(archive.sections.begin(),archive.sections.end(),[&](const auto& s){return s.id==id;});
    require(section.required,"OHST must be encoded required");archive.sections.push_back(section);
    rejects([&]{decode_session_bundle(encode_session_archive(archive));},"duplicate OHST section accepted");
}

void object_trade_ledger_cases() {
    auto bundle=fixture();const auto legacy=odh::encode_session_bundle(bundle);
    require(odh::encode_session_bundle(odh::decode_session_bundle(legacy))==legacy,"no-ledger archive keeps exact legacy bytes");
    bundle.generic_execution=true;bundle.object_history=odh::SessionObjectHistory{true,{},{}};
    odh::World world;require(world.restore_snapshot(bundle.world)&&world.create(3,odh::EntityKind::object)&&world.define_type(3,0x1234),"trade ledger object");
    bundle.world=world.snapshot();
    odh::SessionCreatedObject origin{3,world.find(3)->generation,0x1234,"GameData/Objects/Objects.far","chair.iff",128};
    bundle.created_objects.push_back(origin);
    bundle.object_trade=odh::SessionObjectTradeLedger{{{origin,101,std::uint16_t{80}}}};
    const auto bytes=odh::encode_session_bundle(bundle);const auto opened=odh::decode_session_bundle(bytes);
    require(opened.object_trade&&opened.object_trade->objects.front().initial_price==101&&
        opened.object_trade->objects.front().wear==80&&odh::encode_session_bundle(opened)==bytes,"required trade ledger exact signed price/Wear roundtrip");
    bundle.object_trade->objects.front().wear.reset();
    const auto absent=odh::decode_session_bundle(odh::encode_session_bundle(bundle));
    require(!absent.object_trade->objects.front().wear,"known absent ObjectState is distinct from Wear0");
    auto unsupported_master=bundle;unsupported_master.object_trade->objects.front().object.master_guid=0xe1234567U;
    rejects([&]{odh::encode_session_bundle(unsupported_master);},"OTRD1 cannot silently discard embedded construction master");
    auto bad=bundle;bad.object_trade->objects.front().object.generation++;
    rejects([&]{odh::encode_session_bundle(bad);},"trade ledger rejects generation reuse");
    bad=bundle;bad.object_trade->objects.front().object.source="different.far";
    rejects([&]{odh::encode_session_bundle(bad);},"trade ledger rejects different creation provenance");
    bad=bundle;bad.object_trade->objects.push_back(bad.object_trade->objects.front());
    rejects([&]{odh::encode_session_bundle(bad);},"trade ledger rejects duplicate identity");
    bad=bundle;bad.object_history.reset();
    rejects([&]{odh::encode_session_bundle(bad);},"trade ledger cannot discard required history");
    auto archive=odh::decode_session_archive(bytes);
    auto ledger=std::find_if(archive.sections.begin(),archive.sections.end(),[](const auto& e){return e.id==std::array<char,4>{{'O','T','R','D'}};});
    require(ledger!=archive.sections.end()&&ledger->required,"trade section mandatory");
    const auto index=static_cast<std::size_t>(ledger-archive.sections.begin());
    auto stripped=archive;stripped.sections.erase(stripped.sections.begin()+static_cast<std::ptrdiff_t>(index));
    rejects([&]{odh::decode_session_bundle(odh::encode_session_archive(stripped));},"HOST5 rejects stripped trade ledger");
    stripped=archive;stripped.sections[index].required=false;
    rejects([&]{odh::decode_session_bundle(odh::encode_session_archive(stripped));},"trade ledger cannot become optional");
    stripped=archive;stripped.sections[index].data[0]=3;
    rejects([&]{odh::decode_session_bundle(odh::encode_session_archive(stripped));},"unknown trade version rejected");
    {
        auto grouped=bundle;odh::World extra;require(extra.restore_snapshot(grouped.world)&&extra.create(4,odh::EntityKind::object)&&
            extra.define_type(4,0x4444,0x9000)&&extra.create(5,odh::EntityKind::object)&&extra.define_type(5,0x5555)&&
            extra.create(6,odh::EntityKind::object)&&extra.define_type(6,0x6666,0xa000),"OTRD2 original heterogeneous group plus distinct constructed group");
        grouped.world=extra.snapshot();
        odh::SessionCreatedObject other{4,extra.find(4)->generation,0x4444,"Parts.far","part.iff",129,0x9000};
        odh::SessionCreatedObject first{5,extra.find(5)->generation,0x5555,"Parts.far","first.iff",130};
        // The heterogeneous cohort is original-import state: do not also
        // claim its third member belongs to a separate constructed OGRP group.
        // A different created subpart keeps its own matching physical/value
        // authority so both contracts are exercised without contradictory groups.
        grouped.created_objects.clear();
        odh::SessionCreatedObject constructed{6,extra.find(6)->generation,0x6666,"Parts.far","created.iff",131,0xa000};
        grouped.created_objects.push_back(constructed);
        grouped.object_groups=odh::ObjectGroupSnapshot{{{0xa000,true,constructed.id,{{constructed.id,constructed.generation,0,0,0}}}}};
        odh::SessionObjectValueMember a;a.object=first;a.lead_object=0;a.wear_known=true;a.wear=0;a.disabled=0;
        odh::SessionObjectValueMember b;b.object=origin;b.offset_x=16;b.lead_object=1;b.wear_known=true;b.wear=100;b.disabled=3;
        odh::SessionObjectValueMember c;c.object=other;c.offset_x=32;c.lead_object=0;
        odh::SessionObjectValueGroup group;group.group_id=7;group.revision=9;group.multitile=true;group.members={a,b,c};
        group.base=odh::ObjectTradeIdentity{origin.id,origin.generation};group.initial_price=123456;
        std::array<std::uint8_t,32> digest{};digest.fill(0x7a);
        group.price_origin={odh::SessionObjectPriceSource::fresh_import_objd,0x9000,"Masters.far","master.iff",150,digest,{}};
        odh::SessionObjectValueMember created_member;created_member.object=constructed;created_member.lead_object=0;
        created_member.wear_known=true;created_member.wear=0;created_member.disabled=0;
        odh::SessionObjectValueGroup created_group;created_group.group_id=8;created_group.revision=9;created_group.multitile=true;
        created_group.members={created_member};created_group.base=odh::ObjectTradeIdentity{constructed.id,constructed.generation};created_group.initial_price=77;
        created_group.price_origin={odh::SessionObjectPriceSource::constructed_catalog,0xa000,"Masters.far","created-master.iff",151,{},{}};
        odh::SessionObjectTradeLedger values;values.version=2;values.revision=9;values.groups={group,created_group};grouped.object_trade=values;
        const auto encoded=odh::encode_session_bundle(grouped);const auto reopened=odh::decode_session_bundle(encoded);
        require(odh::encode_session_bundle(reopened)==encoded&&reopened.object_trade->version==2&&reopened.object_trade->objects.empty()&&
            reopened.object_trade->groups[0].members[0].object.id==5&&reopened.object_trade->groups[0].members[1].object.id==3&&
            reopened.object_trade->groups[0].members[2].object.master_guid==0x9000&&reopened.object_trade->groups[0].base->id==3&&
            reopened.object_trade->groups[0].price_origin.objd_sha256==digest&&reopened.object_trade->groups[0].initial_price==123456&&
            reopened.object_groups&&reopened.object_groups->groups.front().definition_guid==0xa000&&
            reopened.object_groups->groups.front().members.front().id==constructed.id,
            "OTRD2 exact source member order, individual masters, actual base, shared price and OBJD provenance roundtrip");
        {
            auto live=grouped;auto& price_group=live.object_trade->groups.front();
            price_group.members.push_back(live.object_trade->groups.back().members.front());
            live.object_trade->groups.pop_back();
            odh::ObjectGroup association;association.kind=odh::ObjectGroupKind::live_association;
            association.multitile=price_group.multitile;association.base_id=price_group.base->id;
            for(const auto& part:price_group.members){const auto& o=part.object;
                odh::ObjectGroupMember body{o.id,o.generation,part.offset_x,part.offset_y,part.offset_level};
                body.origin=odh::ObjectGroupMemberOrigin{o.id==6?odh::ObjectGroupMemberSource::native_created:odh::ObjectGroupMemberSource::original_import,
                    o.guid,o.master_guid,o.source,o.asset,o.definition_chunk_id};association.members.push_back(body);}
            live.object_groups=odh::ObjectGroupSnapshot{{association}};
            const auto live_bytes=odh::encode_session_bundle(live);const auto live_copy=odh::decode_session_bundle(live_bytes);
            require(odh::encode_session_bundle(live_copy)==live_bytes&&live_copy.object_groups->groups.front().kind==odh::ObjectGroupKind::live_association&&
                live_copy.object_groups->groups.front().members.size()==4&&live_copy.object_groups->groups.front().members.back().origin->source_kind==odh::ObjectGroupMemberSource::native_created,
                "live association preserves ordered original/native provenance, heterogeneous masters and price in full bundle");
            auto bad=live;bad.object_groups->groups.front().members.back().origin->source_kind=odh::ObjectGroupMemberSource::original_import;
            rejects([&]{odh::encode_session_bundle(bad);},"live association cannot relabel created member as original import");
            bad=live;bad.object_groups->groups.front().members.front().origin->source_kind=odh::ObjectGroupMemberSource::native_created;
            rejects([&]{odh::encode_session_bundle(bad);},"live association cannot relabel original member as native creation");
            bad=live;bad.object_groups->groups.front().members.front().origin->asset="forged.iff";
            rejects([&]{odh::encode_session_bundle(bad);},"live association individual resource differs from OTRD provenance");
            bad=live;bad.object_groups->groups.front().members.front().offset_y=17;
            rejects([&]{odh::encode_session_bundle(bad);},"live association physical/value offsets must agree");
            bad=live;bad.object_trade.reset();rejects([&]{odh::encode_session_bundle(bad);},"live association cannot omit v2 value authority");
            bad=live;bad.object_trade->groups.front().base.reset();
            rejects([&]{odh::encode_session_bundle(bad);},"live association cannot discard its exact base authority");
        }
        require(reopened.object_trade->groups[0].members[0].disabled==0&&reopened.object_trade->groups[0].members[1].disabled==3&&
            !reopened.object_trade->groups[0].members[2].disabled,"OTRD2 preserves runtime Disabled zero/nonzero/unknown independently per source member");
        auto unknown=grouped;unknown.object_trade->groups[0].initial_price.reset();unknown.object_trade->groups[0].price_origin={};
        unknown.object_trade->groups[0].base.reset();const auto unknown_read=odh::decode_session_bundle(odh::encode_session_bundle(unknown));
        require(!unknown_read.object_trade->groups[0].base&&!unknown_read.object_trade->groups[0].initial_price&&
            !unknown_read.object_trade->groups[0].members[2].wear_known&&!unknown_read.object_trade->groups[0].members[2].wear,
            "OTRD2 unknown price/BaseObject/Wear survive without becoming zero or absent ObjectState");
        auto missing_authority=grouped;missing_authority.object_groups.reset();
        rejects([&]{odh::encode_session_bundle(missing_authority);},"OTRD2 cannot replace a created subpart's required dynamic group authority");
        auto invalid=grouped;invalid.object_trade->objects.push_back({origin,101,{}});
        rejects([&]{odh::encode_session_bundle(invalid);},"OTRD2 cannot coexist with legacy singleton price authority");
        invalid=grouped;invalid.object_trade->groups[0].members[0].object.generation++;
        rejects([&]{odh::encode_session_bundle(invalid);},"OTRD2 refuses numeric identity reuse");
        invalid=grouped;invalid.object_trade->groups[1].members[0].object.asset="different.iff";
        rejects([&]{odh::encode_session_bundle(invalid);},"OTRD2 authenticates individual created provenance");
        invalid=grouped;invalid.object_trade->groups[0].base=odh::ObjectTradeIdentity{first.id,first.generation};
        rejects([&]{odh::encode_session_bundle(invalid);},"OTRD2 cannot replace real lead BaseObject with first or minimum ID");
        invalid=grouped;invalid.object_trade->groups[0].price_origin.objd_sha256.reset();
        rejects([&]{odh::encode_session_bundle(invalid);},"fresh import valuation requires exact OBJD payload digest");
        invalid=grouped;invalid.object_trade->groups[0].members[2].wear=12;
        rejects([&]{odh::encode_session_bundle(invalid);},"unknown per-member Wear cannot carry guessed values");
        invalid=grouped;invalid.object_trade->groups.push_back(group);invalid.object_trade->groups.back().group_id=8;
        rejects([&]{odh::encode_session_bundle(invalid);},"one physical member cannot acquire competing group prices");
        auto malformed=odh::decode_session_archive(encoded);const auto section=std::find_if(malformed.sections.begin(),malformed.sections.end(),[](const auto& s){return s.id==std::array<char,4>{{'O','T','R','D'}};});
        require(section!=malformed.sections.end(),"OTRD2 required section present");section->data[28]=2;
        rejects([&]{odh::decode_session_bundle(odh::encode_session_archive(malformed));},"OTRD2 rejects malformed booleans rather than normalizing");
        auto disabled_flag=odh::decode_session_archive(encoded);
        const auto disabled_section=std::find_if(disabled_flag.sections.begin(),disabled_flag.sections.end(),[](const auto& section){return section.id==std::array<char,4>{{'O','T','R','D'}};});
        const auto disabled_offset=std::size_t{31}+2+8+4+2+first.source.size()+2+first.asset.size()+2+1+6+1+2+1+1+2;
        require(disabled_section->data.at(disabled_offset)==1,"first member Disabled has an explicit presence marker");
        disabled_section->data[disabled_offset]=2;
        rejects([&]{odh::decode_session_bundle(odh::encode_session_archive(disabled_flag));},"OTRD2 refuses malformed Disabled presence marker");
        auto empty=grouped;empty.object_trade->groups.clear();const auto empty_bytes=odh::encode_session_bundle(empty);
        require(odh::encode_session_bundle(odh::decode_session_bundle(empty_bytes))==empty_bytes&&
            odh::decode_session_bundle(empty_bytes).object_trade->version==2,"empty OTRD2 keeps its explicit version/revision across reopen");
        require(odh::encode_session_bundle(opened)==bytes,"new group codec preserves exact old OTRD1 bytes");
    }
    stripped=archive;
    for(auto& section:stripped.sections)if(section.id==std::array<char,4>{{'H','O','S','T'}}){section.data[8]=3;section.data.erase(section.data.begin()+10);}
    rejects([&]{odh::decode_session_bundle(odh::encode_session_archive(stripped));},"legacy HOST cannot accept detached trade authority");
}

void runtime_globals_codec_cases() {
    auto bundle=fixture();const auto legacy=odh::encode_session_bundle(bundle);
    require(odh::encode_session_bundle(odh::decode_session_bundle(legacy))==legacy,
        "absent runtime globals keep exact legacy archive bytes");
    bundle.host.global_values.clear();
    odh::RuntimeGlobals globals(std::vector<std::int16_t>(64),odh::Ts1GlobalActivation::saved_new,7);
    require(globals.write(0,-123)&&globals.write(3,-42)&&globals.write(9,-11)&&
        globals.write(10,99)&&globals.write(37,-32768),"source raw shadows accept values independent from actual clock/family/house");
    bundle.runtime_globals=globals.snapshot();
    const auto encoded=odh::encode_session_bundle(bundle);const auto opened=odh::decode_session_bundle(encoded);
    require(opened.runtime_globals&&opened.runtime_globals->words==bundle.runtime_globals->words&&
        opened.runtime_globals->selected_sim_is_runtime_id&&odh::encode_session_bundle(opened)==encoded,
        "required runtime globals reopen exact full bank and runtime selection authority");
    auto sparse=odh::RuntimeGlobals::from_legacy({}).snapshot();
    bundle.runtime_globals=sparse;
    const auto unknown=odh::decode_session_bundle(odh::encode_session_bundle(bundle));
    require(!unknown.runtime_globals->words[3]&&!unknown.runtime_globals->words[37]&&
        !unknown.runtime_globals->selected_sim_is_runtime_id,"sparse legacy unknowns stay distinct from source zero");
    bundle.runtime_globals=globals.snapshot();
    auto bad=bundle;bad.runtime_globals->words.pop_back();
    rejects([&]{odh::encode_session_bundle(bad);},"runtime globals must have exactly source38 extent");
    bad=bundle;bad.runtime_globals->words[3].reset();
    rejects([&]{odh::encode_session_bundle(bad);},"runtime selection marker cannot certify absent selected value");
    bad=bundle;bad.host.global_values.push_back({17,4});
    rejects([&]{odh::encode_session_bundle(bad);},"runtime globals cannot have duplicate legacy HOST authority");
    auto archive=odh::decode_session_archive(encoded);
    const auto found=std::find_if(archive.sections.begin(),archive.sections.end(),[](const auto& section){return section.id==std::array<char,4>{{'R','G','L','B'}};});
    require(found!=archive.sections.end()&&found->required,"runtime globals section is required");
    const auto index=static_cast<std::size_t>(found-archive.sections.begin());
    auto wire=archive;wire.sections.erase(wire.sections.begin()+static_cast<std::ptrdiff_t>(index));
    rejects([&]{odh::decode_session_bundle(odh::encode_session_archive(wire));},"HOST6 rejects stripped runtime globals");
    wire=archive;wire.sections[index].required=false;
    rejects([&]{odh::decode_session_bundle(odh::encode_session_archive(wire));},"runtime globals cannot be made optional");
    wire=archive;wire.sections[index].data[0]=2;
    rejects([&]{odh::decode_session_bundle(odh::encode_session_archive(wire));},"unknown runtime globals version rejected");
    wire=archive;wire.sections[index].data[2]=2;
    rejects([&]{odh::decode_session_bundle(odh::encode_session_archive(wire));},"invalid runtime selection marker rejected");
    wire=archive;wire.sections[index].data[7]|=64;
    rejects([&]{odh::decode_session_bundle(odh::encode_session_archive(wire));},"runtime bitmap cannot address source index38");
    wire=archive;wire.sections[index].data.push_back(0);
    rejects([&]{odh::decode_session_bundle(odh::encode_session_archive(wire));},"runtime globals trailing bytes rejected");
    wire=archive;wire.sections[index].data.pop_back();
    rejects([&]{odh::decode_session_bundle(odh::encode_session_archive(wire));},"truncated runtime bank rejected");
    wire=archive;
    for(auto& section:wire.sections)if(section.id==std::array<char,4>{{'H','O','S','T'}}){
        const auto old_version=bundle.host.family_budget?(bundle.household_roster.empty()?3:4):(bundle.household_roster.empty()?1:2);
        section.data[8]=static_cast<std::uint8_t>(old_version);section.data.erase(section.data.begin()+10);
    }
    rejects([&]{odh::decode_session_bundle(odh::encode_session_archive(wire));},"legacy HOST cannot silently carry unannounced runtime globals");
    bundle.generic_execution=true;bundle.object_history=odh::SessionObjectHistory{true,{},{}};
    bundle.object_trade=odh::SessionObjectTradeLedger{};
    const auto together=odh::encode_session_bundle(bundle);const auto both=odh::decode_session_bundle(together);
    require(both.runtime_globals&&both.object_trade&&odh::encode_session_bundle(both)==together,
        "HOST6 preserves independent runtime globals and trade capabilities together");
    wire=odh::decode_session_archive(together);
    wire.sections.erase(std::remove_if(wire.sections.begin(),wire.sections.end(),[](const auto& section){return section.id==std::array<char,4>{{'O','T','R','D'}};}),wire.sections.end());
    rejects([&]{odh::decode_session_bundle(odh::encode_session_archive(wire));},"HOST6 trade feature marker rejects stripped ledger");
}

void advertisement_thread_archive_cases(){
    auto actor=generic_action_fixture();
    actor.runtime_entities.front().vm.advertisement_changes=std::map<std::uint32_t,std::int16_t>{{0x10007,0},{0x2ffff,-13}};
    const auto actor_bytes=odh::encode_session_bundle(actor);
    const auto loaded_actor=odh::decode_session_bundle(actor_bytes);
    require(loaded_actor.runtime_entities.front().vm.advertisement_changes==actor.runtime_entities.front().vm.advertisement_changes&&
        odh::encode_session_bundle(loaded_actor)==actor_bytes,"resident thread advertisements preserve explicit zero and signed-index key");
    auto object=generic_object_fixture();object.object_threads.front().vm.advertisement_changes.emplace();
    const auto object_bytes=odh::encode_session_bundle(object);const auto loaded_object=odh::decode_session_bundle(object_bytes);
    require(loaded_object.object_threads.front().vm.advertisement_changes&&
        loaded_object.object_threads.front().vm.advertisement_changes->empty()&&
        odh::encode_session_bundle(loaded_object)==object_bytes,"object thread preserves allocated-empty advertisement dictionary");
    auto visitor=npc_avatar_fixture();
    visitor.npc_avatars.front().runtime.vm.advertisement_changes=std::map<std::uint32_t,std::int16_t>{{7,9}};
    const auto visitor_bytes=odh::encode_session_bundle(visitor);const auto loaded_visitor=odh::decode_session_bundle(visitor_bytes);
    require(loaded_visitor.npc_avatars.front().runtime.vm.advertisement_changes==visitor.npc_avatars.front().runtime.vm.advertisement_changes&&
        odh::encode_session_bundle(loaded_visitor)==visitor_bytes,"visitor thread advertisements survive its independent VM payload");
    auto routed=local_route_fixture();auto& runtime=routed.runtime_entities.front();
    odh::RouteJobSnapshot job;static_cast<odh::LocalRouteSnapshot&>(job)=*runtime.route;
    job.pending=*runtime.vm.pending;job.route_frame=runtime.vm.stack.back();runtime.route.reset();
    runtime.route_stack=odh::RouteStackSnapshot{{job},routed.host.tick,{}};
    auto archive=odh::decode_session_archive(odh::encode_session_bundle(routed));bool changed=false;
    for(auto& section:archive.sections)if(section.id==std::array<char,4>{{'R','S','T','K'}}){
        auto& data=section.data;
        const auto read32=[&](std::size_t at){require(at+4<=data.size(),"bounded route envelope field");
            return std::uint32_t(data[at])|(std::uint32_t(data[at+1])<<8)|(std::uint32_t(data[at+2])<<16)|(std::uint32_t(data[at+3])<<24);};
        const auto write32=[&](std::size_t at,std::uint32_t value){for(unsigned i=0;i<4;++i)data[at+i]=std::uint8_t(value>>(8*i));};
        const auto entry_size=read32(6);require(data.size()>14&&data[10]==1,"first actor has a route stack");
        std::size_t cursor=13;const bool last_tick=data[cursor++]!=0;if(last_tick)cursor+=8;
        cursor+=2;const auto local_size=read32(cursor);cursor+=4+local_size+2;
        const auto vm_size=read32(cursor);require(cursor+4+vm_size<=data.size(),"bounded route VM payload");
        const auto begin=cursor+4;
        auto values=odh::decode_vm_snapshot(std::vector<std::uint8_t>(data.begin()+begin,data.begin()+begin+vm_size));
        values.advertisement_changes.emplace();const auto replacement=odh::encode_vm_snapshot(values);
        data.erase(data.begin()+begin,data.begin()+begin+vm_size);data.insert(data.begin()+begin,replacement.begin(),replacement.end());
        write32(cursor,static_cast<std::uint32_t>(replacement.size()));
        write32(6,static_cast<std::uint32_t>(entry_size-vm_size+replacement.size()));changed=true;
    }
    require(changed,"route stack section found for ownership test");
    rejects([&]{odh::decode_session_bundle(odh::encode_session_archive(archive));},
        "route frame envelope must not silently discard thread advertisement state");
}
void generic_footprint_archive_cases(){
    advertisement_thread_archive_cases();
    auto bundle=generic_action_fixture();
    bundle.world.entities.front().static_footprint=false;
    const auto bytes=odh::encode_session_bundle(bundle);
    const auto decoded=odh::decode_session_bundle(bytes);
    require(!decoded.world.entities.front().static_footprint&&
        !decoded.runtime_entities.front().route&&!decoded.runtime_entities.front().route_stack&&
        odh::encode_session_bundle(decoded)==bytes,
        "GEXP actor class roundtrips independently of an active walking job");
    auto bad=bundle;++bad.runtime_entities.front().controllers.interactions.actor_generation;
    rejects([&]{odh::encode_session_bundle(bad);},"dynamic actor class cannot bypass generation links");
    bad=bundle;bad.world.entities.front().kind=odh::EntityKind::object;
    rejects([&]{odh::encode_session_bundle(bad);},"dynamic actor class cannot bypass actor kind");
    bad=bundle;bad.runtime_entities.clear();
    rejects([&]{odh::encode_session_bundle(bad);},"dynamic actor class requires its execution graph");
    auto legacy=fixture();legacy.world.entities.front().static_footprint=false;
    rejects([&]{odh::encode_session_bundle(legacy);},"legacy dynamic actor still requires its limited route profile");
}
void retained_reference_archive_cases(){
    generic_footprint_archive_cases();
    auto bundle=generic_object_fixture();
    const auto origin=bundle.created_objects.front();
    odh::World world;require(world.restore_snapshot(bundle.world),"retained archive World restore");
    require(world.remove_retaining_body({origin.id,origin.generation}),"archive removal retains exact body");
    require(world.create_runtime_object(origin.id,origin.guid+1,{},{}),"archive numeric ID immediately reused");
    bundle.world=world.snapshot();bundle.object_threads.clear();bundle.deferred_removals.clear();bundle.removed_original_objects.clear();
    bundle.scheduler.entities.erase(std::remove_if(bundle.scheduler.entities.begin(),bundle.scheduler.entities.end(),[&](const auto& e){return e.id==origin.id;}),bundle.scheduler.entities.end());
    auto next=origin;next.generation=world.find(origin.id)->generation;next.guid++;
    bundle.created_objects={next};bundle.object_history=odh::SessionObjectHistory{};
    bundle.object_history->complete=true;bundle.object_history->reusable_entity_ids=true;
    bundle.object_history->retired_created_objects={origin};
    auto& vm=bundle.runtime_entities.front().vm;
    odh::VmFrame frame{};frame.key=odh::routine_key(4096);frame.context={1,1,origin.id,0};
    require(world.bind_context_live(frame.context),"bind all archive context roles atomically");
    require(odh::vm_context_set_reference(frame.context,odh::VmEntityRole::stack_object,{origin.id,origin.generation}),"archive frame keeps original generation");
    vm.stack={frame};vm.action_tree_known=true;
    const auto bytes=odh::encode_session_bundle(bundle);const auto decoded=odh::decode_session_bundle(bytes);
    require(decoded.world.retained_entities.size()==1&&decoded.object_history->reusable_entity_ids&&
        decoded.runtime_entities.front().vm.stack.front().context.stack_object_generation==origin.generation&&
        decoded.created_objects.front().generation!=origin.generation&&odh::encode_session_bundle(decoded)==bytes,
        "same number old retained body and new live identity survive exact session roundtrip");
    {
        auto part=bundle;constexpr std::uint32_t master=0xe1234567U;
        part.object_history->retired_created_objects.front().master_guid=master;
        part.world.retained_entities.front().definition->master_guid=master;
        const auto encoded=odh::encode_session_bundle(part);
        const auto copy=odh::decode_session_bundle(encoded);
        require(copy.object_history->retired_created_objects.front().master_guid==master&&
            copy.world.retained_entities.front().definition->master_guid==master&&
            !copy.object_groups&&odh::encode_session_bundle(copy)==encoded,
            "retired multipart provenance survives without any live group or aliases to reused ID");
        auto archive=odh::decode_session_archive(encoded);
        const auto section=std::find_if(archive.sections.begin(),archive.sections.end(),[](const auto& v){return v.id==std::array<char,4>{{'O','H','S','T'}};});
        require(section!=archive.sections.end()&&section->data[0]==4,"master provenance requires OHST4");
        for(const auto invalid:{std::uint32_t{0},origin.guid,master+1}) {
            auto mismatch=part;mismatch.object_history->retired_created_objects.front().master_guid=invalid;
            rejects([&]{odh::encode_session_bundle(mismatch);},"invalid or mismatched retired master rejected");
        }
        auto missing=part;missing.object_history->retired_created_objects.front().master_guid.reset();
        rejects([&]{odh::encode_session_bundle(missing);},"retired World master requires exact history master");
        part.object_history->retired_created_objects.front().master_guid.reset();
        part.world.retained_entities.front().definition->master_guid.reset();
        require(odh::encode_session_bundle(part)==bytes,"absent master retains exact legacy history bytes");
    }
    auto bad=bundle;bad.runtime_entities.front().vm.stack.front().context.stack_object_generation=UINT64_MAX;
    rejects([&]{odh::encode_session_bundle(bad);},"missing captured body generation accepted");
    bad=bundle;bad.object_history->retired_created_objects.clear();
    rejects([&]{odh::encode_session_bundle(bad);},"retained body without exact provenance accepted");
    bad=bundle;bad.runtime_entities.front().vm.stack.front().context.stack_object_generation=0;
    rejects([&]{odh::encode_session_bundle(bad);},"nil reference must not keep an unowned old body alive");
    bad=bundle;bad.runtime_entities.front().vm.stack.front().context.stack_object_generation=0;
    odh::World collected;require(collected.restore_snapshot(bad.world)&&collected.collect_retained_bodies({}),"collect unreferenced archive body with exact word accounting");bad.world=collected.snapshot();
    const auto nil=odh::decode_session_bundle(odh::encode_session_bundle(bad));
    require(nil.runtime_entities.front().vm.stack.front().context.stack_object==origin.id&&
        nil.runtime_entities.front().vm.stack.front().context.stack_object_generation==0,
        "known nil preserves raw number despite new live occupant");
    auto transient=bundle;transient.world.entities.front().dead=true;
    rejects([&]{odh::encode_session_bundle(transient);},"indexed Dead phase cannot become a safe session checkpoint");
    auto guest=npc_avatar_fixture();const auto guest_origin=guest.npc_avatars.front().origin;
    odh::World guest_world;require(guest_world.restore_snapshot(guest.world)&&guest_world.remove_retaining_body({guest_origin.id,guest_origin.generation}),"retain visiting avatar body");
    guest.world=guest_world.snapshot();guest.npc_avatars.clear();
    guest.scheduler.entities.erase(std::remove_if(guest.scheduler.entities.begin(),guest.scheduler.entities.end(),[&](const auto& e){return e.id==guest_origin.id;}),guest.scheduler.entities.end());
    guest.object_history=odh::SessionObjectHistory{};guest.object_history->reusable_entity_ids=true;
    guest.object_history->retired_npc_avatars={guest_origin};
    auto guest_frame=frame;guest_frame.context={1,1,guest_origin.id,0};
    require(guest_world.bind_context_live(guest_frame.context)&&odh::vm_context_set_reference(guest_frame.context,odh::VmEntityRole::stack_object,{guest_origin.id,guest_origin.generation}),"bind retained visitor to foreign Main");
    guest.runtime_entities.front().vm.stack={guest_frame};guest.runtime_entities.front().vm.action_tree_known=true;
    const auto guest_bytes=odh::encode_session_bundle(guest);const auto guest_copy=odh::decode_session_bundle(guest_bytes);
    require(guest_copy.object_history->retired_npc_avatars.size()==1&&odh::encode_session_bundle(guest_copy)==guest_bytes,"retained visitor origin and body persist independently of live NPC roster");
    {
        auto direct=guest;
        direct.object_history->retired_npc_avatars.front().neighbor_id=0;
        auto& retained=direct.world.retained_entities.front();retained.person_data[31]=0;
        const auto encoded=odh::encode_session_bundle(direct);
        auto archive=odh::decode_session_archive(encoded);
        auto section=std::find_if(archive.sections.begin(),archive.sections.end(),[](const auto& s){return s.id==std::array<char,4>{{'O','H','S','T'}};});
        require(section!=archive.sections.end()&&section->data[0]==3&&
            odh::decode_session_bundle(encoded).object_history->retired_npc_avatars.front().neighbor_id==0&&
            odh::encode_session_bundle(odh::decode_session_bundle(encoded))==encoded,
            "retired resource-only NPC zero NeighborID retains exact origin/body through OHST3");
        section->data[0]=2;
        rejects([&]{odh::decode_session_bundle(odh::encode_session_archive(archive));},
            "legacy OHST2 must not reinterpret retired resource-only NPC NeighborID zero");
    }
    {
        auto household=guest;household.object_history->retired_npc_avatars.clear();
        odh::SessionRetiredHouseholdAvatar retired;retired.member={guest_origin.id,guest_origin.generation,guest_origin.guid,
            guest_origin.neighbor_id,guest_origin.object_definition_id,"PlayerData/Characters/User3.iff",guest_origin.character_fingerprint};
        retired.source_lease.neighborhood_identity.fill(1);retired.source_lease.family_identity.fill(2);
        retired.source_lease.occupancy_epoch=3;retired.source_lease.family_chunk_id=21;retired.source_checkpoint_sha256.fill(4);
        household.object_history->retired_household_avatars={retired};
        const auto encoded=odh::encode_session_bundle(household);const auto decoded=odh::decode_session_bundle(encoded);
        const auto archive=odh::decode_session_archive(encoded);
        const auto section=std::find_if(archive.sections.begin(),archive.sections.end(),[](const auto& item){return item.id==std::array<char,4>{{'O','H','S','T'}};});
        require(section!=archive.sections.end()&&section->data.front()==5&&decoded.object_history->retired_npc_avatars.empty()&&
            decoded.world.retained_entities.size()==1&&decoded.object_history->retired_household_avatars.size()==1&&
            odh::same_session_retired_household_avatar(retired,decoded.object_history->retired_household_avatars.front())&&
            odh::encode_session_bundle(decoded)==encoded,"OHST5 preserves distinct retired household role and live foreign Thread reference");
        {auto original_family_zero=household;original_family_zero.object_history->retired_household_avatars.front().source_lease.family_chunk_id=0;
            require(odh::decode_session_bundle(odh::encode_session_bundle(original_family_zero)).object_history->retired_household_avatars.front().source_lease.family_chunk_id==0,
                "original FAMI chunk zero remains a real historical identity");}
        for(unsigned scenario=0;scenario<10;++scenario){auto bad=household;auto& value=bad.object_history->retired_household_avatars.front();
            switch(scenario){case 0:value.source_lease.occupancy_epoch=0;break;
            case 1:value.source_lease.family_identity.fill(0);break;case 2:value.source_checkpoint_sha256.fill(0);break;
            case 3:value.member.character_asset="../User3.iff";break;
            case 4:bad.object_history->retired_household_avatars.push_back(value);break;
            case 5:bad.object_history->retired_npc_avatars={guest_origin};break;
            case 6:++value.member.guid;break;case 7:value.member.generation=UINT64_MAX;break;
            case 8:value.member.character_fingerprint.fill(0);break;case 9:value.member.id=1;break;}
            rejects([&]{odh::encode_session_bundle(bad);},"malformed/aliased retired household provenance accepted");
        }
        auto wire=archive;auto& bytes=wire.sections.at(static_cast<std::size_t>(section-archive.sections.begin())).data;
        bytes.pop_back();rejects([&]{odh::decode_session_bundle(odh::encode_session_archive(wire));},"truncated household history accepted");
        household.object_history->retired_household_avatars.clear();household.object_history->retired_npc_avatars={guest_origin};
        require(odh::encode_session_bundle(household)==guest_bytes,"absence of household origins preserves exact legacy OHST bytes");
    }
    auto legacy=fixture();require(world.bind_context_live(frame.context),"existing bound context unchanged");
    legacy.runtime_entities.front().vm.stack={frame};legacy.runtime_entities.front().vm.stack.front().context={1,1,30000,0};
    rejects([&]{odh::bind_legacy_session_entity_references(legacy,world);},"native legacy missing body must not be silently invented as nil");
}

void blocking_dialog_archive_cases() {
    for(const auto opcode:{std::uint16_t{36},std::uint16_t{38},std::uint16_t{39}}){
        odh::World world;require(world.create(1,odh::EntityKind::avatar)&&world.create(2,odh::EntityKind::object),
            "create dialog thread owner and distinct logical caller");
        odh::DialogServices services;
        services.thread_owner=[&](const odh::Vm&)->std::optional<odh::WorldEntityReference>{
            return odh::WorldEntityReference{1,world.find(1)->generation};
        };
        services.prepare=[](auto,const auto&,auto,std::string&)->std::optional<odh::DialogPrompt>{
            odh::DialogPrompt prompt;prompt.title="Confirmar";prompt.message="Continuar?";
            prompt.yes="Sim";prompt.no="Não";return prompt;
        };
        services.show=[](const auto&,bool,std::string&){return true;};
        odh::DialogController dialogs(world,std::move(services));
        odh::Instruction instruction{opcode,254,255,{}};instruction.operands[5]=1;
        odh::Bhav routine{0x8002,{instruction},{},odh::Bhav::Metadata{0,4,4,0}};
        odh::Vm vm({{odh::routine_key(4096),std::move(routine)}},world.vm_memory(),{},odh::dialog_primitive(dialogs));
        odh::EntityThreadControllers controllers(world,1);
        odh::VmContext context{2,2,2,0};require(world.bind_context_live(context),"capture actual dialog frame identities");
        require(vm.start(odh::routine_key(4096),context)==odh::VmStatus::running&&controllers.bind(vm),
            "bind actual dialog thread");
        require(vm.run(1)==odh::VmStatus::waiting&&dialogs.pending().has_value(),"actual dialog suspends its VM");
        odh::EntityVmScheduler scheduler(4);
        require(scheduler.add(1,vm,[](std::uint64_t){return true;},[]{return true;},[]{return true;}),"register dialog VM");
        auto bundle=fixture();bundle.world=world.snapshot();
        bundle.runtime_entities={{1,vm.snapshot(),controllers.snapshot()}};bundle.scheduler=scheduler.snapshot();
        bundle.dialog=*dialogs.pending();bundle.dialog->input="rascunho ç";bundle.dialog->selected_button=1;
        const auto bytes=odh::encode_session_bundle(bundle);const auto reopened=odh::decode_session_bundle(bytes,bundle.resources);
        require(reopened.dialog&&reopened.dialog->input==bundle.dialog->input&&
            reopened.dialog->selected_button==1&&reopened.dialog->thread_owner.id==1&&
            reopened.dialog->source.context.caller==2&&odh::encode_session_bundle(reopened)==bytes,
            "dialog prompt, draft and exact VM wait survive a canonical whole session");
        auto bad=bundle;bad.dialog.reset();
        rejects([&]{(void)odh::encode_session_bundle(bad);},"dialog VM cannot be saved without its prompt");
        bad=bundle;bad.dialog->thread_owner.generation++;
        rejects([&]{(void)odh::encode_session_bundle(bad);},"dialog rejects replaced thread owner");
        bad=bundle;bad.dialog->thread_owner={2,world.find(2)->generation};
        rejects([&]{(void)odh::encode_session_bundle(bad);},"logical Caller cannot substitute for the actual dialog thread owner");
        bad=bundle;bad.dialog->ticket++;
        rejects([&]{(void)odh::encode_session_bundle(bad);},"dialog rejects another pending ticket");
        bad=bundle;bad.dialog->run_generation++;
        rejects([&]{(void)odh::encode_session_bundle(bad);},"dialog rejects an earlier VM run");
        bad=bundle;bad.runtime_entities.front().vm.stack.back().locals.front()++;
        rejects([&]{(void)odh::encode_session_bundle(bad);},"dialog cannot restore against changed frame locals");
        auto archive=odh::decode_session_archive(bytes);
        for(auto& section:archive.sections)if(section.id==std::array<char,4>{'D','L','O','G'})section.required=false;
        rejects([&]{(void)odh::decode_session_bundle(odh::encode_session_archive(archive));},"dialog state cannot be optional");
        archive=odh::decode_session_archive(bytes);
        archive.sections.erase(std::remove_if(archive.sections.begin(),archive.sections.end(),[](const auto& section){
            return section.id==std::array<char,4>{'D','L','O','G'};
        }),archive.sections.end());
        rejects([&]{(void)odh::decode_session_bundle(odh::encode_session_archive(archive));},"stripped dialog section cannot silently lose a prompt");
    }
}

int main() {
    {
        auto bundle=fixture();
        const auto legacy=odh::encode_session_bundle(bundle);
        const auto old=odh::decode_session_bundle(legacy);
        require(!old.world.entities[0].suppress_referenced_reset&&!old.world.entities[1].suppress_referenced_reset,
            "legacy session retains unknown recovery policy");
        bundle.world.entities[0].suppress_referenced_reset=false;
        bundle.world.entities[1].suppress_referenced_reset=true;
        const auto bytes=odh::encode_session_bundle(bundle);
        const auto decoded=odh::decode_session_bundle(bytes);
        require(decoded.world.entities[0].suppress_referenced_reset==std::optional<bool>{false}&&
            decoded.world.entities[1].suppress_referenced_reset==std::optional<bool>{true}&&
            odh::encode_session_bundle(decoded)==bytes,"full session preserves both known policy values byte-stably");
        odh::World reopened;
        require(reopened.restore_snapshot(decoded.world)&&
            reopened.suppress_referenced_reset({2,reopened.find(2)->generation})==std::optional<bool>{true},
            "fresh runtime World owns the policy decoded from the complete archive");
    }
    {
        // Installed lots can exceed the former 131,072 live-word ceiling after
        // immutable object metadata is bound. The save codec and candidate
        // World must agree, while an explicitly smaller profile still rejects.
        odh::World large(odh::installed_lot_world_limits());
        for(std::int16_t id=1;id<=40;++id)
            require(large.create(id,odh::EntityKind::object,{4096,0,0,0}),"create large lot fixture");
        require(large.word_count()>131072,"large fixture crosses former admission limit");
        const auto codec=odh::SessionBundleLimits{}.world;
        const auto bytes=odh::encode_world_snapshot(large.snapshot(),codec);
        odh::World reopened(odh::installed_lot_world_limits());
        require(reopened.restore_snapshot(odh::decode_world_snapshot(bytes,codec)),
            "application codec reopens larger installed World");
        require(odh::encode_world_snapshot(reopened.snapshot(),codec)==bytes,
            "larger World codec round trip preserves all banks");
        auto smaller=codec;smaller.world.words=131072;
        rejects([&]{odh::decode_world_snapshot(bytes,smaller);},
            "explicit smaller profile rejects larger World");
    }
    blocking_dialog_archive_cases();
    retained_reference_archive_cases();
    carried_route_archive_tests();
    architecture_route_event_archive_tests();
    runtime_globals_codec_cases();
    object_trade_ledger_cases();
    transient_selected_range_archive_tests();
    source_slot_chair_choices_archive_tests();
    sparse_locomotion_archive_tests();
    route_stack_archive_tests();
    unplaced_room_failure_archive_tests();
    route_cache_archive_tests();
    npc_avatar_cases();
    object_history_cases();
    ui_state_cases();
    named_resource_owner_cases();
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
    for(const bool returned:{false,true}){
        const auto immediate=immediate_interaction_fixture(returned);
        const auto immediate_bytes=odh::encode_session_bundle(immediate);
        const auto immediate_restored=odh::decode_session_bundle(immediate_bytes,immediate.resources);
        require(odh::encode_session_bundle(immediate_restored)==immediate_bytes&&
            immediate_restored.runtime_entities[0].vm.immediate_continuations.size()==1&&
            immediate_restored.runtime_entities[0].vm.immediate_return.has_value()==returned&&
            immediate_restored.runtime_entities[0].controllers.schedule_idle_start==0,
            returned?"popped immediate return and shared clock round-trip":
                     "active immediate child-create wait and shared clock round-trip");
    }
    for(const auto mode_flags:{std::uint8_t{0},std::uint8_t{0x11}}){
        for(const bool returned:{false,true}){
            const auto parked=parked_playback_parent_fixture(returned,mode_flags);
            const auto parked_bytes=odh::encode_session_bundle(parked);
            const auto parked_restored=odh::decode_session_bundle(parked_bytes,parked.resources);
            const auto& saved=parked_restored.runtime_entities[0];
            require(odh::encode_session_bundle(parked_restored)==parked_bytes&&
                saved.vm.immediate_continuations.front().parent_pending->instruction.instruction.opcode==44&&
                saved.playback.state&&!saved.playback.state->wait&&
                saved.playback.state->resource.source==2&&saved.playback.state->resource.id==9&&
                saved.vm.immediate_continuations.front().parent_pending->instruction.instruction.operands[0]==7&&
                saved.vm.immediate_return.has_value()==returned,
                "parked mode-0/3 animation parent and independent current child Playback state round-trip");
        }
    }
    auto invalid_parked_animation=parked_playback_parent_fixture(false,1);
    rejects([&]{(void)odh::encode_session_bundle(invalid_parked_animation);},
        "parked animation continuation rejects nonblocking mode 1");
    invalid_parked_animation=parked_playback_parent_fixture(false,16);
    rejects([&]{(void)odh::encode_session_bundle(invalid_parked_animation);},
        "parked animation continuation rejects nonblocking mode 2");
    invalid_parked_animation=parked_playback_parent_fixture();
    invalid_parked_animation.runtime_entities[0].vm.immediate_continuations[0].parent_pending->
        instruction.instruction.operands[5]=0x80;
    rejects([&]{(void)odh::encode_session_bundle(invalid_parked_animation);},
        "parked animation continuation rejects flags outside the supported playback profile");
    invalid_parked_animation=parked_playback_parent_fixture();
    invalid_parked_animation.runtime_entities[0].vm.immediate_continuations[0].parent_pending->
        instruction.instruction.operands[0]=0;
    invalid_parked_animation.runtime_entities[0].vm.immediate_continuations[0].parent_pending->
        instruction.instruction.operands[1]=0;
    rejects([&]{(void)odh::encode_session_bundle(invalid_parked_animation);},
        "parked opcode-44 id-zero reset cannot be represented as a pending wait");
    invalid_parked_animation=parked_playback_parent_fixture();
    invalid_parked_animation.runtime_entities[0].vm.immediate_continuations[0].parent_pending->
        instruction.instruction.operands[0]=1;
    invalid_parked_animation.runtime_entities[0].vm.immediate_continuations[0].parent_pending->
        instruction.instruction.operands[5]=4;
    rejects([&]{(void)odh::encode_session_bundle(invalid_parked_animation);},
        "parked animation resource parameter index outside parent arguments is rejected");
    invalid_parked_animation=parked_playback_parent_fixture();
    invalid_parked_animation.runtime_entities[0].vm.stack[0].arguments.clear();
    rejects([&]{(void)odh::encode_session_bundle(invalid_parked_animation);},
        "parked animation argument output index outside parent arguments is rejected");
    invalid_parked_animation=parked_playback_parent_fixture();
    invalid_parked_animation.runtime_entities[0].vm.immediate_continuations[0].parent_pending->
        instruction.depth++;
    rejects([&]{(void)odh::encode_session_bundle(invalid_parked_animation);},
        "parked animation continuation depth differing from its frozen parent frame is rejected");
    invalid_parked_animation=parked_playback_parent_fixture();
    invalid_parked_animation.runtime_entities[0].vm.immediate_continuations[0].parent_pending->
        context.caller=2;
    rejects([&]{(void)odh::encode_session_bundle(invalid_parked_animation);},
        "parked animation caller differing from its avatar actor is rejected");
    auto current_child_animation=parked_playback_parent_fixture();
    auto& current_child=current_child_animation.runtime_entities[0];
    current_child.vm.stack.back().arguments={0};
    current_child.vm.pending->instruction.instruction={44,254,255,{9,0,0,0,2,0,1,0}};
    current_child.playback.state->wait=odh::PlaybackWaitSnapshot{
        current_child.vm.pending->ticket,odh::VmFrameBank::arguments,0,1};
    const auto child_animation_bytes=odh::encode_session_bundle(current_child_animation);
    require(odh::encode_session_bundle(odh::decode_session_bundle(child_animation_bytes))==child_animation_bytes,
        "parked parent animation and current child opcode-44 wait retain separate exact continuations");
    current_child_animation.runtime_entities[0].playback.state->wait->expected=2;
    rejects([&]{(void)odh::encode_session_bundle(current_child_animation);},
        "parked parent animation does not weaken current child opcode-44 wait linkage");
    auto invalid_immediate=immediate_interaction_fixture();
    invalid_immediate.runtime_entities[0].controllers.sleep.actor.reset();
    rejects([&]{(void)odh::encode_session_bundle(invalid_immediate);},
        "parked Sleep parent without its controller actor is rejected");
    invalid_immediate=immediate_interaction_fixture();
    invalid_immediate.runtime_entities[0].controllers.schedule_idle_start.reset();
    rejects([&]{(void)odh::encode_session_bundle(invalid_immediate);},
        "immediate parent continuation without shared schedule clock is rejected");
    invalid_immediate=immediate_interaction_fixture();
    invalid_immediate.runtime_entities[0].vm.immediate_continuations[0].ticket=3;
    rejects([&]{(void)odh::encode_session_bundle(invalid_immediate);},
        "immediate continuation ticket that differs from protected action is rejected");
    invalid_immediate=immediate_interaction_fixture();
    invalid_immediate.runtime_entities[0].controllers.schedule_idle_start=1;
    rejects([&]{(void)odh::encode_session_bundle(invalid_immediate);},
        "shared schedule clock ahead of host/controller clocks is rejected");
    invalid_immediate=immediate_interaction_fixture();
    invalid_immediate.runtime_entities[0].controllers.interactions.actions.back().call.context.code_owner=0x80000000U;
    invalid_immediate.runtime_entities[0].vm.stack.back().context.code_owner=0x80000000U;
    invalid_immediate.runtime_entities[0].vm.pending->context.code_owner=0x80000000U;
    invalid_immediate.runtime_entities[0].vm.pending->instruction.code_owner=0x80000000U;
    rejects([&]{(void)odh::encode_session_bundle(invalid_immediate);},
        "immediate action with an undeclared resource owner is rejected");
    invalid_immediate=immediate_interaction_fixture();
    invalid_immediate.runtime_entities[0].controllers.interactions.actions.back().flags=0;
    rejects([&]{(void)odh::encode_session_bundle(invalid_immediate);},
        "immediate action return mode without RunImmediately flag is rejected");
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
    auto resident_range=route_bundle;set_source_range_route(resident_range.runtime_entities.front());
    const auto resident_range_bytes=odh::encode_session_bundle(resident_range);
    const auto resident_range_reopened=odh::decode_session_bundle(resident_range_bytes,resident_range.resources);
    require(resident_range_reopened.runtime_entities.front().route->source_range&&
        resident_range_reopened.runtime_entities.front().route->source_range->fallback_goals.size()==10&&
        odh::encode_session_bundle(resident_range_reopened)==resident_range_bytes,
        "resident RTES route uses the same canonical codec7 range payload as NPCA");
    auto bad_route=route_bundle;bad_route.runtime_entities[0].route.reset();
    rejects([&]{odh::encode_session_bundle(bad_route);},"Route45 pending wait without path state is rejected");
    bad_route=route_bundle;bad_route.world.entities[1].position->y++;
    rejects([&]{odh::encode_session_bundle(bad_route);},"legacy route retains its conservative target-pose validation");
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
    // This fixture targets the ordinary avatar2, not the historical native
    // Destination marker. Direction255 therefore has source AnyDirection
    // semantics even though its raw instruction matches the old marker call.
    go_here_route.runtime_entities[0].route->face_anywhere=true;
    go_here_route.runtime_entities[0].route->facing=0;
    const auto go_here_bytes=odh::encode_session_bundle(go_here_route);
    const auto go_here_restored=odh::decode_session_bundle(go_here_bytes,go_here_route.resources);
    require(go_here_restored.runtime_entities[0].route&&
            go_here_restored.runtime_entities[0].route->call_failure_trees&&
            go_here_restored.runtime_entities[0].route->entry_flags==0&&
            go_here_restored.runtime_entities[0].route->face_anywhere&&
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
    {
        auto physical=dynamic_route;
        auto& link=*physical.runtime_entities[0].dynamic_action_owners[0].linked_semiglobal;
        link.name="CarGlobals";link.entry="CarGlobals.IFF";
        const auto bytes=odh::encode_session_bundle(physical);
        const auto copy=odh::decode_session_bundle(bytes,physical.resources);
        require(copy.runtime_entities[0].dynamic_action_owners[0].linked_semiglobal->entry=="CarGlobals.IFF"&&
            odh::encode_session_bundle(copy)==bytes,"dynamic GLOB uppercase physical extension survives byte-exact roundtrip");
        for(const auto* entry:{"Other.IFF","carglobals.IFF"}) {
            link.entry=entry;
            rejects([&]{odh::encode_session_bundle(physical);},"dynamic GLOB stem remains exact despite extension folding");
        }
    }
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
    auto point_route_bundle=route_bundle;
    auto& point_route=*point_route_bundle.runtime_entities.front().route;
    point_route.entry_flags=0;point_route.arrival_radian_direction=1.2345F;
    point_route.call_failure_trees=(point_route_bundle.runtime_entities.front().vm.pending->instruction.instruction.operands[4]&1U)==0;
    const auto point_bytes=odh::encode_session_bundle(point_route_bundle);
    const auto point_archive=odh::decode_session_archive(point_bytes);
    const auto point_section=std::find_if(point_archive.sections.begin(),point_archive.sections.end(),
        [](const auto& section){return section.id==std::array<char,4>{{'R','O','U','T'}};});
    require(point_section!=point_archive.sections.end()&&point_section->data[route_payload_offset]==3&&
        odh::decode_session_bundle(point_bytes).runtime_entities.front().route->arrival_radian_direction==std::optional<float>{1.2345F},
        "only continuous SLOT arrivals select the version-3 route payload");
    auto self_bundle=route_bundle;auto& self_state=self_bundle.runtime_entities.front();auto& self_route=*self_state.route;
    self_route.target=self_route.actor;self_route.target_generation=self_route.actor_generation;
    self_route.target_position={self_route.path.front().x,self_route.path.front().y,self_route.expected.level,0};
    self_route.target_anchor_radian_direction=0.31F;
    self_state.vm.stack.back().context.stack_object=self_route.actor;self_state.vm.pending->context.stack_object=self_route.actor;
    const auto self_bytes=odh::encode_session_bundle(self_bundle);const auto self_archive=odh::decode_session_archive(self_bytes);
    const auto self_section=std::find_if(self_archive.sections.begin(),self_archive.sections.end(),
        [](const auto& section){return section.id==std::array<char,4>{{'R','O','U','T'}};});
    require(self_section!=self_archive.sections.end()&&self_section->data[route_payload_offset]==4&&
        odh::decode_session_bundle(self_bytes).runtime_entities.front().route->target_anchor_radian_direction==std::optional<float>{0.31F},
        "self-target anchor selects version-4 route payload while old versions remain readable");
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
    simulated.simulation->avatars[0].decay.fractions[0]=std::numeric_limits<std::int16_t>::min();
    simulated.simulation->avatars[0].decay.fractions[1]=std::numeric_limits<std::int16_t>::max();
    simulated.simulation->avatars[1].decay.fractions[2]=-1;
    const auto simulation_bytes=odh::encode_session_bundle(simulated);
    const auto simulation_restored=odh::decode_session_bundle(simulation_bytes,source.resources);
    require(simulation_restored.simulation && simulation_restored.simulation->avatars[1].decay.fractions[4]==1000 &&
        simulation_restored.simulation->avatars[0].decay.fractions[0]==std::numeric_limits<std::int16_t>::min() &&
        simulation_restored.simulation->avatars[0].decay.fractions[1]==std::numeric_limits<std::int16_t>::max() &&
        simulation_restored.simulation->avatars[1].decay.fractions[2]==-1 &&
        odh::encode_session_bundle(simulation_restored)==simulation_bytes,"SIMS v1 deterministic full round-trip");
    auto invalid_sim=simulated;invalid_sim.simulation->clock.ticks=1;
    rejects([&]{odh::encode_session_bundle(invalid_sim);},"simulation tick must match host");
    invalid_sim=simulated;invalid_sim.simulation->avatars[1].id=1;
    rejects([&]{odh::encode_session_bundle(invalid_sim);},"simulation roster IDs must match runtime roster");
    invalid_sim=simulated;invalid_sim.simulation->avatars[0].decay.last_two_minute_bucket=30;
    rejects([&]{odh::encode_session_bundle(invalid_sim);},"unrepresentable native motive minute bucket rejected");
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
    {
        // Named Reach archive profile, with existing next-tick state and no opcode44 wait.
        auto reaching=live_playback_wait_fixture();auto& entity=reaching.runtime_entities.front();
        entity.vm.status=odh::VmStatus::next_tick;entity.vm.pending.reset();
        reaching.scheduler.entities.front().status=odh::VmStatus::next_tick;
        auto& animation=*entity.playback.state;
        animation.wait.reset();animation.resource.kind=odh::PlaybackResourceKey::Kind::reach;
        animation.resource.source=0;animation.resource.id=0;animation.resource.posture=0;
        animation.speed=1.0f;animation.queue={0,7};animation.events_run=2;
        const auto bytes=odh::encode_session_bundle(reaching);
        const auto reopened=odh::decode_session_bundle(bytes,reaching.resources);
        require(reopened.runtime_entities.front().vm.status==odh::VmStatus::next_tick&&
            reopened.runtime_entities.front().playback.state->resource.kind==odh::PlaybackResourceKey::Kind::reach&&
            reopened.runtime_entities.front().playback.state->queue==animation.queue&&
            odh::encode_session_bundle(reopened)==bytes,"named Reach profile and event cursor survive exact archive round-trip");
        for(unsigned field=0;field<4;++field){auto wrong=reaching;auto& state=*wrong.runtime_entities.front().playback.state;
            if(field==0)state.resource.id=4;
            else if(field==1)state.resource.source=1;
            else if(field==2)state.speed=1.2f;
            else state.loop=true;
            rejects([&]{odh::encode_session_bundle(wrong);},"invalid Reach resource or playback profile rejects");
        }
    }
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

    {
        auto bundle=fixture();bundle.world.lockout_count_clock=static_cast<std::uint32_t>(bundle.host.tick);
        const auto decoded=odh::decode_session_bundle(odh::encode_session_bundle(bundle));
        require(decoded.world.lockout_count_clock==bundle.world.lockout_count_clock,
            "session preserves World lockout clock matching scheduler tick");
        bundle.world.lockout_count_clock=static_cast<std::uint32_t>(bundle.host.tick+1);
        bool rejected=false;
        try{(void)odh::encode_session_bundle(bundle);}catch(const odh::FormatError&){rejected=true;}
        require(rejected,"session rejects a World lockout clock ahead of its scheduler");
    }
    {
        auto bundle=fixture();const auto legacy=odh::encode_session_bundle(bundle);
        odh::PreparedArchitecture p;p.width=p.height=2;p.buildable.assign(4,1);
        for(auto& level:p.levels){level.floors.assign(4,0);level.walls.resize(4);}
        p.levels[0].floors[1]=1;
        bundle.architecture=odh::bind_architecture_snapshot(p,{});
        const auto encoded=odh::encode_session_bundle(bundle);
        const auto decoded=odh::decode_session_bundle(encoded);
        require(decoded.architecture&&decoded.architecture->prepared.levels[0].floors[1]==1,"edited architecture survives complete session codec");
        require(odh::encode_session_bundle(decoded)==encoded,"edited architecture preserves whole session bytes");
        auto archive=odh::decode_session_archive(encoded);
        for(auto& section:archive.sections)if(section.id==std::array<char,4>{{'A','R','C','T'}})section.required=false;
        rejects([&]{(void)odh::decode_session_bundle(odh::encode_session_archive(archive));},"edited geometry cannot be silently ignored by older readers");
        bundle.architecture.reset();require(odh::encode_session_bundle(bundle)==legacy,"legacy sessions do not gain architecture sections");
    }
    {
        auto bundle=fixture();const auto legacy=odh::encode_session_bundle(bundle);
        bundle.architecture_object_support=odh::make_architecture_object_support(2,2);
        bundle.architecture_object_support->levels[0][1]=1;
        bundle.architecture_object_support->levels[1][3]=1;
        const auto encoded=odh::encode_session_bundle(bundle);
        const auto decoded=odh::decode_session_bundle(encoded);
        require(decoded.architecture_object_support==bundle.architecture_object_support,"object support event history survives both floors");
        require(odh::encode_session_bundle(decoded)==encoded,"object support preserves complete session bytes");
        auto archive=odh::decode_session_archive(encoded);
        for(auto& section:archive.sections)if(section.id==std::array<char,4>{{'O','S','U','P'}})section.required=false;
        rejects([&]{(void)odh::decode_session_bundle(odh::encode_session_archive(archive));},"object support cannot be silently ignored");
        archive=odh::decode_session_archive(encoded);
        for(auto& section:archive.sections)if(section.id==std::array<char,4>{{'O','S','U','P'}})section.data.back()=2;
        rejects([&]{(void)odh::decode_session_bundle(odh::encode_session_archive(archive));},"object support rejects invalid boolean marker");
        archive=odh::decode_session_archive(encoded);
        for(auto& section:archive.sections)if(section.id==std::array<char,4>{{'O','S','U','P'}})section.data.pop_back();
        rejects([&]{(void)odh::decode_session_bundle(odh::encode_session_archive(archive));},"object support rejects truncated grid");
        odh::PreparedArchitecture p;p.width=p.height=3;p.buildable.assign(9,1);
        for(auto& level:p.levels){level.floors.assign(9,0);level.walls.resize(9);}
        bundle.architecture=odh::bind_architecture_snapshot(p,{});
        rejects([&]{(void)odh::encode_session_bundle(bundle);},"object support must match edited architecture extent");
        bundle.architecture.reset();bundle.architecture_object_support.reset();
        require(odh::encode_session_bundle(bundle)==legacy,"legacy sessions retain unknown support without new sections");
    }
    {
        auto bundle=fixture();const auto legacy=odh::encode_session_bundle(bundle);
        odh::ArchitectureObjectState state;state.object_support=odh::make_architecture_object_support(2,2);
        for(auto& level:state.occupied_walls)level.assign(4,0);
        state.object_support.levels[0][1]=1;state.occupied_walls[1][2]=8;
        bundle.architecture_object_state=state;
        const auto encoded=odh::encode_session_bundle(bundle);
        auto decoded=odh::decode_session_bundle(encoded);
        require(decoded.architecture_object_state&&!decoded.architecture_object_support,"complete architecture events have a single native owner");
        require(odh::encode_session_bundle(decoded)==encoded,"complete architecture events preserve whole session bytes");
        auto archive=odh::decode_session_archive(encoded);
        for(auto& section:archive.sections)if(section.id==std::array<char,4>{{'A','O','B','J'}})section.required=false;
        rejects([&]{(void)odh::decode_session_bundle(odh::encode_session_archive(archive));},"architecture event state cannot be ignored");
        bundle.architecture_object_support=state.object_support;
        rejects([&]{(void)odh::encode_session_bundle(bundle);},"complete and legacy support cannot compete");
        bundle.architecture_object_support.reset();
        odh::ObjectPositionRegistrySnapshot historical;historical.width=historical.height=2;historical.rooms.resize(1);
        historical.rooms[0].entities.push_back({999,99});historical.retired_owners.push_back({999,99});
        bundle.architecture_object_state->position_registry=historical;
        const auto opaque=odh::encode_session_bundle(bundle);
        require(odh::encode_session_bundle(odh::decode_session_bundle(opaque))==opaque,"incomplete tagged retired references preserve bytes without query authority");
        bundle.architecture_object_state->position_registry->retired_owners.clear();
        rejects([&]{(void)odh::encode_session_bundle(bundle);},"untagged stale registry identity cannot be saved");
        historical.rooms[0].entities.clear();historical.retired_owners={{bundle.world.entities.front().id,bundle.world.entities.front().generation}};
        bundle.architecture_object_state->position_registry=historical;
        rejects([&]{(void)odh::encode_session_bundle(bundle);},"live identity cannot claim a retired tag");
        bundle.architecture_object_state.reset();
        require(odh::encode_session_bundle(bundle)==legacy,"legacy absence retains exact session bytes");
    }
    {
        auto bundle=fixture();const auto legacy=odh::encode_session_bundle(bundle);
        odh::NeighborInventorySnapshot inventory;inventory.inventory_by_id[-1]={{2,0,65535},{2,0,0},{6,0x12345678,4}};
        bundle.neighbor_inventory=inventory;const auto encoded=odh::encode_session_bundle(bundle);
        const auto decoded=odh::decode_session_bundle(encoded);
        require(decoded.neighbor_inventory&&decoded.neighbor_inventory->inventory_by_id.at(-1).size()==3,"neighbor inventory preserves signed IDs, duplicates and zero counts");
        require(odh::encode_session_bundle(decoded)==encoded,"neighbor inventory whole archive roundtrip");
        auto archive=odh::decode_session_archive(encoded);
        for(auto& section:archive.sections)if(section.id==std::array<char,4>{{'N','I','N','V'}})section.required=false;
        rejects([&]{odh::decode_session_bundle(odh::encode_session_archive(archive));},"mutable neighbor inventory cannot be silently ignored");
        archive=odh::decode_session_archive(encoded);
        for(auto& section:archive.sections)if(section.id==std::array<char,4>{{'N','I','N','V'}})section.data.pop_back();
        rejects([&]{odh::decode_session_bundle(odh::encode_session_archive(archive));},"truncated mutable inventory rejected");
        bundle.neighbor_inventory.reset();require(odh::encode_session_bundle(bundle)==legacy,"old nontravel archives retain exact bytes");
    }
    {
        auto bundle=fixture();const auto legacy=odh::encode_session_bundle(bundle);
        require(!odh::decode_session_bundle(legacy).build_buy_permission,"legacy session omits permission and retains source true default");
        bundle.build_buy_permission=odh::BuildBuyPermissionSnapshot{false};
        const auto encoded=odh::encode_session_bundle(bundle);const auto decoded=odh::decode_session_bundle(encoded);
        require(decoded.build_buy_permission&&!decoded.build_buy_permission->enabled&&odh::encode_session_bundle(decoded)==encoded,
            "disabled Build/Buy survives whole-session roundtrip without architecture grids");
        auto archive=odh::decode_session_archive(encoded);
        for(auto& section:archive.sections)if(section.id==std::array<char,4>{{'B','B','U','Y'}})section.required=false;
        rejects([&]{(void)odh::decode_session_bundle(odh::encode_session_archive(archive));},"permission cannot be silently optional");
        for(const auto bytes:std::vector<std::vector<std::uint8_t>>{{1,0,2},{2,0,0},{1,0},{1,0,0,0}}){
            archive=odh::decode_session_archive(encoded);
            for(auto& section:archive.sections)if(section.id==std::array<char,4>{{'B','B','U','Y'}})section.data=bytes;
            rejects([&]{(void)odh::decode_session_bundle(odh::encode_session_archive(archive));},"malformed permission section rejected");
        }
        bundle.build_buy_permission=odh::BuildBuyPermissionSnapshot{true};
        require(odh::decode_session_bundle(odh::encode_session_bundle(bundle)).build_buy_permission->enabled,"explicit true permission retained");
        bundle.build_buy_permission.reset();require(odh::encode_session_bundle(bundle)==legacy,"unchanged legacy sessions preserve exact bytes");
    }
    {
        auto bundle=fixture();const auto legacy=odh::encode_session_bundle(bundle);
        odh::FamilySpellsSnapshot spells;spells.family_chunk_id=bundle.host.family_chunk_id;
        spells.family_number=bundle.host.family_number;spells.loaded_presence=true;spells.extension_dword=0xf1234567U;
        spells.words={-32768,-1,0,1,100,32767};bundle.family_spells=spells;
        const auto bytes=odh::encode_session_bundle(bundle);const auto reopened=odh::decode_session_bundle(bytes);
        require(reopened.family_spells&&odh::encode_family_spells_snapshot(*reopened.family_spells)==odh::encode_family_spells_snapshot(spells)&&
            odh::encode_session_bundle(reopened)==bytes,"private family spells preserve signed six words, extension, presence and exact archive bytes");
        auto mismatch=bundle;++mismatch.family_spells->family_chunk_id;
        rejects([&]{(void)odh::encode_session_bundle(mismatch);},"spell carrier chunk ID must match host, never FamilyNumber");
        mismatch=bundle;++mismatch.family_spells->family_number;
        rejects([&]{(void)odh::encode_session_bundle(mismatch);},"spell carrier FamilyNumber provenance must match host independently");
        auto archive=odh::decode_session_archive(bytes);
        for(auto& section:archive.sections)if(section.id==std::array<char,4>{{'F','S','P','L'}})section.required=false;
        rejects([&]{(void)odh::decode_session_bundle(odh::encode_session_archive(archive));},"spell carrier cannot be silently optional");
        for(const unsigned malformed:{0U,1U,2U}){
            archive=odh::decode_session_archive(bytes);
            for(auto& section:archive.sections)if(section.id==std::array<char,4>{{'F','S','P','L'}}){
                if(malformed==0)section.data.pop_back();else if(malformed==1)section.data.push_back(0);else section.data[6]=2;
            }
            rejects([&]{(void)odh::decode_session_bundle(odh::encode_session_archive(archive));},"truncated/trailing/nonboolean spell carrier rejected");
        }
        bundle.family_spells.reset();require(odh::encode_session_bundle(bundle)==legacy,"absent unchanged spell carrier preserves legacy whole bytes");
    }
    {
        auto bundle=fixture();const auto legacy=odh::encode_session_bundle(bundle);
        require(!odh::decode_session_bundle(legacy).type_attributes,"legacy session lacks type attribute overlay");
        bundle.type_attributes=odh::TypeAttributesSnapshot{1,{{0xf1234567,{-32768,-1,0,32767}},{700,{}}}};
        const auto encoded=odh::encode_session_bundle(bundle);const auto reopened=odh::decode_session_bundle(encoded);
        require(reopened.type_attributes&&
            odh::encode_type_attributes_snapshot(*reopened.type_attributes)==odh::encode_type_attributes_snapshot(*bundle.type_attributes)&&
            odh::encode_session_bundle(reopened)==encoded,"shared type attributes retain all signed banks through whole bundle");
        auto archive=odh::decode_session_archive(encoded);
        for(auto& section:archive.sections)if(section.id==std::array<char,4>{{'T','A','T','T'}})section.required=false;
        rejects([&]{odh::decode_session_bundle(odh::encode_session_archive(archive));},"type attributes section must be required");
        for(const unsigned malformed:{0U,1U,2U}){
            archive=odh::decode_session_archive(encoded);
            for(auto& section:archive.sections)if(section.id==std::array<char,4>{{'T','A','T','T'}}){
                if(malformed==0)section.data.pop_back();else if(malformed==1)section.data.push_back(0);else section.data[0]=2;
            }
            rejects([&]{odh::decode_session_bundle(odh::encode_session_archive(archive));},"malformed type attributes session rejected");
        }
        bundle.type_attributes.reset();require(odh::encode_session_bundle(bundle)==legacy,"absent type attributes preserve legacy whole bytes");
    }
    {
        auto bundle=generic_object_fixture();const auto legacy=odh::encode_session_bundle(bundle);
        require(!odh::decode_session_bundle(legacy).object_groups,"legacy singleton creation has no dynamic group section");
        const auto& origin=bundle.created_objects.front();
        bundle.object_groups=odh::ObjectGroupSnapshot{{{origin.guid,false,origin.id,{{origin.id,origin.generation,16,-32,-1}}}}};
        const auto single=odh::encode_session_bundle(bundle);const auto single_copy=odh::decode_session_bundle(single);
        require(single_copy.object_groups&&single_copy.object_groups->groups.front().members.front().id==origin.id&&
            single_copy.object_groups->groups.front().members.front().offset_x==16&&
            single_copy.object_groups->groups.front().members.front().offset_y==-32&&
            single_copy.object_groups->groups.front().members.front().offset_level==-1&&
            odh::encode_session_bundle(single_copy)==single,"direct singleton retains raw source offsets and exact created origin through session codec");
        bundle.object_groups.reset();require(odh::encode_session_bundle(bundle)==legacy,"absent OGRP retains exact old archive bytes");
        // A master resource is not itself instantiated: part origin GUID must
        // remain distinct from the group's requested/master definition GUID.
        constexpr std::uint32_t master=0xf1234567U;
        bundle.world.entities.back().definition->master_guid=master;
        rejects([&]{odh::encode_session_bundle(bundle);},"created master-backed object cannot silently drop its dynamic group authority");
        bundle.object_groups=odh::ObjectGroupSnapshot{{{master,true,origin.id,{{origin.id,origin.generation,-32,16,-1}}}}};
        const auto encoded=odh::encode_session_bundle(bundle);const auto decoded=odh::decode_session_bundle(encoded);
        require(decoded.object_groups&&decoded.object_groups->groups.front().definition_guid==master&&
            decoded.created_objects.front().guid==origin.guid&&origin.guid!=master&&
            decoded.object_groups->groups.front().members.front().offset_x==-32&&
            odh::encode_session_bundle(decoded)==encoded,"master group identity and exact individual part provenance are independently authenticated");
        bundle.created_objects.front().master_guid=master;
        const auto with_master=odh::encode_session_bundle(bundle);
        const auto master_copy=odh::decode_session_bundle(with_master);
        require(master_copy.created_objects.front().master_guid==master&&
            odh::encode_session_bundle(master_copy)==with_master,"live construction master survives GEXP2");
        auto master_archive=odh::decode_session_archive(with_master);
        const auto master_section=std::find_if(master_archive.sections.begin(),master_archive.sections.end(),[](const auto& v){return v.id==std::array<char,4>{{'G','E','X','P'}};});
        require(master_section!=master_archive.sections.end()&&master_section->data[0]==2,"master provenance requires GEXP2");
        for(const auto bad_master:{std::uint32_t{0},origin.guid,master+1}) {
            auto mismatch=bundle;mismatch.created_objects.front().master_guid=bad_master;
            rejects([&]{odh::encode_session_bundle(mismatch);},"live master differs from exact World/group provenance");
        }
        bundle.created_objects.front().master_guid.reset();
        require(odh::encode_session_bundle(bundle)==encoded,"legacy live OGRP without master history remains byte-identical");
        auto invalid=bundle;++invalid.object_groups->groups.front().members.front().generation;
        rejects([&]{odh::encode_session_bundle(invalid);},"group membership cannot refer to an older/reused numeric object ID");
        invalid=bundle;invalid.created_objects.clear();
        rejects([&]{odh::encode_session_bundle(invalid);},"dynamic member cannot claim imported-object provenance");
        invalid=bundle;invalid.created_objects.front().guid=master;
        rejects([&]{odh::encode_session_bundle(invalid);},"master GUID cannot replace actual part origin GUID");
        auto archive=odh::decode_session_archive(encoded);
        for(auto& section:archive.sections)if(section.id==std::array<char,4>{{'O','G','R','P'}})section.required=false;
        rejects([&]{odh::decode_session_bundle(odh::encode_session_archive(archive));},"OGRP cannot become an ignorable optional section");
        archive=odh::decode_session_archive(encoded);
        archive.sections.erase(std::remove_if(archive.sections.begin(),archive.sections.end(),[](const auto& s){
            return s.id==std::array<char,4>{{'O','G','R','P'}};}),archive.sections.end());
        rejects([&]{odh::decode_session_bundle(odh::encode_session_archive(archive));},"deleting OGRP wire section cannot orphan live created multipart objects");
        odh::SessionBundleLimits tiny;tiny.object_groups.members=0;
        rejects([&]{odh::decode_session_bundle(encoded,{},tiny);},"configured group-membership budget gates whole-session decoder");
    }
    {
        auto bundle=fixture();const auto legacy=odh::encode_session_bundle(bundle);
        require(!odh::decode_session_bundle(legacy).house_layout_history,"old native saves retain unknown House history");
        bundle.house_layout_history=odh::new_house_layout_history(200);
        bundle.house_layout_history->current={12,3};bundle.house_layout_history->last_sample_tick=0;
        const auto encoded=odh::encode_session_bundle(bundle);
        const auto restored=odh::decode_session_bundle(encoded);
        require(restored.house_layout_history&&restored.house_layout_history->current.samples==12&&
            restored.house_layout_history->current.routing==3&&restored.house_layout_history->last_sample_tick==0&&
            odh::encode_session_bundle(restored)==encoded,"House counters and known zero stamp survive exact full-session roundtrip");
        const auto id=std::array<char,4>{{'H','L','S','T'}};
        auto imported=bundle;imported.house_layout_history->last_sample_tick.reset();
        const auto imported_bytes=odh::encode_session_bundle(imported);
        require(!odh::decode_session_bundle(imported_bytes).house_layout_history->last_sample_tick&&
            odh::encode_session_bundle(odh::decode_session_bundle(imported_bytes))==imported_bytes,
            "imported original counters preserve absence of a native tick stamp");
        const auto tamper=[&](auto operation){auto archive=odh::decode_session_archive(encoded);
            for(auto& section:archive.sections)if(section.id==id)operation(section);
            rejects([&]{odh::decode_session_bundle(odh::encode_session_archive(archive));},"invalid House section accepted");};
        tamper([](auto& section){section.required=false;});
        tamper([](auto& section){section.data[0]=2;});
        tamper([](auto& section){section.data[2]^=1;});
        tamper([](auto& section){section.data[4]^=1;});
        tamper([](auto& section){section.data.pop_back();});
        tamper([](auto& section){section.data.push_back(0);});
        tamper([](auto& section){section.data[84]=2;});
        tamper([](auto& section){section.data[85]=10;}); // Tick10 cannot belong to HOST tick0.
        tamper([](auto& section){for(unsigned at=36;at<40;++at)section.data[at]=255;});
        auto invalid=bundle;invalid.house_layout_history->last_sample_tick=1;
        rejects([&]{odh::encode_session_bundle(invalid);},"House stamp outside10tick cadence rejected before save");
        invalid=bundle;invalid.house_layout_history->current={3,4};
        rejects([&]{odh::encode_session_bundle(invalid);},"routing count cannot exceed real sample count");
        auto stripped=odh::decode_session_archive(encoded);
        stripped.sections.erase(std::remove_if(stripped.sections.begin(),stripped.sections.end(),[&](const auto& section){return section.id==id;}),stripped.sections.end());
        require(!odh::decode_session_bundle(odh::encode_session_archive(stripped)).house_layout_history,
            "removed optional extension remains unknown, never reconstructed from compiled fill");
        bundle.house_layout_history.reset();require(odh::encode_session_bundle(bundle)==legacy,"history-absent legacy bytes stay unchanged");
    }
    std::cout<<"session bundle checks passed\n";
}
