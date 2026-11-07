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
    state.interactions.actions.front().check_permissions = true;
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
std::vector<std::uint8_t> legacy_v3_checked_queue_fixture() {
    std::vector<std::uint8_t> bytes;
    const auto u8=[&](std::uint8_t value){bytes.push_back(value);};
    const auto u16=[&](std::uint16_t value){u8(static_cast<std::uint8_t>(value));u8(static_cast<std::uint8_t>(value>>8));};
    const auto i16=[&](std::int16_t value){u16(static_cast<std::uint16_t>(value));};
    const auto u32=[&](std::uint32_t value){for(unsigned n=0;n<32;n+=8)u8(static_cast<std::uint8_t>(value>>n));};
    const auto u64=[&](std::uint64_t value){for(unsigned n=0;n<64;n+=8)u8(static_cast<std::uint8_t>(value>>n));};
    u16(3); // controller codec v3
    u8(0);u8(0);u8(0);u64(0);u8(0);u8(0);u32(0); // empty Sleep state
    i16(1);u64(2);u64(7);u64(3);u64(1);u64(0);u64(0);
    u8(0);u8(0);u32(1); // Interaction actor/clock, then one action
    u64(1);u64(8); // native identity/generation
    u8(static_cast<std::uint8_t>(odh::routine_key(4096).scope));u16(4096);
    i16(1);i16(2);i16(2);u32(22); // call context
    u32(0);u8(static_cast<std::uint8_t>(odh::VmCallReturnMode::interaction_next_tick));
    u16(4);i16(50);u8(static_cast<std::uint8_t>(odh::InteractionQueueMode::normal));
    u32(1U<<28);u8(0); // queue UID, priority/mode/flags, no notification
    u8(1);u8(static_cast<std::uint8_t>(odh::routine_key(4098).scope));u16(4098); // check key
    u8(0);u8(0);u8(0);u8(0);u32(0); // no active/wait/completion/interrupt/diagnostic
    u8(static_cast<std::uint8_t>(odh::InteractionQueueProfile::ts1_priority));u16(5);
    u8(0); // thread interrupt
    return bytes;
}
std::vector<std::uint8_t> legacy_v4_checked_queue_fixture() {
    std::vector<std::uint8_t> bytes;
    const auto u8=[&](std::uint8_t value){bytes.push_back(value);};
    const auto u16=[&](std::uint16_t value){u8(static_cast<std::uint8_t>(value));u8(static_cast<std::uint8_t>(value>>8));};
    const auto i16=[&](std::int16_t value){u16(static_cast<std::uint16_t>(value));};
    const auto u32=[&](std::uint32_t value){for(unsigned n=0;n<32;n+=8)u8(static_cast<std::uint8_t>(value>>n));};
    const auto u64=[&](std::uint64_t value){for(unsigned n=0;n<64;n+=8)u8(static_cast<std::uint8_t>(value>>n));};
    u16(4); // controller codec v4
    u8(0);u8(0);u8(0);u64(0);u8(0);u8(0);u32(0); // empty Sleep state
    i16(1);u64(2);u64(7);u64(3);u64(1);u64(0);u64(0);
    u8(0);u8(0);u32(1); // Interaction actor/clock, then one action
    u64(1);u64(8);
    u8(static_cast<std::uint8_t>(odh::routine_key(4096).scope));u16(4096);
    i16(1);i16(2);i16(2);u32(22);
    u32(0);u8(static_cast<std::uint8_t>(odh::VmCallReturnMode::interaction_next_tick));
    u16(4);i16(50);u8(static_cast<std::uint8_t>(odh::InteractionQueueMode::normal));
    u32(1U<<28);u8(0);
    u8(1);u8(static_cast<std::uint8_t>(odh::routine_key(4098).scope));u16(4098);u8(0);
    u8(0);u8(0);u8(0);u8(0);u32(0);
    u8(static_cast<std::uint8_t>(odh::InteractionQueueProfile::ts1_priority));u16(5);
    u8(0);
    return bytes;
}
void same(const odh::EntityThreadControllersSnapshot& left,
          const odh::EntityThreadControllersSnapshot& right) {
    const auto& a = left.sleep; const auto& b = right.sleep;
    require(a.actor.has_value() == b.actor.has_value() && a.job.has_value() == b.job.has_value() &&
        a.last_tick == b.last_tick && a.clock == b.clock && a.interrupted == b.interrupted &&
        a.notify_resume == b.notify_resume && a.diagnostic == b.diagnostic &&
        left.schedule_idle_start==right.schedule_idle_start,"Sleep/controller fields round-trip");
    if (a.actor) require(a.actor->id == b.actor->id && a.actor->generation == b.actor->generation &&
        a.actor->run_generation == b.actor->run_generation, "Sleep actor round-trips");
    if (a.job) require(a.job->ticket == b.job->ticket && a.job->idle_start == b.job->idle_start &&
        a.job->deadline == b.job->deadline && a.job->argument == b.job->argument &&
        a.job->next_evaluation==b.job->next_evaluation, "Sleep job round-trips");
    const auto& x = left.interactions; const auto& y = right.interactions;
    require(x.actor_id == y.actor_id && x.capacity == y.capacity &&
        x.actor_generation == y.actor_generation && x.run_generation == y.run_generation &&
        x.sequence == y.sequence && x.clock == y.clock && x.idle_start == y.idle_start &&
        x.last_tick == y.last_tick && x.resume_tick == y.resume_tick && x.actions.size() == y.actions.size() &&
        x.active.has_value() == y.active.has_value() && x.wait.has_value() == y.wait.has_value() &&
        x.completion.has_value() == y.completion.has_value() && x.interrupted == y.interrupted &&
        x.diagnostic == y.diagnostic && x.profile == y.profile && x.next_queue_uid == y.next_queue_uid &&
        x.suspended_active.size()==y.suspended_active.size() &&
        left.thread_interrupt == right.thread_interrupt,
        "interaction controller fields round-trip");
    for(std::size_t i=0;i<x.suspended_active.size();++i)
        require(x.suspended_active[i].uid==y.suspended_active[i].uid&&
            x.suspended_active[i].ticket==y.suspended_active[i].ticket,
            "suspended parent prefix round-trips");
    for (std::size_t i = 0; i < x.actions.size(); ++i) {
        const auto& p = x.actions[i]; const auto& q = y.actions[i];
        require(p.uid == q.uid && p.target_generation == q.target_generation &&
            p.call.key.scope == q.call.key.scope && p.call.key.id == q.call.key.id &&
            p.call.context.caller == q.call.context.caller && p.call.context.callee == q.call.context.callee &&
            p.call.context.stack_object == q.call.context.stack_object &&
            p.call.context.code_owner == q.call.context.code_owner && p.call.arguments == q.call.arguments &&
            p.call.return_mode == q.call.return_mode&&p.call.exact_arguments==q.call.exact_arguments&&
            p.argument_mode==q.argument_mode&&p.check_context==q.check_context&&p.queue_uid == q.queue_uid &&
            p.priority == q.priority && p.mode == q.mode && p.flags == q.flags &&
            p.notify_idle == q.notify_idle && p.check.has_value() == q.check.has_value() &&
            (!p.check || (p.check->id == q.check->id && p.check->scope == q.check->scope)) &&
            p.check_permissions == q.check_permissions&&p.interaction_number==q.interaction_number, "queued call fields round-trip");
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
        require(encoded[0]==5&&encoded[1]==0,"legacy schedule keeps controller wire version5");
        require(odh::encode_entity_thread_controllers_snapshot(
            odh::decode_entity_thread_controllers_snapshot(encoded))==encoded,
            "legacy schedule reencodes byte-identically");
        {
            auto labeled=state;
            auto& action=labeled.interactions.actions.front();
            action.label=odh::InteractionLabel{"Voltar para casa",{odh::InteractionLabelKind::script_string,
                14,"GameData/Objects/Objects.far","PhonePay.iff",302,2}};
            const auto bytes=odh::encode_entity_thread_controllers_snapshot(labeled);
            const auto restored=odh::decode_entity_thread_controllers_snapshot(bytes);
            const auto& saved=restored.interactions.actions.front();
            require(bytes[0]==14&&saved.label&&saved.label->text=="Voltar para casa"&&saved.label->origin.language==14&&
                saved.label->origin.kind==odh::InteractionLabelKind::script_string&&saved.label->origin.table==302&&
                saved.label->origin.index==2&&saved.label->origin.asset=="PhonePay.iff"&&
                saved.label->origin.resource=="GameData/Objects/Objects.far"&&saved.call.arguments==action.call.arguments&&
                !restored.interactions.actions.back().label&&odh::encode_entity_thread_controllers_snapshot(restored)==bytes,
                "chosen localized action label and original call survive reopening independently of another queued action");
            action.label->origin.language=0;
            rejects([&]{odh::encode_entity_thread_controllers_snapshot(labeled);},"unidentified source language rejected");
            action.label->origin.language=14;action.label->origin.resource="../PhonePay.iff";
            rejects([&]{odh::encode_entity_thread_controllers_snapshot(labeled);},"label resource traversal rejected");
            action.label->origin.resource="GameData/Objects/Objects.far";action.label->text.assign(2049,'x');
            rejects([&]{odh::encode_entity_thread_controllers_snapshot(labeled);},"oversize label rejected");
        }
        {
            auto named=state;named.interactions.actions.front().call.key=odh::named_routine_key(400,false);
            named.interactions.actions.front().check=odh::named_routine_key(0,true);
            const auto bytes=odh::encode_entity_thread_controllers_snapshot(named);
            const auto restored=odh::decode_entity_thread_controllers_snapshot(bytes);
            require(bytes[0]==13&&bytes[1]==0&&restored.interactions.actions.front().call.key.scope==odh::RoutineScope::named_private&&
                restored.interactions.actions.front().call.key.id==400&&restored.interactions.actions.front().check->id==0&&
                odh::encode_entity_thread_controllers_snapshot(restored)==bytes,
                "controller v13 retains physical named scope and exact ushort IDs including zero");
            auto legacy=bytes;legacy[0]=12;
            rejects([&]{(void)odh::decode_entity_thread_controllers_snapshot(legacy);},"legacy controller wire cannot silently reinterpret named entry scopes");
        }
        {
            auto automatic=state;auto& action=automatic.interactions.actions.front();
            action.argument_mode=odh::InteractionArgumentMode::source_explicit;action.call.exact_arguments=true;
            action.call.arguments={19,0,0,0};action.check_permissions=true;action.check_context=odh::InteractionCheckContext::autonomous;
            const auto bytes=odh::encode_entity_thread_controllers_snapshot(automatic);
            const auto restored=odh::decode_entity_thread_controllers_snapshot(bytes);
            require(bytes[0]==11&&bytes[1]==0&&restored.interactions.actions.front().check_context==odh::InteractionCheckContext::autonomous&&
                restored.interactions.actions.front().call.arguments==std::vector<std::int16_t>{19,0,0,0},"controller11 persists automatic eligibility independently of authored action arguments");
            same(automatic,restored);require(odh::encode_entity_thread_controllers_snapshot(restored)==bytes,"automatic eligibility controller archive reencodes identically");
            for(std::size_t size=0;size<bytes.size();++size){auto prefix=bytes;prefix.resize(size);
                rejects([&]{(void)odh::decode_entity_thread_controllers_snapshot(prefix);},"every truncated automatic eligibility controller archive rejects");}
            auto old=automatic;old.interactions.actions.front().check_context=odh::InteractionCheckContext::default_zero;
            const auto legacy=odh::encode_entity_thread_controllers_snapshot(old);const auto old_restored=odh::decode_entity_thread_controllers_snapshot(legacy);
            require(legacy[0]==10&&old_restored.interactions.actions.front().check_context==odh::InteractionCheckContext::default_zero&&
                odh::encode_entity_thread_controllers_snapshot(old_restored)==legacy,"older exact-argument saves retain zero check context and identical bytes");
            auto single=automatic;single.interactions.actions.resize(1);single.interactions.active.reset();
            single.interactions.suspended_active.clear();single.interactions.wait.reset();single.interactions.completion.reset();single.interactions.resume_tick.reset();
            single.interactions.diagnostic.clear();single.schedule_idle_start.reset();
            auto zero_context=odh::encode_entity_thread_controllers_snapshot(single);
            // Last action context precedes the fixed empty interaction tail (17 bytes).
            require(zero_context.size()>18&&zero_context[zero_context.size()-18]==1,"single-action v11 eligibility wire marker located");
            zero_context[zero_context.size()-18]=0;
            rejects([&]{(void)odh::decode_entity_thread_controllers_snapshot(zero_context);},"v11 with only default contexts cannot silently downgrade to v10");
            zero_context[zero_context.size()-18]=2;
            rejects([&]{(void)odh::decode_entity_thread_controllers_snapshot(zero_context);},"unknown wire eligibility context rejects");
            auto invalid=automatic;invalid.interactions.actions.front().check_context=static_cast<odh::InteractionCheckContext>(2);
            rejects([&]{(void)odh::encode_entity_thread_controllers_snapshot(invalid);},"unknown eligibility context rejects");
            invalid=automatic;invalid.interactions.actions.front().check_permissions=false;
            rejects([&]{(void)odh::encode_entity_thread_controllers_snapshot(invalid);},"automatic eligibility cannot silently disable original permissions");
            invalid=automatic;invalid.interactions.actions.front().call.arguments.pop_back();
            rejects([&]{(void)odh::encode_entity_thread_controllers_snapshot(invalid);},"automatic producer retains original four-argument action extent");
        }
        {
            for(const auto mode:{odh::InteractionArgumentMode::source_null,odh::InteractionArgumentMode::source_explicit}){
                auto exact=state;auto& action=exact.interactions.actions.front();action.argument_mode=mode;
                action.call.exact_arguments=true;action.call.arguments=mode==odh::InteractionArgumentMode::source_null?
                    std::vector<std::int16_t>(4,0):std::vector<std::int16_t>{};
                const auto bytes=odh::encode_entity_thread_controllers_snapshot(exact);
                const auto restored=odh::decode_entity_thread_controllers_snapshot(bytes);
                require(bytes.front()==10&&restored.interactions.actions.front().argument_mode==mode&&
                    restored.interactions.actions.front().call.arguments==action.call.arguments&&
                    !restored.interactions.actions.back().call.exact_arguments,
                    "conditional controller10 preserves source null/explicit Args alongside legacy queued calls");
                same(exact,restored);
                require(odh::encode_entity_thread_controllers_snapshot(restored)==bytes,
                    "exact source action controller archive reencodes canonically");
                for(std::size_t size=0;size<bytes.size();++size){auto truncated=bytes;truncated.resize(size);
                    rejects([&]{(void)odh::decode_entity_thread_controllers_snapshot(truncated);},
                        "every truncated source Args controller archive rejected");}
                auto broken=bytes;broken[0]=12;
                rejects([&]{(void)odh::decode_entity_thread_controllers_snapshot(broken);},
                    "future version beyond exact source Args rejected");
                auto invalid=exact;invalid.interactions.actions.front().call.exact_arguments=false;
                rejects([&]{(void)odh::encode_entity_thread_controllers_snapshot(invalid);},
                    "source Args mode cannot hide a padded call extent");
                invalid=exact;invalid.interactions.actions.front().argument_mode=odh::InteractionArgumentMode::legacy_metadata;
                rejects([&]{(void)odh::encode_entity_thread_controllers_snapshot(invalid);},
                    "exact action extent requires explicit source Args provenance");
            }
            auto null_args=state;auto& action=null_args.interactions.actions.front();
            action.argument_mode=odh::InteractionArgumentMode::source_null;action.call.exact_arguments=true;action.call.arguments={};
            rejects([&]{(void)odh::encode_entity_thread_controllers_snapshot(null_args);},
                "source null Args requires four explicit zeros in the normalized call");
            action.call.arguments={0,0,7,0};
            rejects([&]{(void)odh::encode_entity_thread_controllers_snapshot(null_args);},
                "source null Args cannot fabricate nonzero action parameters");
            action.argument_mode=odh::InteractionArgumentMode::source_explicit;
            const auto explicit_bytes=odh::encode_entity_thread_controllers_snapshot(null_args);
            require(odh::decode_entity_thread_controllers_snapshot(explicit_bytes).interactions.actions.front().call.arguments==
                std::vector<std::int16_t>({0,0,7,0}),"nonnull source Args retains exact provided values");
            require(odh::encode_entity_thread_controllers_snapshot(state)==encoded,
                "absent source Args modes preserve existing controller version and bytes");
        }
        {
            for(const std::int32_t number:{std::int32_t{0},std::int32_t{255},std::int32_t{65535},
                    std::int32_t{-2147483647-1},std::int32_t{2147483647},std::int32_t{-1}}){
                auto numbered=state;numbered.interactions.actions.front().interaction_number=number;
                const auto bytes=odh::encode_entity_thread_controllers_snapshot(numbered);
                const auto restored=odh::decode_entity_thread_controllers_snapshot(bytes);
                require(bytes[0]==9&&restored.interactions.actions.front().interaction_number==number&&
                    !restored.interactions.actions.back().interaction_number,
                    "conditional controller v9 preserves full signed32 provenance and unknown neighboring action");
                same(numbered,restored);
                require(odh::encode_entity_thread_controllers_snapshot(restored)==bytes,
                    "interaction-number controller extension reencodes byte-identically");
                numbered.interactions.actions.front().interaction_number.reset();
                require(odh::encode_entity_thread_controllers_snapshot(numbered)==encoded,
                    "absent interaction-number metadata leaves original controller bytes unchanged");
            }
            auto numbered=state;numbered.interactions.actions.front().interaction_number=3;
            auto bytes=odh::encode_entity_thread_controllers_snapshot(numbered);
            for(std::size_t size=0;size<bytes.size();++size){auto truncated=bytes;truncated.resize(size);
                rejects([&]{(void)odh::decode_entity_thread_controllers_snapshot(truncated);},
                    "every truncated v9 interaction provenance snapshot rejected");}
            auto broken=bytes;broken[0]=11;
            rejects([&]{(void)odh::decode_entity_thread_controllers_snapshot(broken);},
                "future controller version beyond provenance extension rejected");
            broken=bytes;broken.push_back(0);
            rejects([&]{(void)odh::decode_entity_thread_controllers_snapshot(broken);},
                "v9 interaction provenance trailing bytes rejected");
        }
        {
            auto scheduled=state;
            scheduled.sleep.job->idle_start=scheduled.sleep.clock;
            scheduled.sleep.job->next_evaluation=scheduled.sleep.clock+1;
            const auto scheduled_bytes=odh::encode_entity_thread_controllers_snapshot(scheduled);
            require(scheduled_bytes[0]==6&&scheduled_bytes.size()==encoded.size()+9,
                "only scheduled override upgrades codec and adds optional timestamp");
            same(scheduled,odh::decode_entity_thread_controllers_snapshot(scheduled_bytes));
            auto bad=scheduled;
            bad.sleep.job->next_evaluation=bad.sleep.clock;
            rejects([&]{odh::encode_entity_thread_controllers_snapshot(bad);},"elapsed override rejected");
            bad=scheduled;bad.sleep.job->next_evaluation=bad.sleep.job->deadline;
            rejects([&]{odh::encode_entity_thread_controllers_snapshot(bad);},"redundant natural override rejected");
            bad=scheduled;bad.sleep.job->idle_start--;
            rejects([&]{odh::encode_entity_thread_controllers_snapshot(bad);},"override must match scheduled next tick");
            auto broken=scheduled_bytes;broken[54]=2;
            rejects([&]{odh::decode_entity_thread_controllers_snapshot(broken);},"override boolean must be canonical");
            broken=scheduled_bytes;broken[0]=5;
            rejects([&]{odh::decode_entity_thread_controllers_snapshot(broken);},"legacy version cannot consume override bytes");
        }
        {
            auto active_clock=empty_fixture();active_clock.schedule_idle_start=0;
            auto bytes=odh::encode_entity_thread_controllers_snapshot(active_clock);
            require(bytes[0]==7&&bytes.size()==odh::encode_entity_thread_controllers_snapshot(empty_fixture()).size()+9,
                "present zero ScheduleIdleStart selects v7 and appends its value");
            same(active_clock,odh::decode_entity_thread_controllers_snapshot(bytes));
            active_clock.sleep.clock=40;active_clock.sleep.last_tick=40;
            active_clock.interactions.clock=40;active_clock.interactions.last_tick=40;
            active_clock.schedule_idle_start=12;
            same(active_clock,odh::decode_entity_thread_controllers_snapshot(
                odh::encode_entity_thread_controllers_snapshot(active_clock)));
            auto invalid_clock=active_clock;invalid_clock.schedule_idle_start=41;
            rejects([&]{(void)odh::encode_entity_thread_controllers_snapshot(invalid_clock);},
                "shared idle clock beyond service clock rejected");
            invalid_clock=active_clock;invalid_clock.interactions.clock=39;invalid_clock.interactions.last_tick=39;
            rejects([&]{(void)odh::encode_entity_thread_controllers_snapshot(invalid_clock);},
                "shared idle clock requires service clock equality");
            invalid_clock=active_clock;invalid_clock.schedule_idle_start=0x100000000ULL;
            rejects([&]{(void)odh::encode_entity_thread_controllers_snapshot(invalid_clock);},
                "shared idle clock bounded to source uint32 domain");
            bytes=odh::encode_entity_thread_controllers_snapshot(active_clock);
            auto broken=bytes;broken.back()=0xff;
            rejects([&]{(void)odh::decode_entity_thread_controllers_snapshot(broken);},
                "decoded shared idle clock beyond service clock rejected");
            broken=bytes;broken[broken.size()-9]=2;
            rejects([&]{(void)odh::decode_entity_thread_controllers_snapshot(broken);},
                "v7 clock presence boolean must be canonical");
            broken=bytes;broken.pop_back();
            rejects([&]{(void)odh::decode_entity_thread_controllers_snapshot(broken);},
                "truncated v7 shared clock rejected");
        }
        {
            auto immediate=state;
            immediate.interactions.actions.front().flags=4U;
            immediate.interactions.actions.front().call.return_mode=odh::VmCallReturnMode::immediate;
            const auto bytes=odh::encode_entity_thread_controllers_snapshot(immediate);
            require(bytes[0]==7&&!immediate.schedule_idle_start,
                "queued RunImmediately action selects v7 without requiring a clock");
            same(immediate,odh::decode_entity_thread_controllers_snapshot(bytes));
            auto broken=bytes;broken[0]=6;
            rejects([&]{(void)odh::decode_entity_thread_controllers_snapshot(broken);},
                "v1-v6 readers reject immediate call mode");
            for(std::size_t size=0;size<bytes.size();++size) {
                auto truncated=bytes;truncated.resize(size);
                rejects([&]{(void)odh::decode_entity_thread_controllers_snapshot(truncated);},
                    "every truncated v7 immediate snapshot rejected");
            }
            broken=bytes;broken.push_back(0);
            rejects([&]{(void)odh::decode_entity_thread_controllers_snapshot(broken);},
                "v7 trailing bytes rejected");
            broken=bytes;broken[0]=8;
            rejects([&]{(void)odh::decode_entity_thread_controllers_snapshot(broken);},
                "unsupported future controller version rejected");
            auto invalid_action=immediate;
            invalid_action.interactions.actions.front().call.return_mode=odh::VmCallReturnMode::interaction_next_tick;
            rejects([&]{(void)odh::encode_entity_thread_controllers_snapshot(invalid_action);},
                "RunImmediately flag requires immediate call mode");
            invalid_action=state;
            invalid_action.interactions.actions.front().call.return_mode=odh::VmCallReturnMode::immediate;
            rejects([&]{(void)odh::encode_entity_thread_controllers_snapshot(invalid_action);},
                "immediate call mode requires RunImmediately flag");
            auto empty_v7=odh::encode_entity_thread_controllers_snapshot(empty_fixture());
            empty_v7[0]=7;empty_v7.push_back(0);
            rejects([&]{(void)odh::decode_entity_thread_controllers_snapshot(empty_v7);},
                "empty v7 extension is noncanonical");
        }
        same(state, odh::decode_entity_thread_controllers_snapshot(encoded));
        auto nested=state;
        nested.interactions.suspended_active={{1,6}};
        nested.interactions.active=odh::InteractionActiveSnapshot{2,7};
        const auto nested_bytes=odh::encode_entity_thread_controllers_snapshot(nested);
        same(nested,odh::decode_entity_thread_controllers_snapshot(nested_bytes));
        auto idle_retry=state;
        idle_retry.interactions.idle_start=4;
        const auto idle_retry_bytes=odh::encode_entity_thread_controllers_snapshot(idle_retry);
        same(idle_retry,odh::decode_entity_thread_controllers_snapshot(idle_retry_bytes));
        auto nested_resume=nested;
        nested_resume.interactions.actions.pop_back();
        nested_resume.interactions.suspended_active.clear();
        nested_resume.interactions.active=odh::InteractionActiveSnapshot{1,6};
        nested_resume.interactions.completion=odh::InteractionCompletion{2,7,5,true};
        nested_resume.interactions.resume_tick=6;
        const auto nested_resume_bytes=odh::encode_entity_thread_controllers_snapshot(nested_resume);
        same(nested_resume,odh::decode_entity_thread_controllers_snapshot(nested_resume_bytes));
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
        changed[0] = 8;
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
                !legacy_queued.interactions.actions.front().check&&
                !legacy_queued.interactions.actions.front().check_permissions,
                "legacy v1 queued actions retain FIFO identity without invented TS1 metadata");
        const auto migrated=odh::decode_entity_thread_controllers_snapshot(
            odh::encode_entity_thread_controllers_snapshot(legacy));
        require(migrated.interactions.profile==odh::InteractionQueueProfile::legacy_fifo,
                "resaving a legacy queue does not invent TS1 priority state");
        const auto v2=odh::decode_entity_thread_controllers_snapshot(legacy_v2_queue_fixture());
        require(v2.interactions.profile==odh::InteractionQueueProfile::ts1_priority&&
                v2.interactions.actions.size()==1&&v2.interactions.actions.front().priority==50&&
                !v2.interactions.actions.front().check&&
                !v2.interactions.actions.front().check_permissions,
                "legacy v2 queues retain priority metadata and gain no invented check routine");
        const auto v2_resaved=odh::decode_entity_thread_controllers_snapshot(
            odh::encode_entity_thread_controllers_snapshot(v2));
        require(v2_resaved.interactions.actions.front().priority==50&&
                !v2_resaved.interactions.actions.front().check&&
                !v2_resaved.interactions.actions.front().check_permissions,
                "resaving v2 queue state preserves its no-check meaning");
        const auto v3=odh::decode_entity_thread_controllers_snapshot(legacy_v3_checked_queue_fixture());
        require(v3.interactions.actions.size()==1&&v3.interactions.actions.front().check&&
                v3.interactions.actions.front().check->id==4098&&
                v3.interactions.actions.front().check->scope==odh::routine_key(4098).scope&&
                !v3.interactions.actions.front().check_permissions,
                "legacy v3 check routine is retained with permission checks disabled");
        const auto v4=odh::decode_entity_thread_controllers_snapshot(legacy_v4_checked_queue_fixture());
        require(v4.interactions.actions.size()==1&&v4.interactions.actions.front().check&&
                v4.interactions.actions.front().check->id==4098&&
                v4.interactions.actions.front().check->scope==odh::routine_key(4098).scope&&
                !v4.interactions.actions.front().check_permissions&&v4.interactions.suspended_active.empty(),
                "legacy v4 routine check survives with an empty nested prefix");
        require(legacy.interactions.suspended_active.empty()&&legacy_queued.interactions.suspended_active.empty()&&
                v2.interactions.suspended_active.empty()&&v3.interactions.suspended_active.empty(),
                "legacy controller codecs default to a flat interaction prefix");

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
        invalid=nested;
        invalid.interactions.suspended_active[0].uid=2;
        rejects([&]{(void)odh::encode_entity_thread_controllers_snapshot(invalid);},
            "suspended parent UID mismatch rejected");
        invalid=nested;
        invalid.interactions.suspended_active[0].ticket=8;
        rejects([&]{(void)odh::encode_entity_thread_controllers_snapshot(invalid);},
            "parent ticket after child rejected");
        invalid=nested;
        invalid.interactions.active.reset();
        rejects([&]{(void)odh::encode_entity_thread_controllers_snapshot(invalid);},
            "suspended parent without active child rejected");

        auto modern=state;
        for(auto& action:modern.interactions.actions){auto& context=action.call.context;
            context.entity_references_bound=true;context.caller_generation=modern.interactions.actor_generation;
            context.callee_generation=action.target_generation;context.stack_object_generation=action.target_generation;
            action.call.action_tree_override=true;
        }
        const auto modern_bytes=odh::encode_entity_thread_controllers_snapshot(modern);
        require(modern_bytes[0]==12&&modern_bytes[1]==0,"bound queued references select controller wire12");
        const auto modern_copy=odh::decode_entity_thread_controllers_snapshot(modern_bytes);
        require(modern_copy.interactions.actions[0].call.context.entity_references_bound&&
            modern_copy.interactions.actions[0].call.context.callee_generation==modern.interactions.actions[0].target_generation&&
            modern_copy.interactions.actions[0].call.action_tree_override==std::optional<bool>{true},"controller wire12 preserves body and action-tree intent");
        invalid=modern;++invalid.interactions.actions[0].call.context.callee_generation;
        rejects([&]{(void)odh::encode_entity_thread_controllers_snapshot(invalid);},"hybrid callee generations rejected");
        invalid=modern;invalid.interactions.actions[0].call.context.entity_references_bound=false;
        rejects([&]{(void)odh::encode_entity_thread_controllers_snapshot(invalid);},"unbound context cannot smuggle generation tags");
        std::cout << checks << " thread controller snapshot codec checks passed (synthetic values only)\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
