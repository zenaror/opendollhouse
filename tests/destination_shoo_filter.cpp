#include "odh/entity_next.hpp"
#include <iostream>
#include <map>
#include <memory>
#include <tuple>
#include <vector>

namespace {
int checks = 0;
void require(bool value, const char* message) {
    ++checks;
    if (!value) throw std::runtime_error(message);
}

odh::Instruction instruction(std::uint8_t opcode, std::uint8_t on_true,
                             std::uint8_t on_false,
                             std::array<std::uint8_t, 8> operands) {
    return {opcode, on_true, on_false, operands};
}

odh::Bhav destination_shoo_filter() {
    // Exact four-instruction control flow and operands read from the installed
    // Destination.iff BHAV4114. The commercial resource itself is not required
    // by this synthetic VM test.
    return {0x8002, {
        instruction(2, 1, 253, {0,0,0,0,0,5,10,7}),
        instruction(31, 3, 255, {0,0,0,0,129,10,0,0}),
        instruction(2, 254, 1, {66,0,1,0,0,2,19,7}),
        instruction(2, 2, 1, {29,0,29,0,0,2,4,3})
    }, {}, odh::Bhav::Metadata{0,0,0,2}};
}

struct Probe {
    odh::World world{odh::WorldLimits{}, odh::AttributePolicy::ts1_bounded};
    odh::RoutineKey key = odh::routine_key(4114);
    std::unique_ptr<odh::Vm> vm;

    Probe(std::vector<std::tuple<std::int16_t, std::int16_t, std::int16_t>> avatars) {
        for (const auto [id, room, private_flag] : avatars) {
            require(world.create(id, odh::EntityKind::avatar, {30,67,0,0}),
                    "create fixture avatar with Room and PersonData words");
            require(world.declare_object_word(id, 29, room), "set fixture avatar Room");
            require(world.set_position(id, {16,16,1,0}),
                    "place fixture avatar so Room reads the saved room value");
            require(world.set_person_data(id, 66, private_flag), "set fixture PersonData[66]");
        }
        auto routines = odh::RoutineSet{{key, destination_shoo_filter()}};
        auto memory = world.vm_memory();
        vm = std::make_unique<odh::Vm>(std::move(routines), memory, odh::VmLimits{},
            odh::entity_next_primitive(world, memory));
        require(vm->start(key, {1,2,1,0}) == odh::VmStatus::running,
                "start the reconstructed BHAV4114 in a shared World");
    }

    odh::VmStatus run() {
        return vm->run(64);
    }
};

void eligible_same_room_avatar_is_found() {
    Probe probe{{{1,5,0}, {2,5,1}, {3,6,1}}};
    require(probe.run() == odh::VmStatus::returned_true,
            "same-room avatar with PersonData[66]=1 satisfies BHAV4114");
}

void different_room_avatar_is_skipped() {
    Probe probe{{{1,5,0}, {2,6,1}, {3,5,1}}};
    require(probe.run() == odh::VmStatus::returned_true,
            "BHAV4114 keeps searching past a matching avatar in another room");
    require(probe.vm->stack().empty(), "matching result returns cleanly");
}

void no_eligible_same_room_avatar_returns_false() {
    Probe probe{{{1,5,0}, {2,6,1}, {3,5,0}}};
    require(probe.run() == odh::VmStatus::returned_false,
            "no same-room avatar with PersonData[66]=1 returns false");
}
}

int main() {
    try {
        eligible_same_room_avatar_is_found();
        different_room_avatar_is_skipped();
        no_eligible_same_room_avatar_returns_false();
        std::cout << "destination_shoo_filter: " << checks << " checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "destination_shoo_filter: " << error.what() << '\n';
        return 1;
    }
}
