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

int main() {
    const auto source=fixture();
    const auto bytes=odh::encode_session_bundle(source);
    const auto restored=odh::decode_session_bundle(bytes,source.resources);
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
