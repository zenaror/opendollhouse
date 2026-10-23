#include "odh/avatar_startup.hpp"
#include "odh/household_spawn.hpp"
#include "odh/random.hpp"

#include <iostream>
#include <map>
#include <stdexcept>

namespace {
unsigned checks = 0;
void require(bool value, const char* message) {
    ++checks;
    if (!value) throw std::runtime_error(message);
}

odh::Instruction assign_object_word(std::int16_t index, std::int16_t value) {
    odh::Instruction instruction{2, 0xfe, 0xff, {}};
    const auto data = static_cast<std::uint16_t>(index);
    const auto literal = static_cast<std::uint16_t>(value);
    instruction.operands = {static_cast<std::uint8_t>(data),
        static_cast<std::uint8_t>(data >> 8), static_cast<std::uint8_t>(literal),
        static_cast<std::uint8_t>(literal >> 8), 0, 5, 3, 7};
    return instruction;
}

odh::Instruction random_to_temp() {
    return {8, 1, 0xff, {0, 0, 8, 0, 0xff, 0xff, 7, 0}};
}

odh::Instruction assign_temp_to_object_word(std::int16_t index) {
    odh::Instruction instruction{2, 0xfe, 0xff, {}};
    const auto data = static_cast<std::uint16_t>(index);
    instruction.operands = {static_cast<std::uint8_t>(data),
        static_cast<std::uint8_t>(data >> 8), 0, 0, 0, 5, 3, 8};
    return instruction;
}

odh::Bhav routine(std::vector<odh::Instruction> instructions) {
    return {0x8002, std::move(instructions), {}, odh::Bhav::Metadata{0, 4, 4, 0}};
}

odh::NeighborRecord neighbor(std::uint32_t guid) {
    odh::NeighborRecord result{};
    result.version = 4;
    result.person_mode = 1;
    result.neighbor_id = 42;
    result.guid = guid;
    result.person_data = std::vector<std::int16_t>(80, 17);
    (*result.person_data)[61] = 123;
    return result;
}
}

int main() {
    try {
        constexpr std::uint32_t existing_guid = 0xaaaa;
        constexpr std::uint32_t missing_guid = 0xbbbb;
        odh::World source;
        require(source.create(5, odh::EntityKind::avatar, {0, 101, 3, 16}),
                "source household member exists");
        require(source.define_type(5, existing_guid) && source.set_position(5, {32, 48, 1, 2}),
                "source household member has definition and position");
        const auto source_before = source.snapshot();
        const auto family_member = neighbor(missing_guid);

        const auto shared_program = std::make_shared<const odh::VmProgram>(odh::RoutineSet{});
        std::map<std::int16_t, std::uint64_t> random_seeds;
        auto staged = odh::stage_household_members(source.snapshot(),
            {{existing_guid, nullptr}, {missing_guid, &family_member}}, 123,
            [&](odh::World& candidate, std::int16_t id, std::uint32_t guid) {
                require(guid == missing_guid, "only the absent resident needs startup");
                require(candidate.declare_object_word(id, 20, 0) &&
                        candidate.declare_object_word(id, 21, 0),
                        "candidate avatar exposes fixture data words");

                odh::RandomState random(1);
                auto memory = candidate.vm_memory();
                odh::VmPrimitive primitive = odh::random_primitive(random, memory);
                odh::RoutineSet private_code{
                    {odh::routine_key(4096), routine({assign_object_word(20, 77)})},
                    {odh::routine_key(4097), routine({random_to_temp(), assign_temp_to_object_word(21)})}
                };
                odh::RoutineOwners owners{{static_cast<odh::CodeOwner>(id), std::move(private_code)}};
                odh::Vm vm(shared_program, odh::ShareVmProgramTag{}, memory,
                            odh::VmLimits{}, std::move(primitive), std::move(owners));
                const auto startup = odh::run_avatar_startup(vm, id, 4096, 4097);
                random_seeds.emplace(id, random.seed());
                return startup.completed && startup.steps == 3;
            });

        const auto runtime_id = staged.runtime_ids.at(missing_guid);
        const auto* added = staged.candidate->find(runtime_id);
        require(added && !added->position, "startup does not place the avatar");
        require(added->object_words.at(20) == 77 && added->object_words.at(21) == 31930,
                "Init and entry 8 write through the candidate World's VM memory");
        require(random_seeds.at(runtime_id) == UINT64_C(0x2000001),
                "entry 8 consumes exactly one value from that avatar's explicit RNG");
        require(added->person_data[31] == 0 && added->person_data[61] == 0,
                "family inheritance is still deferred until after startup");

        odh::inherit_staged_household_member(*staged.candidate, runtime_id,
                                              family_member, 123);
        added = staged.candidate->find(runtime_id);
        require(added && added->person_data[31] == 42 && added->person_data[61] == 123,
                "NBRS and family identity are applied after startup");
        require(!added->position, "inheritance does not invent an arrival position");
        const auto source_after = source.snapshot();
        require(source_after.entities.size() == source_before.entities.size() &&
                source_after.entities[0].person_data == source_before.entities[0].person_data &&
                source_after.entities[0].object_words == source_before.entities[0].object_words &&
                source_after.entities[0].position == source_before.entities[0].position,
                "all startup writes remain isolated from the original World");

        std::cout << "PASS staged household startup (" << checks << " checks)\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
