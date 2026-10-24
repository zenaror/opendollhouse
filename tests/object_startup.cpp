#include "odh/object_startup.hpp"

#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

namespace {
int checks = 0;
void require(bool value, const char* message) {
    ++checks;
    if (!value) throw std::runtime_error(message);
}
odh::Bhav set_value(std::uint16_t value) {
    odh::Instruction assign{2, 254, 255,
        {0, 0, static_cast<std::uint8_t>(value & 255),
         static_cast<std::uint8_t>(value >> 8), 0, 5, 0, 7}};
    return {0x8002, {assign}, {}, odh::Bhav::Metadata{0, 4, 4, 0}};
}
}

int main() {
    try {
        constexpr std::int16_t object_id = 37;
        constexpr odh::CodeOwner owner = 0x80000002U;
        int writes = 0;
        odh::VmMemory memory;
        memory.write = [&](const odh::VmFrame& frame, std::uint8_t scope,
                           std::int16_t index, std::int16_t value) {
            require(scope == 0 && index == 0 && value == 321,
                    "Init writes through the object World context");
            require(frame.context.caller == object_id && frame.context.callee == object_id &&
                        frame.context.stack_object == 0 && frame.context.code_owner == owner,
                    "Init uses the object's identity and independent resource owner");
            require(frame.arguments == std::vector<std::int16_t>({0, 0, 0, 0}),
                    "Init receives four zero arguments");
            ++writes;
            return true;
        };
        auto shared = std::make_shared<const odh::VmProgram>(odh::RoutineSet{});
        odh::RoutineOwners owners{{owner, {{odh::routine_key(4100), set_value(321)}}}};
        odh::Vm vm(shared, odh::ShareVmProgramTag{}, memory, odh::VmLimits{}, {},
                   std::move(owners));

        odh::EntryPointTable table{0, 0, std::vector<odh::EntryPoint>(9, {0, 0}), 0,
                                   odh::EntryPointSource::objf};
        table.entries[0].action = 4100;
        const auto started = odh::run_object_startup(vm, object_id, owner, table);
        require(started.completed && started.steps == 1 && writes == 1,
                "object Init completes before any later entry point");
        require(started.stages.size() == 2 && started.stages[0].phase == "init" &&
                    started.stages[0].status == odh::VmStatus::returned_true &&
                    started.stages[1].phase == "dynamic_multitile" &&
                    started.stages[1].status == odh::VmStatus::ready &&
                    started.stages[1].routine == 0,
                "entry 8 is skipped when Food has no action there");
        require(vm.stack().empty(), "object startup leaves Main for the scheduler");

        bool missing_table_rejected = false;
        try {
            odh::EntryPointTable short_table{0, 0, {{0, 4100}}, 0,
                                              odh::EntryPointSource::objf};
            (void)odh::run_object_startup(vm, object_id, owner, short_table);
        } catch (const std::invalid_argument&) { missing_table_rejected = true; }
        require(missing_table_rejected, "incomplete entry table is rejected");

        std::cout << "PASS object startup contract (" << checks << " checks)\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
