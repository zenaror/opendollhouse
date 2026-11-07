#include "odh/read_only_bhav.hpp"
#include "odh/world_snapshot_codec.hpp"
#include <iostream>
#include <algorithm>

namespace {
int checks = 0;
void require(bool value, const char* message) {
    ++checks;
    if (!value) throw std::runtime_error(message);
}
odh::RoutineKey key(std::uint16_t id) { return odh::routine_key(id); }
odh::Instruction expression(std::uint8_t scope, std::int16_t index, std::int16_t value,
                            std::uint8_t operation = 2, std::uint8_t yes = 254,
                            std::uint8_t rhs_scope = 7) {
    return {2, yes, 255, {static_cast<std::uint8_t>(index),
        static_cast<std::uint8_t>(static_cast<std::uint16_t>(index) >> 8),
        static_cast<std::uint8_t>(value),
        static_cast<std::uint8_t>(static_cast<std::uint16_t>(value) >> 8),
        0, operation, scope, rhs_scope}};
}
odh::Instruction call(std::uint16_t id, std::uint8_t yes = 254) { return {id, yes, 255, {}}; }
odh::Bhav routine(std::initializer_list<odh::Instruction> code) {
    return {0x8002, code, {}, odh::Bhav::Metadata{0, 4, 4, 0}};
}
}
int main() {
    try {
        using State = odh::ReadOnlyBhavState;
        using Reason = odh::ReadOnlyBhavReason;
        odh::World source;
        require(source.create(1, odh::EntityKind::avatar, {2, 66, 0, 1}) &&
                source.create(2, odh::EntityKind::object, {2}) && source.define_type(2, 0x12345678),
                "synthetic source entities");
        odh::VmContext context{1, 1, 2, 55};
        odh::VmFrame frame{key(4096), 0, context, {}, {}};
        require(source.write(frame, 0, 0, 321) && source.write(frame, 1, 0, -123) &&
                source.set_person_data(1, 31, -456) && source.set_motive(1, 0, -89) &&
                source.declare_object_word(2, 78, -789) && source.declare_definition_word(2, 9, 42),
                "distinct exact signed memory values");
        const auto snapshot = source.snapshot();
        const auto before = odh::encode_world_snapshot(snapshot);
        const auto run = [&](const odh::RoutineSet& routines, const odh::RoutineOwners& owners,
                             odh::RoutineKey root, odh::VmContext ctx, std::uint64_t budget,
                             const std::vector<std::int16_t>& args = {}, odh::ReadOnlyBhavLimits limits = {}) {
            auto result = odh::evaluate_read_only_bhav(snapshot, routines, owners, root, ctx, args, budget, limits);
            require(odh::encode_world_snapshot(snapshot) == before &&
                    odh::encode_world_snapshot(source.snapshot()) == before,
                    "input snapshot and live original remain byte-exact");
            return result;
        };
        {
            const auto entry=odh::named_routine_key(0,false);
            odh::RoutineOwners named{{55,{{entry,routine({expression(7,0,0)})}}}};
            auto result=run({},named,entry,context,1);
            require(result.state==State::evaluated_true,"read-only admission accepts an authenticated physical named entry without remapping ID0 to global code");
            named.at(55).at(entry)=routine({expression(4,78,999,5)});
            result=run({},named,entry,context,1);
            require(result.state==State::unsupported&&result.reason==Reason::vm_fault,
                "named syntax support never authorizes writes to read-only World state");
        }
        const auto no = routine({expression(7, 0, 1)});
        odh::RoutineSet default_bank{{key(256), routine({call(8192)})},
            {key(4096), no}, {key(4097), no}, {key(8192), no}};
        auto child_call = call(4097, 1); child_call.operands[0] = 37;
        odh::RoutineSet owned{{key(4096), routine({child_call, call(256)})},
            {key(4097), routine({expression(9, 0, 37)})},
            {key(8192), routine({expression(1, 0, -123)})}};
        odh::RoutineOwners owners{{55, owned}, {2, {{key(4096), no}, {key(4097), no}, {key(8192), no}}}};
        auto result = run(default_bank, owners, key(4096), context, 5);
        require(result.state == State::evaluated_true && result.reason == Reason::completed && result.steps == 5,
                "private root/child, global and semiglobal preserve explicit owner unrelated to entity ID");
        require(run(default_bank, owners, key(4096), {1, 1, 2, 2}, 10).state == State::evaluated_false,
                "same root ID resolves different explicit owner");
        require(run(default_bank, owners, key(4096), {1, 1, 2, 0}, 10).state == State::evaluated_false,
                "owner zero explicitly uses default bank");
        result = run(default_bank, owners, key(4096), context, 4);
        require(result.state == State::unsupported && result.reason == Reason::budget_exhausted && result.steps == 4,
                "nested call shares instruction budget");
        for (const auto& test : std::vector<std::array<std::int16_t, 3>>{
                 {0, 0, 321}, {1, 0, -123}, {18, 31, -456}, {14, 0, -89}, {4, 78, -789}, {21, 9, 42}}) {
            auto bank = odh::RoutineSet{{key(4096), routine({expression(static_cast<std::uint8_t>(test[0]), test[1], test[2])})}};
            require(run(bank, {}, key(4096), {1, 1, 2, 0}, 1).state == State::evaluated_true,
                    "World read observes exact restored scratch values");
        }
        auto changed_snapshot = snapshot;
        changed_snapshot.entities[1].attributes[0] = -222;
        const auto changed_before = odh::encode_world_snapshot(changed_snapshot);
        const odh::RoutineSet changed_bank{{key(4096), routine({expression(1, 0, -222)})}};
        require(odh::evaluate_read_only_bhav(changed_snapshot, changed_bank, {}, key(4096),
                    {1, 1, 2, 0}, {}, 1).state == State::evaluated_true &&
                source.find(2)->attributes[0] == -123 &&
                odh::encode_world_snapshot(changed_snapshot) == changed_before,
                "scratch reads supplied snapshot independently of differing live World");
        require(run(default_bank, owners, key(256), context, 2).state == State::evaluated_true,
                "global root retains supplied owner when calling semiglobal");
        auto type_test = call(32); type_test.operands = {0x78, 0x56, 0x34, 0x12, 0, 0, 10, 0};
        odh::RoutineSet bank{{key(4096), routine({type_test})}};
        require(run(bank, {}, key(4096), {1, 1, 2, 0}, 1).state == State::evaluated_true,
                "opcode32 reads scratch object identity");
        type_test.operands[0] = 0;
        bank[key(4096)] = routine({type_test});
        require(run(bank, {}, key(4096), {1, 1, 2, 0}, 1).state == State::evaluated_false,
                "opcode32 false distinct from unsupported");
        for (auto scope : {0, 1, 3, 4, 14, 15, 18, 19}) {
            // All are writable World scopes in the live adapter. Scratch rejects them.
            const auto index = (scope == 3 || scope == 4) ? 78 : 0;
            bank[key(4096)] = routine({expression(static_cast<std::uint8_t>(scope), static_cast<std::int16_t>(index), 999, 5)});
            result = run(bank, {}, key(4096), {1, 1, 2, 0}, 1);
            require(result.state == State::unsupported && result.reason == Reason::vm_fault &&
                    result.diagnostic.find("unsupported write scope") != std::string::npos,
                    "World assignment rejected before original can change");
        }
        for (auto opcode : {0, 17, 20, 255}) {
            bank[key(4096)] = routine({call(static_cast<std::uint16_t>(opcode))});
            result = run(bank, {}, key(4096), {1, 1, 2, 0}, 1);
            require(result.state == State::unsupported && result.reason == Reason::vm_fault,
                    "WAIT and external primitives are never dispatched to services");
        }
        bank[key(4096)] = routine({expression(8, 0, 77, 5, 1), expression(8, 0, 77)});
        require(run(bank, {}, key(4096), {1, 1, 2, 0}, 2).state == State::evaluated_true,
                "VM-local writes remain usable");
        const std::array<std::uint8_t,19> compatibility_modes{0,3,7,8,9,12,13,16,22,23,25,34,35,36,37,38,39,40,41};
        for(unsigned mode=0;mode<256;++mode) {
            // Generic12 leaves Temp0 intact; Generic13 does not abort this VM.
            // All operands after the selector are ignored by this finite profile.
            const odh::Instruction generic{1,2,255,{static_cast<std::uint8_t>(mode),255,128,42,7,99,1,200}};
            bank[key(4096)]=routine({expression(8,0,1234,5,1),generic,expression(8,0,1234)});
            const auto compatible=std::find(compatibility_modes.begin(),compatibility_modes.end(),mode)!=compatibility_modes.end();
            const auto evaluated=run(bank,{},key(4096),{1,1,2,0},3);
            if(mode==14)require(evaluated.state==State::unsupported&&evaluated.reason==Reason::vm_fault,
                "stateful radio setter cannot masquerade as no-effect in read-only discovery");
            require(compatible?(evaluated.state==State::evaluated_true&&evaluated.steps==3):
                (evaluated.state==State::unsupported&&evaluated.reason==Reason::vm_fault),
                "read-only GenericTS1 finite modes preserve Temp0/World and continue; all services and unknown selectors remain unsupported");
        }
        bank[key(4096)] = routine({expression(9, 0, -222)});
        require(run(bank, {}, key(4096), {1, 1, 2, 0}, 1, {-222}).state == State::evaluated_true,
                "explicit root arguments are supplied");
        result = run(bank, {}, key(4096), {1, 1, 2, 0}, 0);
        require(result.reason == Reason::budget_exhausted && result.steps == 0, "zero budget executes nothing");
        bank[key(4096)] = routine({expression(7, 0, 0, 2, 0)});
        result = run(bank, {}, key(4096), {1, 1, 2, 0}, 7);
        require(result.reason == Reason::budget_exhausted && result.steps == 7, "loop is bounded");
        result = run(default_bank, owners, key(4096), {1, 1, 2, 99}, 10);
        require(result.reason == Reason::start_failed && result.steps == 0 &&
                result.diagnostic.find("code owner not supplied: 99") != std::string::npos,
                "absent owner distinguished from budget and false");
        auto missing = owners; missing[55].erase(key(4097));
        result = run(default_bank, missing, key(4096), context, 10);
        require(result.reason == Reason::vm_fault && result.diagnostic.find("code owner 55") != std::string::npos,
                "missing private routine never falls back to default");
        missing = owners; missing[55].erase(key(8192));
        require(run(default_bank, missing, key(4096), context, 10).reason == Reason::vm_fault,
                "missing semiglobal never falls back to default");
        auto invalid = snapshot; ++invalid.word_count;
        require(odh::evaluate_read_only_bhav(invalid, bank, {}, key(4096), {}, {}, 1).reason == Reason::invalid_snapshot,
                "invalid snapshot rejected");
        odh::ReadOnlyBhavLimits limits; limits.world.entities = 1;
        require(run(bank, {}, key(4096), {}, 1, {}, limits).reason == Reason::invalid_snapshot,
                "scratch World capacity bound enforced");
        limits = {}; limits.vm.stack_depth = 1;
        require(run(default_bank, owners, key(4096), context, 10, {}, limits).reason == Reason::vm_fault,
                "VM stack bound enforced");
        limits = {}; limits.vm.code_owners = 1;
        require(run(default_bank, owners, key(4096), context, 10, {}, limits).reason == Reason::invalid_resources,
                "VM owner bound rejected without exception");
        require(run(bank, {}, {odh::RoutineScope::global, 4096}, {}, 1).reason == Reason::invalid_resources,
                "explicit mismatched root key rejected");
        std::cout << checks << " read-only BHAV checks passed (synthetic scratch World only)\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
