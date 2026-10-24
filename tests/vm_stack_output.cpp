#include "odh/vm.hpp"
#include <iostream>

namespace {
int checks = 0;
void require(bool condition, const char* message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}
odh::RoutineKey key() { return odh::routine_key(4096); }
odh::VmContext context() { return {11, 22, 33, 17}; }
bool identities(const odh::VmContext& value, std::int16_t stack_object) {
    return value.caller == 11 && value.callee == 22 && value.stack_object == stack_object &&
           value.code_owner == 17;
}
odh::RoutineSet routines() {
    // Following expressions copy scope10 into temp1, then read a backend scope
    // using the updated frame context before returning through the selected branch.
    return {{key(), {0x8002, {{1, 1, 1, {}},
                            {2, 2, 255, {1,0,0,0,0,5,8,10}},
                            {2, 3, 255, {2,0,0,0,0,5,8,19}},
                            {1, 254, 255, {}}}, {},
                     odh::Bhav::Metadata{0, 4, 1, 0}}}};
}
odh::Vm machine(odh::PrimitiveOutcome outcome, odh::VmMemory memory = {}) {
    return odh::Vm(routines(), std::move(memory), {},
        [outcome](const auto&, const auto&, const auto&, const auto&) { return outcome; },
        {{17, routines()}});
}
void rejects(odh::PrimitiveOutcome outcome) {
    auto vm = machine(outcome);
    require(vm.start(key(), context(), {71}) == odh::VmStatus::running, "rejection fixture starts");
    vm.temps()[0] = 42;
    const auto before = vm.temps();
    require(vm.run(1) == odh::VmStatus::fault, "invalid compound output faults");
    require(vm.temps() == before, "rejection preserves all temps");
    require(vm.stack().size() == 1 && vm.stack()[0].pc == 0 &&
            vm.stack()[0].arguments[0] == 71 && vm.stack()[0].locals[0] == 0 &&
            identities(vm.stack()[0].context, 33),
            "rejection preserves frame, context, branch and depth");
    require(!vm.pending() && !vm.interaction_return(), "rejection creates no pending or interaction state");
}
}
int main() {
    try {
        {
            auto room_routines = odh::RoutineSet{{key(), {0x8002, {{2, 254, 255, {1,0,1,0,0,5,8,23}}}, {},
                odh::Bhav::Metadata{0, 4, 1, 0}}}};
            odh::VmMemory memory;
            memory.room_by_temp0 = [&](const auto& frame, std::int16_t room_selector,
                                       std::int16_t data_index) -> std::optional<std::int16_t> {
                require(frame.context.caller==11&&frame.context.callee==22&&
                        frame.context.stack_object==33&&frame.context.code_owner==0&&
                        room_selector == 7 && data_index == 1,
                        "RoomByTemp0 callback receives Temp0 and its data index");
                return 42;
            };
            odh::Vm vm(std::move(room_routines), memory);
            require(vm.start(key(), {11,22,33,0}) == odh::VmStatus::running,
                    "RoomByTemp0 scope fixture starts");
            vm.temps()[0] = 7;
            require(vm.run(1) == odh::VmStatus::returned_true,
                    "RoomByTemp0 backend value is read by an expression");
        }
        {
            odh::PrimitiveOutcome event{odh::PrimitiveDisposition::goto_false,{}};
            event.write=odh::VmFrameWrite{odh::VmFrameBank::locals,21,7,true};
            auto vm=machine(event);
            require(vm.start(key(),context())==odh::VmStatus::running,"animation event local fixture starts");
            require(vm.run(1)==odh::VmStatus::budget_exhausted&&vm.stack()[0].locals.size()==22&&
                    vm.stack()[0].locals[21]==7,
                    "explicit animation event output grows a short local bank within its byte index");
        }
        for (const auto disposition : {odh::PrimitiveDisposition::goto_true,
                                       odh::PrimitiveDisposition::goto_false}) {
            for (const std::int16_t value : {std::int16_t{0}, std::int16_t{-7},
                                           std::int16_t{-32768}, std::int16_t{32767}}) {
                // Old aggregate fields retain their order; the new field is last.
                odh::PrimitiveOutcome outcome{disposition, {},
                    odh::VmFrameWrite{odh::VmFrameBank::arguments, 0, 99}, {},
                    odh::VmTempWrite{19, -9}, odh::VmStackObjectWrite{value}};
                unsigned reads = 0;
                odh::VmMemory memory;
                memory.read = [&](const auto& frame, auto scope, auto index) -> std::optional<std::int16_t> {
                    require(scope == 19 && index == 0 && identities(frame.context, value),
                            "memory callback sees new stack identity and preserved other identities");
                    ++reads; return frame.context.stack_object;
                };
                auto vm = machine(outcome, memory);
                require(vm.start(key(), context(), {71}) == odh::VmStatus::running, "valid fixture starts");
                vm.temps()[0] = 42;
                require(vm.run(1) == odh::VmStatus::budget_exhausted &&
                        vm.stack()[0].pc == 1 && identities(vm.stack()[0].context, value) &&
                        vm.stack()[0].arguments[0] == 99 && vm.temps()[19] == -9 && vm.temps()[0] == 42,
                        "compound frame/temp/context output publishes on either branch");
                require(vm.run(2) == odh::VmStatus::budget_exhausted &&
                        vm.temps()[1] == value && vm.temps()[2] == value && reads == 1,
                        "subsequent Expression scope10 and backend read observe signed output");
                require(vm.run(1) == (disposition == odh::PrimitiveDisposition::goto_true ?
                        odh::VmStatus::returned_true : odh::VmStatus::returned_false),
                        "stack output preserves selected return branch");
            }
        }
        odh::PrimitiveOutcome combined{odh::PrimitiveDisposition::goto_true, {},
            odh::VmFrameWrite{odh::VmFrameBank::arguments, 0, 99}, {},
            odh::VmTempWrite{0, 9}, odh::VmStackObjectWrite{-7}};
        combined.temp_write = odh::VmTempWrite{20, 9}; rejects(combined);
        combined.temp_write = odh::VmTempWrite{0, 9};
        combined.write = odh::VmFrameWrite{odh::VmFrameBank::arguments, 4, 99}; rejects(combined);
        combined.write = odh::VmFrameWrite{odh::VmFrameBank::locals, 4, 99}; rejects(combined);
        combined.write = odh::VmFrameWrite{static_cast<odh::VmFrameBank>(99), 0, 99}; rejects(combined);
        combined.write = odh::VmFrameWrite{odh::VmFrameBank::arguments, 0, 99};
        combined.call = odh::VmCall{key(), context(), {}}; rejects(combined);
        for (const auto disposition : {odh::PrimitiveDisposition::unsupported,
                                       odh::PrimitiveDisposition::wait, odh::PrimitiveDisposition::call,
                                       static_cast<odh::PrimitiveDisposition>(99)}) {
            // Context output alone is forbidden too; a call cannot push a child first.
            odh::PrimitiveOutcome rejected{disposition, {}};
            rejected.stack_object_write = odh::VmStackObjectWrite{99};
            if (disposition == odh::PrimitiveDisposition::call)
                rejected.call = odh::VmCall{key(), context(), {}};
            rejects(rejected);
            rejected.write = odh::VmFrameWrite{odh::VmFrameBank::arguments, 0, 99};
            rejected.temp_write = odh::VmTempWrite{0, 9};
            rejects(rejected);
        }
        std::cout << checks << " checks passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
