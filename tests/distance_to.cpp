#include "odh/distance_to.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
int checks = 0;

void require(bool value, const char* message) {
    ++checks;
    if (!value) throw std::runtime_error(message);
}

std::array<std::uint8_t, 8> operand(
    std::uint16_t temp, std::uint8_t flags, std::uint8_t scope,
    std::int16_t data, std::array<std::uint8_t, 2> trailing = {}) {
    const auto raw = static_cast<std::uint16_t>(data);
    return {static_cast<std::uint8_t>(temp & 0xffU),
            static_cast<std::uint8_t>(temp >> 8), flags, scope,
            static_cast<std::uint8_t>(raw & 0xffU),
            static_cast<std::uint8_t>(raw >> 8), trailing[0], trailing[1]};
}

odh::RoutineSet routine(std::array<std::uint8_t, 8> operands) {
    return {{odh::routine_key(4096), odh::Bhav{0x8002,
        {{11, 254, 255, operands}}, {}, odh::Bhav::Metadata{0, 4, 4, 0}}}};
}

struct RunResult {
    odh::VmStatus status;
    odh::Ts1Temps temps;
    std::string diagnostic;
};

RunResult run(odh::World& world, std::array<std::uint8_t, 8> operands,
              std::vector<std::int16_t> arguments = {},
              std::vector<std::int16_t> locals = {},
              odh::Ts1Temps temps = {},
              std::int16_t caller = 1, std::int16_t stack_object = 2) {
    arguments.resize(4, 0);
    locals.resize(4, 0);
    odh::Vm vm{routine(operands), world.vm_memory(), {},
               odh::get_distance_to_primitive(world, world.vm_memory())};
    const auto key = odh::routine_key(4096);
    odh::VmFrame frame{key, 0, {caller, stack_object, stack_object, 0},
                       std::move(arguments), std::move(locals)};
    vm.restore_dormant_state({std::move(frame)}, temps);
    require(vm.activate_dormant_state(), "distance BHAV did not activate");
    const auto status = vm.run(1);
    return {status, vm.temps(), vm.diagnostic()};
}
}

int main() {
    try {
        odh::World world;
        require(world.create(1, odh::EntityKind::avatar, {0, 88, 0}) &&
                    world.set_position(1, {0, 0, 1, 0}), "caller fixture");
        require(world.create(2, odh::EntityKind::object, {0, 0, 1}) &&
                    world.set_position(2, {48, 64, 1, 0}), "stack object fixture");
        require(world.create(3, odh::EntityKind::object, {0, 0, 1}) &&
                    world.set_position(3, {80, 0, 1, 0}), "explicit target fixture");
        require(world.create(4, odh::EntityKind::object, {0, 0, 1}) &&
                    world.set_position(4, {0, 0, 2, 0}), "cross-floor target fixture");
        require(world.create(5, odh::EntityKind::object), "out-of-world target fixture");

        auto result = run(world, operand(0, 0, 0, 0));
        require(result.status == odh::VmStatus::returned_true && result.temps[0] == 5,
                "even Flags normalize to MyObject[11] and floor the Euclidean distance");

        require(world.set_position(2, {47, 63, 1, 0}), "set below-boundary stack position");
        result = run(world, operand(0, 0, 0, 0));
        require(result.status == odh::VmStatus::returned_true && result.temps[0] == 4,
                "GetDistanceTo floors values immediately below the next tile boundary");

        // Even Flags values ignore serialized Scope/Data. Trailing bytes are
        // unused by the pinned operand reader and must not constrain the opcode.
        result = run(world, operand(1, 0xfe, 6, -1234, {0xa5, 0x5a}));
        require(result.status == odh::VmStatus::returned_true && result.temps[1] == 4,
                "all even Flags values select Caller and TempNum selects the output register");

        result = run(world, operand(2, 1, 7, 3, {0x77, 0x88}));
        require(result.status == odh::VmStatus::returned_true && result.temps[2] == 4,
                "literal target selector resolves an arbitrary entity ID");

        odh::Ts1Temps temp_target{};
        temp_target[0] = 3;
        result = run(world, operand(1, 3, 8, 0), {}, {}, temp_target);
        require(result.status == odh::VmStatus::returned_true && result.temps[1] == 4 &&
                    result.temps[0] == 3,
                "Temps scope reads the target ID and preserves unrelated Temp values");

        result = run(world, operand(1, 1, 9, 0), {3});
        require(result.status == odh::VmStatus::returned_true && result.temps[1] == 4,
                "Parameters scope resolves the selected target ID");

        result = run(world, operand(1, 1, 25, 0), {}, {3});
        require(result.status == odh::VmStatus::returned_true && result.temps[1] == 4,
                "Local scope resolves the selected target ID");

        result = run(world, operand(1, 1, 3, 11));
        require(result.status == odh::VmStatus::returned_true && result.temps[1] == 4,
                "MyObject[11] encoding resolves Caller through VM memory");

        result = run(world, operand(1, 1, 10, -32768));
        require(result.status == odh::VmStatus::returned_true && result.temps[1] == 0,
                "StackObjectID scope resolves the current StackObject");

        require(world.set_position(2, {0, 0, 1, 0}), "set minimum cross-floor stack position");
        result = run(world, operand(0, 1, 7, 4));
        require(result.status == odh::VmStatus::returned_true && result.temps[0] == 20,
                "TS1 cross-floor distance enforces the twenty-tile minimum");

        require(world.set_position(2, {400, 320, 2, 0}), "set long cross-floor stack position");
        result = run(world, operand(0, 1, 7, 1));
        require(result.status == odh::VmStatus::returned_true && result.temps[0] == 37,
                "TS1 cross-floor distance adds five tiles to a long planar distance");

        require(world.set_position(4, {0, 0, 1, 0}) &&
                    world.set_position(2, {0, 0, 3, 0}), "set two-floor-difference positions");
        result = run(world, operand(0, 1, 7, 4));
        require(result.status == odh::VmStatus::returned_true && result.temps[0] == 40,
                "TS1 cross-floor minimum scales by the absolute floor difference");

        require(world.set_position(2, {0, 0, 1, 0}), "set out-of-world distance stack position");
        result = run(world, operand(0, 1, 7, 5));
        require(result.status == odh::VmStatus::returned_true && result.temps[0] == 2896,
                "unpositioned entity uses LotTilePos.OUT_OF_WORLD coordinates");

        odh::Ts1Temps sentinels{};
        sentinels.fill(77);
        result = run(world, operand(2, 1, 7, 99), {}, {}, sentinels);
        require(result.status == odh::VmStatus::fault && result.temps[2] == 77,
                "missing selected target faults without writing TempNum");

        result = run(world, operand(20, 0, 0, 0), {}, {}, sentinels);
        require(result.status == odh::VmStatus::fault && result.temps == sentinels,
                "out-of-range TempNum faults without changing any Temp");

        result = run(world, operand(2, 1, 6, 0), {}, {}, sentinels);
        require(result.status == odh::VmStatus::fault && result.temps == sentinels,
                "unavailable global target memory faults without a Temp write");

        require(world.set_position(2, {0, 0, 130, 0}), "set nonrepresentable source level");
        result = run(world, operand(2, 0, 0, 0), {}, {}, sentinels);
        require(result.status == odh::VmStatus::fault && result.temps == sentinels,
                "position outside the source signed-byte level range is rejected without narrowing");

        std::cout << checks << " source-mapped distance-to checks passed (synthetic World only)\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
