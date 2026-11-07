#include "odh/avatar_startup.hpp"

#include <iostream>
#include <map>
#include <stdexcept>
#include <tuple>

namespace {
int checks = 0;
void require(bool value, const char* message) {
    ++checks;
    if (!value) throw std::runtime_error(message);
}
odh::RoutineKey key(unsigned id) { return odh::routine_key(static_cast<std::uint16_t>(id)); }
odh::Instruction assign(unsigned value) {
    return {2, 254, 255, {0, 0, static_cast<std::uint8_t>(value), 0, 0, 5, 0, 7}};
}
odh::Bhav routine(unsigned value) {
    return {0x8002, {assign(value)}, {}, odh::Bhav::Metadata{0, 4, 4, 0}};
}
odh::Bhav looping_routine() {
    auto instruction = assign(9);
    instruction.on_true = 0;
    return {0x8002, {instruction}, {}, odh::Bhav::Metadata{0, 4, 4, 0}};
}
}

int main() {
    try {
        std::map<std::int16_t, std::int16_t> world_words;
        std::vector<std::tuple<std::int16_t, std::int16_t, std::int16_t,
                               odh::CodeOwner, std::vector<std::int16_t>>> contexts;
        odh::VmMemory memory;
        memory.write = [&](const odh::VmFrame& frame, std::uint8_t scope,
                           std::int16_t index, std::int16_t value) {
            require(scope == 0 && index == 0, "fixture writes the avatar's World-backed value");
            contexts.emplace_back(frame.context.caller, frame.context.callee,
                                  frame.context.stack_object, frame.context.code_owner,
                                  frame.arguments);
            world_words[frame.context.caller] = value;
            return true;
        };
        const auto shared = std::make_shared<const odh::VmProgram>(odh::RoutineSet{});
        const auto make_vm = [&](std::int16_t id, std::int16_t init_value,
                                 std::int16_t entry8_value) {
            odh::RoutineSet private_code{{key(4096), routine(static_cast<unsigned>(init_value))},
                                          {key(4097), routine(static_cast<unsigned>(entry8_value))}};
            odh::RoutineOwners owners{{static_cast<odh::CodeOwner>(id), std::move(private_code)}};
            return std::make_unique<odh::Vm>(shared, odh::ShareVmProgramTag{}, memory,
                                              odh::VmLimits{}, odh::VmPrimitive{}, std::move(owners));
        };
        auto first = make_vm(11, 101, 102);
        auto second = make_vm(22, 201, 202);
        const auto first_result = odh::run_avatar_startup(*first, 11, 4096, 4097);
        const auto second_result = odh::run_avatar_startup(*second, 22, 4096, 4097);
        require(first_result.completed && second_result.completed,
                "each avatar completes Init and entry 8 on its own private VM");
        require(first_result.steps == 2 && second_result.steps == 2,
                "startup reports the actual combined instruction count per avatar");
        require(world_words[11] == 102 && world_words[22] == 202,
                "both startup stages update their own avatar through the shared World callback");
        require(contexts.size() == 4, "both private startup entries reached shared World memory");
        for (const auto& [caller, callee, stack_object, owner, arguments] : contexts) {
            require(caller == callee && stack_object == 0 && owner == static_cast<odh::CodeOwner>(caller),
                    "startup uses caller/callee identity and the avatar's private owner");
            require(arguments == std::vector<std::int16_t>({0, 0, 0, 0}),
                    "startup passes exactly four zero arguments");
        }
        require(first_result.stages[0].phase == "init" &&
                first_result.stages[1].phase == "dynamic_multitile" &&
                first_result.stages[0].status == odh::VmStatus::returned_true &&
                first_result.stages[1].status == odh::VmStatus::returned_true,
                "Init runs before entry 8 and both return synchronously");
        require(first->stack().empty() && second->stack().empty(),
                "startup helper never stacks or dispatches Main");

        // A declared argument count is not the constructor's actual array length.
        odh::Bhav defaults{0x8002, {}, {}, odh::Bhav::Metadata{0, 0, 0, 0}};
        for (std::uint16_t n = 0; n < 4; ++n) {
            defaults.instructions.push_back({2, static_cast<std::uint8_t>(n == 3 ? 254 : n + 1), 255,
                {static_cast<std::uint8_t>(n), 0, 0, 0, 0, 2, 9, 7}}); // Param n == 0
        }
        odh::Vm default_vm(shared, odh::ShareVmProgramTag{}, memory, odh::VmLimits{}, {},
            odh::RoutineOwners{{55, {{key(4096), defaults}, {key(4097), defaults}}}});
        const auto default_result = odh::run_avatar_startup(default_vm, 55, 4096, 4097);
        require(default_result.completed && default_result.steps == 8 &&
                default_result.stages[0].status == odh::VmStatus::returned_true &&
                default_result.stages[1].status == odh::VmStatus::returned_true,
                "both avatar constructor phases can read default Params0..3 despite zero metadata arguments");
        auto outside_defaults = defaults;
        outside_defaults.metadata->arguments = 9;
        outside_defaults.instructions = {{2, 254, 255, {4, 0, 0, 0, 0, 2, 9, 7}}};
        for (const bool dynamic_phase : {false, true}) {
            odh::Vm bounded_vm(shared, odh::ShareVmProgramTag{}, memory, odh::VmLimits{}, {},
                odh::RoutineOwners{{55, {{key(4096), dynamic_phase ? defaults : outside_defaults},
                                        {key(4097), outside_defaults}}}});
            const auto bounded_result = odh::run_avatar_startup(bounded_vm, 55, 4096, 4097);
            require(!bounded_result.completed && bounded_result.stages.back().status == odh::VmStatus::fault &&
                    bounded_result.stages.back().phase == (dynamic_phase ? "dynamic_multitile" : "init"),
                    "neither avatar constructor phase pads Params beyond the four source defaults");
        }

        auto skipped_vm = make_vm(33, 0, 0);
        const auto skipped = odh::run_avatar_startup(*skipped_vm, 33, 255, 8);
        require(skipped.completed && skipped.steps == 0 && skipped.stages.size() == 2 &&
                skipped.stages[0].status == odh::VmStatus::ready &&
                skipped.stages[1].status == odh::VmStatus::ready,
                "entry IDs at or below 255 are skipped as in the reference");

        odh::RoutineOwners budget_owners{{44, {{key(4096), routine(1)},
                                               {key(4097), looping_routine()}}}};
        odh::Vm budget_vm(shared, odh::ShareVmProgramTag{}, memory, odh::VmLimits{},
                          odh::VmPrimitive{}, std::move(budget_owners));
        const auto budget = odh::run_avatar_startup(budget_vm, 44, 4096, 4097, 2);
        require(!budget.completed && budget.steps == 2 && budget.stages.size() == 2 &&
                budget.stages[0].status == odh::VmStatus::returned_true &&
                budget.stages[1].status == odh::VmStatus::budget_exhausted &&
                budget.stages[1].steps == 1,
                "Init and entry 8 share one budget and failed-stage work remains counted");
        require(budget.diagnostic == "startup entry did not return synchronously",
                "incomplete startup has an explicit host diagnostic");
        std::cout << "PASS avatar startup contract (" << checks << " checks)\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
