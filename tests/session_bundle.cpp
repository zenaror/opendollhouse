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
            restored.host.global_values[1].value==-4&&restored.runtime_entities.size()==2&&
            restored.runtime_entities[1].id==2,"host and both residents round-trip");
    for(const bool sleep_wait:{true,false}) {
        const auto waiting=live_wait_fixture(sleep_wait);
        const auto waiting_bytes=odh::encode_session_bundle(waiting);
        const auto waiting_restored=odh::decode_session_bundle(waiting_bytes,waiting.resources);
        require(odh::encode_session_bundle(waiting_restored)==waiting_bytes,
                "real Sleep and IdleForInput waits survive bundle round-trip");
    }

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
