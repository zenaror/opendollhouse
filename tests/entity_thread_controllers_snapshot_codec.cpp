#include "odh/entity_thread_controllers_snapshot_codec.hpp"

#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
int checks = 0;
void require(bool condition, const char* message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}
template <typename Function>
void rejects(Function&& function, const char* message) {
    bool threw = false;
    try { function(); } catch (const odh::FormatError&) { threw = true; }
    require(threw, message);
}
odh::Bhav queue_fixture_routine() {
    return {0x8002,{{2,254,255,{0,0,0,0,0,5,8,7}}},{},odh::Bhav::Metadata{0,4,4,0}};
}
odh::EntityThreadControllersSnapshot fixture() {
    odh::EntityThreadControllersSnapshot state{};
    state.sleep.actor = odh::SleepActorSnapshot{1, 7, 3};
    state.sleep.job = odh::SleepJobSnapshot{4, 2, 7, 0};
    state.sleep.last_tick = 5;
    state.sleep.clock = 5;
    state.sleep.diagnostic = "sleep state";
    state.interactions.actor_id = 1;
    state.interactions.capacity = 4;
    state.interactions.actor_generation = 7;
    state.interactions.run_generation = 3;
    state.interactions.sequence = 2;
    state.interactions.clock = 5;
    state.interactions.last_tick = 5;
    const odh::VmContext context{1, 2, 2, 22};
    const odh::VmCall first{odh::routine_key(4096), context, {3, 91, -7},
                            odh::VmCallReturnMode::interaction_next_tick};
    const odh::VmCall second{odh::routine_key(4097), context, {-9},
                             odh::VmCallReturnMode::interaction_next_tick};
    state.interactions.actions = {{1, 8, first}, {2, 9, second}};
    state.interactions.actions.front().check = odh::routine_key(4098);
    state.interactions.active = odh::InteractionActiveSnapshot{1, 6};
    state.interactions.diagnostic = "queued actions";
    state.thread_interrupt = true;
    return state;
}
odh::EntityThreadControllersSnapshot empty_fixture() {
    odh::EntityThreadControllersSnapshot state{};
    state.interactions.actor_id = 1;
    state.interactions.capacity = 1;
    return state;
}
std::vector<std::uint8_t> legacy_v1_empty_fixture() {
    std::vector<std::uint8_t> bytes;
    const auto u8=[&](std::uint8_t value){bytes.push_back(value);};
    const auto u16=[&](std::uint16_t value){u8(static_cast<std::uint8_t>(value));u8(static_cast<std::uint8_t>(value>>8));};
    const auto u32=[&](std::uint32_t value){for(unsigned n=0;n<32;n+=8)u8(static_cast<std::uint8_t>(value>>n));};
    const auto u64=[&](std::uint64_t value){for(unsigned n=0;n<64;n+=8)u8(static_cast<std::uint8_t>(value>>n));};
    u16(1); // controller codec v1
    u8(0);u8(0);u8(0);u64(0);u8(0);u8(0);u32(0); // empty Sleep state
    u16(1);u64(1);u64(0);u64(0);u64(0);u64(0);u64(0);
    u8(0);u8(0);u32(0);u8(0);u8(0);u8(0);u8(0);u32(0); // empty FIFO controller
    u8(0); // thread interrupt
    return bytes;
}
std::vector<std::uint8_t> legacy_v1_queue_fixture() {
    std::vector<std::uint8_t> bytes;
    const auto u8=[&](std::uint8_t value){bytes.push_back(value);};
    const auto u16=[&](std::uint16_t value){u8(static_cast<std::uint8_t>(value));u8(static_cast<std::uint8_t>(value>>8));};
    const auto i16=[&](std::int16_t value){u16(static_cast<std::uint16_t>(value));};
    const auto u32=[&](std::uint32_t value){for(unsigned n=0;n<32;n+=8)u8(static_cast<std::uint8_t>(value>>n));};
    const auto u64=[&](std::uint64_t value){for(unsigned n=0;n<64;n+=8)u8(static_cast<std::uint8_t>(value>>n));};
    u16(1); // controller codec v1
    u8(0);u8(0);u8(0);u64(0);u8(0);u8(0);u32(0); // empty Sleep state
    i16(1);u64(2);u64(7);u64(3);u64(1);u64(0);u64(0);
    u8(0);u8(0);u32(1); // Interaction actor/clock, then one action
    u64(1);u64(8); // native identity/generation
    u8(static_cast<std::uint8_t>(odh::routine_key(4096).scope));u16(4096);
    i16(1);i16(2);i16(2);u32(22); // call context
    u32(0);u8(static_cast<std::uint8_t>(odh::VmCallReturnMode::interaction_next_tick));
    u8(0);u8(0);u8(0);u8(0);u32(0); // no active/wait/completion/interrupt/diagnostic
    u8(0); // thread interrupt
    return bytes;
}
std::vector<std::uint8_t> legacy_v2_queue_fixture() {
    std::vector<std::uint8_t> bytes;
    const auto u8=[&](std::uint8_t value){bytes.push_back(value);};
    const auto u16=[&](std::uint16_t value){u8(static_cast<std::uint8_t>(value));u8(static_cast<std::uint8_t>(value>>8));};
    const auto i16=[&](std::int16_t value){u16(static_cast<std::uint16_t>(value));};
    const auto u32=[&](std::uint32_t value){for(unsigned n=0;n<32;n+=8)u8(static_cast<std::uint8_t>(value>>n));};
    const auto u64=[&](std::uint64_t value){for(unsigned n=0;n<64;n+=8)u8(static_cast<std::uint8_t>(value>>n));};
    u16(2); // controller codec v2
    u8(0);u8(0);u8(0);u64(0);u8(0);u8(0);u32(0); // empty Sleep state
    i16(1);u64(2);u64(7);u64(3);u64(1);u64(0);u64(0);
    u8(0);u8(0);u32(1); // Interaction actor/clock, then one action
    u64(1);u64(8); // native identity/generation
    u8(static_cast<std::uint8_t>(odh::routine_key(4096).scope));u16(4096);
    i16(1);i16(2);i16(2);u32(22); // call context
    u32(0);u8(static_cast<std::uint8_t>(odh::VmCallReturnMode::interaction_next_tick));
    u16(4);i16(50);u8(static_cast<std::uint8_t>(odh::InteractionQueueMode::normal));
    u32(1U<<28);u8(0); // queue UID, priority/mode/flags, no notification
    u8(0);u8(0);u8(0);u8(0);u32(0); // no active/wait/completion/interrupt/diagnostic
    u8(static_cast<std::uint8_t>(odh::InteractionQueueProfile::ts1_priority));u16(5);
    u8(0); // thread interrupt
    return bytes;
}
void same(const odh::EntityThreadControllersSnapshot& left,
          const odh::EntityThreadControllersSnapshot& right) {
    const auto& a = left.sleep; const auto& b = right.sleep;
    require(a.actor.has_value() == b.actor.has_value() && a.job.has_value() == b.job.has_value() &&
        a.last_tick == b.last_tick && a.clock == b.clock && a.interrupted == b.interrupted &&
        a.notify_resume == b.notify_resume && a.diagnostic == b.diagnostic, "Sleep fields round-trip");
    if (a.actor) require(a.actor->id == b.actor->id && a.actor->generation == b.actor->generation &&
        a.actor->run_generation == b.actor->run_generation, "Sleep actor round-trips");
    if (a.job) require(a.job->ticket == b.job->ticket && a.job->idle_start == b.job->idle_start &&
        a.job->deadline == b.job->deadline && a.job->argument == b.job->argument, "Sleep job round-trips");
    const auto& x = left.interactions; const auto& y = right.interactions;
    require(x.actor_id == y.actor_id && x.capacity == y.capacity &&
        x.actor_generation == y.actor_generation && x.run_generation == y.run_generation &&
        x.sequence == y.sequence && x.clock == y.clock && x.idle_start == y.idle_start &&
        x.last_tick == y.last_tick && x.resume_tick == y.resume_tick && x.actions.size() == y.actions.size() &&
        x.active.has_value() == y.active.has_value() && x.wait.has_value() == y.wait.has_value() &&
        x.completion.has_value() == y.completion.has_value() && x.interrupted == y.interrupted &&
        x.diagnostic == y.diagnostic && x.profile == y.profile && x.next_queue_uid == y.next_queue_uid &&
        left.thread_interrupt == right.thread_interrupt,
        "interaction controller fields round-trip");
    for (std::size_t i = 0; i < x.actions.size(); ++i) {
        const auto& p = x.actions[i]; const auto& q = y.actions[i];
        require(p.uid == q.uid && p.target_generation == q.target_generation &&
            p.call.key.scope == q.call.key.scope && p.call.key.id == q.call.key.id &&
            p.call.context.caller == q.call.context.caller && p.call.context.callee == q.call.context.callee &&
            p.call.context.stack_object == q.call.context.stack_object &&
            p.call.context.code_owner == q.call.context.code_owner && p.call.arguments == q.call.arguments &&
            p.call.return_mode == q.call.return_mode && p.queue_uid == q.queue_uid &&
            p.priority == q.priority && p.mode == q.mode && p.flags == q.flags &&
            p.notify_idle == q.notify_idle && p.check.has_value() == q.check.has_value() &&
            (!p.check || (p.check->id == q.check->id && p.check->scope == q.check->scope)), "queued call fields round-trip");
    }
    if (x.active) require(x.active->uid == y.active->uid && x.active->ticket == y.active->ticket,
                          "active action round-trips");
    if (x.wait) require(x.wait->ticket == y.wait->ticket && x.wait->deadline == y.wait->deadline,
                        "idle wait round-trips");
    if (x.completion) require(x.completion->uid == y.completion->uid && x.completion->ticket == y.completion->ticket &&
        x.completion->tick == y.completion->tick && x.completion->result == y.completion->result,
        "completion round-trips");
}
}

int main() {
    try {
        const auto state = fixture();
        const auto encoded = odh::encode_entity_thread_controllers_snapshot(state);
        same(state, odh::decode_entity_thread_controllers_snapshot(encoded));
        auto push_head = state;
        push_head.interactions.sequence = 3;
        push_head.interactions.next_queue_uid = 3;
        push_head.interactions.actions = {{1, 8, state.interactions.actions[0].call},
            {3, 9, state.interactions.actions[1].call},
            {2, 9, state.interactions.actions[1].call}};
        push_head.interactions.actions[0].priority=100;
        push_head.interactions.actions[0].flags=0x18000000U;
        push_head.interactions.actions[1].priority=40;
        push_head.interactions.actions[1].mode=odh::InteractionQueueMode::parent_idle;
        push_head.interactions.actions[2].priority=30;
        push_head.interactions.actions[2].mode=odh::InteractionQueueMode::parent_exit;
        same(push_head, odh::decode_entity_thread_controllers_snapshot(
            odh::encode_entity_thread_controllers_snapshot(push_head)));
        push_head.interactions.actions = {{2, 9, state.interactions.actions[1].call}};
        push_head.interactions.active.reset();
        push_head.interactions.completion = odh::InteractionCompletion{3, 7, 5, true};
        same(push_head, odh::decode_entity_thread_controllers_snapshot(
            odh::encode_entity_thread_controllers_snapshot(push_head)));
        auto action_wait = state;
        action_wait.interactions.idle_start = action_wait.interactions.clock;
        action_wait.interactions.wait = odh::InteractionWaitSnapshot{7, 9};
        const auto action_wait_bytes = odh::encode_entity_thread_controllers_snapshot(action_wait);
        same(action_wait, odh::decode_entity_thread_controllers_snapshot(action_wait_bytes));
        for (std::size_t size = 0; size < encoded.size(); ++size) {
            auto truncated = encoded;
            truncated.resize(size);
            rejects([&] { (void)odh::decode_entity_thread_controllers_snapshot(truncated); }, "every truncated prefix rejected");
        }
        auto changed = encoded;
        changed[0] = 4;
        rejects([&] { (void)odh::decode_entity_thread_controllers_snapshot(changed); }, "unsupported version rejected");
        changed = encoded;
        changed[2] = 2;
        rejects([&] { (void)odh::decode_entity_thread_controllers_snapshot(changed); }, "invalid Sleep actor flag rejected");
        changed = encoded;
        changed.push_back(0);
        rejects([&] { (void)odh::decode_entity_thread_controllers_snapshot(changed); }, "trailing bytes rejected");
        const auto empty_bytes = odh::encode_entity_thread_controllers_snapshot(empty_fixture());
        for (const auto offset : {2u, 3u, 4u, 13u, 14u, 69u, 70u, 75u, 76u, 77u, 78u, 83u}) {
            changed = empty_bytes;
            changed[offset] = 2;
            rejects([&] { (void)odh::decode_entity_thread_controllers_snapshot(changed); },
                    "every controller boolean rejects noncanonical value");
        }
        const auto legacy=odh::decode_entity_thread_controllers_snapshot(legacy_v1_empty_fixture());
        require(legacy.interactions.profile==odh::InteractionQueueProfile::legacy_fifo,
                "legacy v1 controller queues restore with explicit FIFO semantics");
        const auto legacy_queued=odh::decode_entity_thread_controllers_snapshot(legacy_v1_queue_fixture());
        require(legacy_queued.interactions.profile==odh::InteractionQueueProfile::legacy_fifo&&
                legacy_queued.interactions.actions.size()==1&&
                legacy_queued.interactions.actions.front().priority==1&&
                legacy_queued.interactions.actions.front().mode==odh::InteractionQueueMode::normal&&
                legacy_queued.interactions.actions.front().flags==0&&
                !legacy_queued.interactions.actions.front().check,
                "legacy v1 queued actions retain FIFO identity without invented TS1 metadata");
        const auto migrated=odh::decode_entity_thread_controllers_snapshot(
            odh::encode_entity_thread_controllers_snapshot(legacy));
        require(migrated.interactions.profile==odh::InteractionQueueProfile::legacy_fifo,
                "resaving a legacy queue does not invent TS1 priority state");
        const auto v2=odh::decode_entity_thread_controllers_snapshot(legacy_v2_queue_fixture());
        require(v2.interactions.profile==odh::InteractionQueueProfile::ts1_priority&&
                v2.interactions.actions.size()==1&&v2.interactions.actions.front().priority==50&&
                !v2.interactions.actions.front().check,
                "legacy v2 queues retain priority metadata and gain no invented check routine");
        const auto v2_resaved=odh::decode_entity_thread_controllers_snapshot(
            odh::encode_entity_thread_controllers_snapshot(v2));
        require(v2_resaved.interactions.actions.front().priority==50&&
                !v2_resaved.interactions.actions.front().check,
                "resaving v2 queue state in v3 preserves its no-check meaning");

        auto invalid_check=state;
        invalid_check.interactions.actions.front().check=odh::RoutineKey{static_cast<odh::RoutineScope>(99),4098};
        rejects([&]{(void)odh::encode_entity_thread_controllers_snapshot(invalid_check);},
                "invalid optional check key rejected");

        odh::World migration_world;
        require(migration_world.create(1,odh::EntityKind::avatar)&&
                migration_world.create(2,odh::EntityKind::object),"legacy migration entities created");
        odh::InteractionQueue migration_queue(migration_world,1,2);
        odh::Vm migration_vm({{odh::routine_key(4096),queue_fixture_routine()},
                              {odh::routine_key(4097),queue_fixture_routine()}},
            migration_world.vm_memory(),{},migration_queue.primitive());
        require(migration_queue.bind(migration_vm)&&
                migration_vm.start(odh::routine_key(4096),{1,1,1,0})==odh::VmStatus::running,
                "native queue VM starts for legacy migration");
        auto restored_v1=legacy_queued.interactions;
        restored_v1.actor_generation=migration_world.find(1)->generation;
        restored_v1.run_generation=migration_vm.run_generation();
        restored_v1.actions.front().target_generation=migration_world.find(2)->generation;
        require(migration_queue.restore_snapshot(restored_v1)&&
                migration_queue.enqueue_ts1(odh::routine_key(4097),{1,2,2,0},{},50,
                    odh::InteractionQueueMode::normal,0,false,false),
                "decoded v1 queue restores and accepts a new native TS1 action");
        const auto migrated_queue=migration_queue.snapshot();
        require(migrated_queue.profile==odh::InteractionQueueProfile::ts1_priority&&
                migrated_queue.actions.size()==2&&migrated_queue.actions[0].priority==50&&
                migrated_queue.actions[1].call.key.id==4096&&migrated_queue.actions[0].queue_uid==0&&
                migrated_queue.actions[1].queue_uid==1&&migrated_queue.next_queue_uid==2,
                "codec-decoded legacy action order survives TS1 adoption with unique queue IDs");

        auto limits = odh::EntityThreadControllersSnapshotCodecLimits{};
        limits.max_queue_actions = 1;
        rejects([&] { (void)odh::encode_entity_thread_controllers_snapshot(state, limits); }, "action count limit enforced");
        limits = {};
        limits.max_call_arguments = 2;
        rejects([&] { (void)odh::encode_entity_thread_controllers_snapshot(state, limits); }, "argument limit enforced");
        limits = {};
        limits.max_bytes = encoded.size() - 1;
        rejects([&] { (void)odh::decode_entity_thread_controllers_snapshot(encoded, limits); }, "byte limit enforced");
        limits = {};
        limits.max_diagnostic_bytes = 4;
        rejects([&] { (void)odh::encode_entity_thread_controllers_snapshot(state, limits); }, "diagnostic limit enforced");

        auto invalid = state;
        invalid.interactions.actions[1].uid = invalid.interactions.actions[0].uid;
        rejects([&] { (void)odh::encode_entity_thread_controllers_snapshot(invalid); }, "duplicate queued IDs rejected");
        invalid = state;
        invalid.interactions.actions[0].call.key.scope = static_cast<odh::RoutineScope>(99);
        rejects([&] { (void)odh::encode_entity_thread_controllers_snapshot(invalid); }, "invalid routine scope rejected");
        invalid = state;
        invalid.sleep.job->deadline = invalid.sleep.clock;
        rejects([&] { (void)odh::encode_entity_thread_controllers_snapshot(invalid); }, "expired Sleep deadline rejected");
        invalid = state;
        invalid.interactions.active->uid = 2;
        rejects([&] { (void)odh::encode_entity_thread_controllers_snapshot(invalid); }, "active FIFO mismatch rejected");

        std::cout << checks << " thread controller snapshot codec checks passed (synthetic values only)\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
