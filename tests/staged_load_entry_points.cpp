#include "odh/staged_load_entry_points.hpp"
#include "odh/world_snapshot_codec.hpp"
#include "odh/random.hpp"
#include <array>
#include <iostream>

namespace {
int checks = 0;
void require(bool value, const char* message) {
    ++checks;
    if (!value) throw std::runtime_error(message);
}
odh::RoutineKey key(std::uint16_t id) { return odh::routine_key(id); }
odh::Bhav routine(std::initializer_list<odh::Instruction> code) {
    return {0x8002, code, {}, odh::Bhav::Metadata{0, 4, 0, 0}};
}
odh::Instruction assign_to(std::uint8_t scope, std::int16_t index, std::int16_t value) {
    const auto lhs = static_cast<std::uint16_t>(index);
    const auto rhs = static_cast<std::uint16_t>(value);
    return {2, 254, 255, {static_cast<std::uint8_t>(lhs), static_cast<std::uint8_t>(lhs >> 8),
        static_cast<std::uint8_t>(rhs), static_cast<std::uint8_t>(rhs >> 8), 0, 5, scope, 7}};
}
odh::Instruction assign(std::int16_t index, std::int16_t value) { return assign_to(3, index, value); }
odh::Instruction assign_from_temp(std::int16_t index, std::int16_t temp) {
    const auto lhs = static_cast<std::uint16_t>(index);
    const auto rhs = static_cast<std::uint16_t>(temp);
    return {2, 254, 255, {static_cast<std::uint8_t>(lhs), static_cast<std::uint8_t>(lhs >> 8),
        static_cast<std::uint8_t>(rhs), static_cast<std::uint8_t>(rhs >> 8), 0, 5, 3, 8}};
}
odh::Instruction assign_from_scope(std::uint8_t lhs_scope,std::int16_t lhs_index,
                                   std::uint8_t rhs_scope,std::int16_t rhs_index) {
    const auto lhs=static_cast<std::uint16_t>(lhs_index);
    const auto rhs=static_cast<std::uint16_t>(rhs_index);
    return {2,254,255,{static_cast<std::uint8_t>(lhs),static_cast<std::uint8_t>(lhs>>8),
        static_cast<std::uint8_t>(rhs),static_cast<std::uint8_t>(rhs>>8),0,5,lhs_scope,rhs_scope}};
}
odh::WorldSnapshot fixture() {
    odh::World world;
    for (const auto id : {1, 2}) {
        if (!world.create(static_cast<std::int16_t>(id), odh::EntityKind::object) ||
            !world.define_type(static_cast<std::int16_t>(id), 0x1000U + static_cast<unsigned>(id)) ||
            !world.declare_object_word(static_cast<std::int16_t>(id), 23, -1) ||
            !world.define_entry_points(static_cast<std::int16_t>(id), {{0, 0}, {0, 0}, {77, 4096}}))
            throw std::runtime_error("synthetic Load entity setup");
    }
    return world.snapshot();
}
}

int main() {
    try {
        const auto source = fixture();
        const auto source_bytes = odh::encode_world_snapshot(source);
        const std::map<std::int16_t, odh::CodeOwner> entity_owners{{1, 101}, {2, 202}};
        const std::map<std::int16_t, bool> non_ghost{{1, false}, {2, false}};
        const std::map<std::int16_t, std::array<std::int16_t, 20>> zero_temps{{1, {}}, {2, {}}};
        const odh::RoutineOwners owners{
            {101, {{key(4096), routine({assign(23, 11)})}}},
            {202, {{key(4096), routine({assign(23, 22)})}}}
        };
        const odh::RoutineSet globals;

        auto result = odh::stage_saved_load_entry_points(source, {2, 1}, entity_owners, non_ghost, zero_temps,
                                                         globals, owners, 8);
        require(result.snapshot.has_value() && result.reason.find("all Load actions") == 0,
                "complete batch returns a staged World");
        require(result.completed_entities == std::vector<std::int16_t>({2, 1}),
                "the caller's serialized entity order is preserved");
        require(result.instructions == 2, "aggregate count covers each executed action");
        require(result.snapshot->entities[0].object_words.at(23) == 11 &&
                result.snapshot->entities[1].object_words.at(23) == 22,
                "each private action uses the exact resource owner for its entity");
        require(odh::encode_world_snapshot(source) == source_bytes,
                "staging leaves the source snapshot byte-for-byte unchanged");

        auto missing_root_source = source;
        missing_root_source.entities[0].entry_points->at(2).action = 4107;
        auto missing_root_owners = owners;
        missing_root_owners[101].erase(key(4096));
        result = odh::stage_saved_load_entry_points(missing_root_source, {1, 2}, entity_owners,
            non_ghost, zero_temps, globals, missing_root_owners, 8);
        require(result.snapshot.has_value() && result.missing_root_actions ==
                std::vector<std::pair<std::int16_t, std::uint16_t>>({{1, 4107}}),
                "a missing immediate Load root returns false and does not abort the batch");
        require(result.snapshot->entities[0].object_words.at(23) == -1 &&
                result.snapshot->entities[1].object_words.at(23) == 22,
                "missing-root false return preserves the object and continues later Load actions");

        auto tuning_owners=owners;
        tuning_owners[101][key(4096)]=routine({assign_from_scope(3,23,26,16516)});
        odh::VmMemory tuning_memory;
        bool exact_tuning_query=false;
        tuning_memory.read=[&](const odh::VmFrame& frame,std::uint8_t scope,
                               std::int16_t index)->std::optional<std::int16_t>{
            if(scope!=26)return {};
            exact_tuning_query=frame.context.code_owner==101&&index==16516;
            return exact_tuning_query?std::optional<std::int16_t>{42}:std::nullopt;
        };
        result=odh::stage_saved_load_entry_points(source,{1,2},entity_owners,non_ghost,zero_temps,
            globals,tuning_owners,8,{},tuning_memory);
        require(result.snapshot.has_value()&&exact_tuning_query&&
                result.snapshot->entities[0].object_words.at(23)==42,
                "a supplemental owner-scoped read reaches the staged Load VM and its World copy");

        auto missing_primitive_owners=owners;
        missing_primitive_owners[101][key(4096)]=routine({
            {10,1,253,{}},assign(23,71)
        });
        result=odh::stage_saved_load_entry_points(source,{1,2},entity_owners,non_ghost,zero_temps,
            globals,missing_primitive_owners,8);
        require(result.snapshot.has_value()&&result.snapshot->entities[0].object_words.at(23)==71,
                "Simitone's unregistered opcode 10 follows its true branch in saved Load");
        missing_primitive_owners[101][key(4096)]=routine({
            {1,1,253,{}},assign(23,72)
        });
        result=odh::stage_saved_load_entry_points(source,{1,2},entity_owners,non_ghost,zero_temps,
            globals,missing_primitive_owners,8);
        require(result.snapshot.has_value()&&result.snapshot->entities[0].object_words.at(23)==72,
                "Simitone's unregistered opcode 1 placeholder follows its true branch in saved Load");
        missing_primitive_owners[101][key(4096)]=routine({
            {7,1,253,{0,0,0,0,0,0,0,0}},assign(23,73)
        });
        result=odh::stage_saved_load_entry_points(source,{1,2},entity_owners,non_ghost,zero_temps,
            globals,missing_primitive_owners,8);
        require(result.snapshot.has_value()&&result.snapshot->entities[0].object_words.at(23)==73,
                "Load Refresh(Caller, graphic) is a render-cache no-op in the unpublished candidate");

        auto random_owners=owners;
        random_owners[101][key(4096)]=routine({
            {8,1,253,{0,0,8,0,5,0,7,0}},assign_from_temp(23,0)
        });
        odh::RandomState load_random(1),expected_random(1);
        const auto expected_random_value=static_cast<std::int16_t>(expected_random.next(5));
        const auto random_handler=odh::random_primitive(load_random);
        result=odh::stage_saved_load_entry_points(source,{1,2},entity_owners,non_ghost,zero_temps,
            globals,random_owners,8,{}, {},random_handler);
        require(result.snapshot.has_value()&&result.snapshot->entities[0].object_words.at(23)==expected_random_value&&
                load_random.seed()==expected_random.seed(),
                "saved Load shares the explicit TS1 RNG state through the injected opcode 8 handler");

        auto failing_owners = owners;
        failing_owners[101][key(4096)] = routine({assign(23, 33)});
        failing_owners[202][key(4096)] = routine({{999, 254, 255, {}}});
        result = odh::stage_saved_load_entry_points(source, {1, 2}, entity_owners, non_ghost, zero_temps,
                                                   globals, failing_owners, 8);
        require(!result.snapshot && result.failed_entity == 2 && result.failed_action == 4096 &&
                result.vm_status == odh::VmStatus::fault,
                "a later unsupported action rejects the whole batch");
        require(odh::encode_world_snapshot(source) == source_bytes,
                "a rejected batch exposes no partial changes to its source");

        result = odh::stage_saved_load_entry_points(source, {1, 2}, entity_owners, non_ghost, zero_temps,
                                                   globals, owners, 1);
        require(!result.snapshot && result.failed_entity == 2 &&
                result.vm_status == odh::VmStatus::budget_exhausted,
                "aggregate budget exhaustion rejects the batch");
        result = odh::stage_saved_load_entry_points(source, {1}, entity_owners, non_ghost, zero_temps,
                                                   globals, owners, 8);
        require(!result.snapshot && result.reason == "entity order does not cover the complete World",
                "partial entity lists cannot be mistaken for a complete load");
        result = odh::stage_saved_load_entry_points(source, {1, 1}, entity_owners, non_ghost, zero_temps,
                                                   globals, owners, 8);
        require(!result.snapshot && result.failed_entity == 1 &&
                result.reason == "entity order contains a duplicate ID",
                "duplicate entity identities are rejected");

        auto false_owner = owners;
        false_owner[101][key(4096)] = routine({
            {2, 254, 253, {1, 0, 2, 0, 0, 2, 7, 7}}
        });
        result = odh::stage_saved_load_entry_points(source, {1, 2}, entity_owners, non_ghost, zero_temps,
                                                   globals, false_owner, 8);
        require(result.snapshot.has_value() && result.completed_entities.size() == 2,
                "a synchronous false return is a completed generic EntryPoint");

        result = odh::stage_saved_load_entry_points(source, {1, 2}, {{2, 202}}, non_ghost, zero_temps,
                                                   globals, owners, 8);
        require(!result.snapshot && result.failed_entity == 1 && result.failed_action == 4096 &&
                result.reason == "executable Load action has no exact resource CodeOwner",
                "missing resource owner rejects the action without fallback");

        auto no_actions = source;
        for (auto& entity : no_actions.entities) entity.entry_points->at(2).action = 255;
        result = odh::stage_saved_load_entry_points(no_actions, {1, 2}, {}, non_ghost, zero_temps, globals, {}, 0);
        require(result.snapshot.has_value() && result.instructions == 0,
                "actions at or below 255 follow the generic-entrypoint no-op path");
        auto one_ghost = non_ghost;
        one_ghost[1] = true;
        result = odh::stage_saved_load_entry_points(source, {1, 2}, entity_owners,
                                                   one_ghost, zero_temps, globals, owners, 8);
        require(result.snapshot.has_value() && result.snapshot->entities[0].object_words.at(23) == -1 &&
                result.snapshot->entities[1].object_words.at(23) == 22,
                "GhostImage actions run in a disposable sandbox and do not publish their writes");
        result = odh::stage_saved_load_entry_points(source, {1, 2}, entity_owners,
                                                   {{1, false}}, zero_temps, globals, owners, 8);
        require(!result.snapshot && result.reason == "GhostImage state is not known for every entity",
                "unknown GhostImage state blocks execution");
        auto saved_temps = zero_temps;
        saved_temps.at(1)[0] = 77;
        auto temp_owner = owners;
        temp_owner[101][key(4096)] = routine({assign_from_temp(23, 0)});
        result = odh::stage_saved_load_entry_points(source, {1, 2}, entity_owners, non_ghost,
                                                   saved_temps, globals, temp_owner, 8);
        require(result.snapshot.has_value() && result.snapshot->entities[0].object_words.at(23) == 77,
                "Load action reads the entity's saved temporary registers");
        result = odh::stage_saved_load_entry_points(source, {1, 2}, entity_owners, non_ghost,
                                                   {{1, {}}}, globals, owners, 8);
        require(!result.snapshot && result.reason == "saved temporary registers are not known for every entity",
                "unknown saved temporary registers block execution");
        std::cout << "staged_load_entry_points: " << checks << " checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "staged_load_entry_points: " << error.what() << '\n';
        return 1;
    }
}
