#include "odh/ts1_budget.hpp"

#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {
int checks = 0;
void require(bool value, const char* message) {
    ++checks;
    if (!value) throw std::runtime_error(message);
}
odh::Instruction budget(std::uint8_t old_owner, std::uint8_t scope,
        std::uint16_t index, std::uint8_t flags) {
    return {25, 255, 255, {old_owner, scope, static_cast<std::uint8_t>(index),
        static_cast<std::uint8_t>(index >> 8), flags, 0, 3, 0}};
}
}

int main() {
    try {
        odh::VmMemory memory;
        memory.read = [](const odh::VmFrame&, std::uint8_t scope,
                std::int16_t index) -> std::optional<std::int16_t> {
            if (scope == 26 && index == 0) return 17;
            return {};
        };
        std::int32_t family_budget = 500;
        std::vector<std::int32_t> writes;
        const auto handler = odh::ts1_budget_primitive(memory,
            [&](const odh::VmFrame&) { return std::optional<std::int32_t>{family_budget}; },
            [&](const odh::VmFrame&, std::int32_t value) {
                writes.push_back(value); family_budget = value; return true;
            });
        const odh::VmFrame frame{};
        const std::array<std::int16_t, 20> temps{};
        const odh::VmDispatch dispatch{};

        auto outcome = handler(frame, budget(3, 26, 0, 1), temps, dispatch);
        require(outcome.disposition == odh::PrimitiveDisposition::goto_true && writes.empty(),
            "JustTest passes with a nonnegative FAMI budget without changing it");

        family_budget = -1;
        outcome = handler(frame, budget(3, 26, 0, 1), temps, dispatch);
        require(outcome.disposition == odh::PrimitiveDisposition::goto_false && writes.empty(),
            "negative FAMI budget fails before mutation");

        family_budget = 500;
        outcome = handler(frame, budget(3, 26, 0, 0), temps, dispatch);
        require(outcome.disposition == odh::PrimitiveDisposition::goto_true &&
            family_budget == 483 && writes == std::vector<std::int32_t>{483},
            "ordinary TS1 budget operation subtracts the resolved amount");

        family_budget = 500; writes.clear();
        outcome = handler(frame, budget(3, 26, 0, 2), temps, dispatch);
        require(outcome.disposition == odh::PrimitiveDisposition::goto_true &&
            family_budget == 517 && writes == std::vector<std::int32_t>{517},
            "TS1 Subtract flag reverses the amount as in the reference");

        std::int32_t local_amount = 12;
        auto local_frame = frame;
        local_frame.locals = {0, static_cast<std::int16_t>(local_amount)};
        family_budget = 500; writes.clear();
        outcome = handler(local_frame, budget(2, 26, 1, 0), temps, dispatch);
        require(outcome.disposition == odh::PrimitiveDisposition::goto_true && family_budget == 488,
            "old-owner Local overrides the operand scope");

        const auto no_family = odh::ts1_budget_primitive(memory,
            [](const odh::VmFrame&) { return std::optional<std::int32_t>{}; });
        outcome = no_family(frame, budget(3, 26, 0, 0), temps, dispatch);
        require(outcome.disposition == odh::PrimitiveDisposition::goto_true,
            "missing active family uses the TS1 zero-budget default");

        const auto read_only_family = odh::ts1_budget_primitive(memory,
            [](const odh::VmFrame&) { return std::optional<std::int32_t>{500}; });
        outcome = read_only_family(frame, budget(3, 26, 0, 0), temps, dispatch);
        require(outcome.disposition == odh::PrimitiveDisposition::unsupported,
            "a family-changing operation requires a persistent writer");

        outcome = handler(frame, budget(3, 42, 0, 1), temps, dispatch);
        require(outcome.disposition == odh::PrimitiveDisposition::unsupported,
            "TempXL is unavailable without a live VM owner");

        odh::Vm tempxl_vm(odh::RoutineSet{});
        tempxl_vm.temp_xl()[0] = 70000;
        family_budget = 100000; writes.clear();
        outcome = handler(frame, budget(3, 42, 0, 0), temps, {&tempxl_vm, 1});
        require(outcome.disposition == odh::PrimitiveDisposition::goto_true &&
            family_budget == 30000 && writes == std::vector<std::int32_t>{30000},
            "TS1 Budget reads the full signed 32-bit TempXL value");
        outcome = handler(frame, budget(3, 42, 2, 1), temps, {&tempxl_vm, 2});
        require(outcome.disposition == odh::PrimitiveDisposition::unsupported,
            "TempXL indices outside the two-value thread bank remain unsupported");
        tempxl_vm.temp_xl()[1] = std::numeric_limits<std::int32_t>::min();
        family_budget = 500; writes.clear();
        outcome = handler(frame, budget(3, 42, 1, 2), temps, {&tempxl_vm, 3});
        require(outcome.disposition == odh::PrimitiveDisposition::goto_true &&
            family_budget == -2147483148 && writes == std::vector<std::int32_t>{-2147483148},
            "Subtract flag wraps INT32_MIN without signed-overflow behavior");

        std::cout << "ts1_budget checks: " << checks << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "ts1_budget: " << error.what() << '\n';
        return 1;
    }
}
