#include "odh/world_collision_candidates.hpp"

#include <iostream>
#include <stdexcept>

namespace {
int checks = 0;
void require(bool value, const char* message) {
    ++checks;
    if (!value) throw std::runtime_error(message);
}
odh::DefinitionIndex defs(bool metadata = true) {
    odh::ObjectDefinition d{};
    d.guid = 100;
    if (metadata) { d.tile_width = 16; d.footprint_mask = 0; }
    return odh::DefinitionIndex({{"fixture", "fixture.iff", 0, d}});
}
bool object(odh::World& w, std::int16_t id, std::int32_t x,
            bool flags = true, std::uint16_t value = 0) {
    return w.create(id, odh::EntityKind::object, {0, 0, 1}) && w.define_type(id, 100) &&
        (!flags || w.declare_object_word(id, 8, static_cast<std::int16_t>(value))) &&
        w.set_position(id, {x, 16, 1, 0});
}
auto alive = [](std::int16_t, std::uint64_t) -> std::optional<std::int32_t> { return -1; };
}

int main() {
    try {
        using namespace odh;
        World w;
        require(object(w, 3, 48) && object(w, 1, 16), "out-of-order object fixtures");
        require(w.create(2, EntityKind::avatar, {0, 88, 1}) &&
                    w.declare_object_word(2, 8, 0) && w.set_position(2, {32, 16, 1, 0}) &&
                    w.set_static_footprint(2, false), "avatar fixture");
        require(object(w, 4, 64, true, 4), "zero extent object fixture");
        require(w.create(5, EntityKind::object) && w.define_type(5, 100), "unpositioned fixture");
        require(w.create(6, EntityKind::object) && w.attach(1, 0, 6), "attached fixture");
        auto result = world_collision_candidates(w, defs(), alive);
        require(result.state == WorldCollisionCandidatesState::complete && result.entities.size() == 6,
                "complete result includes every enumerated entity");
        require(result.entities[0].entity_id == 1 && result.entities[1].entity_id == 2 &&
                    result.entities[2].entity_id == 3, "output order follows live World ID order");
        require(result.entities[0].state == WorldCollisionCandidateState::candidate &&
                    result.entities[0].box == CollisionBox{8, 8, 24, 24} &&
                    result.entities[0].position == EntityPosition{16, 16, 1, 0},
                "object shape and originating position are retained");
        require(result.entities[1].state == WorldCollisionCandidateState::candidate &&
                    result.entities[1].box == CollisionBox{29, 13, 35, 19} &&
                    !result.entities[1].static_footprint, "avatar shape and dynamic class are retained");
        require(result.entities[3].state == WorldCollisionCandidateState::absent &&
                    !result.entities[3].box, "zero extent object is absent");
        require(result.entities[4].state == WorldCollisionCandidateState::absent &&
                    !result.entities[4].position && result.entities[5].state == WorldCollisionCandidateState::absent,
                "no-position and attached entities are consistently absent");

        World lifecycle;
        require(lifecycle.create(1, EntityKind::avatar, {0, 88, 1}) &&
                    lifecycle.declare_object_word(1, 8, 0) && lifecycle.set_position(1, {16, 16, 1, 0}),
                "lifecycle avatar");
        auto dying = [](std::int16_t, std::uint64_t) -> std::optional<std::int32_t> { return 0; };
        result = world_collision_candidates(lifecycle, defs(), dying);
        require(result.state == WorldCollisionCandidatesState::complete &&
                    result.entities[0].state == WorldCollisionCandidateState::absent,
                "dying avatar without zero extent has no obstacle");
        require(lifecycle.declare_object_word(1, 8, 4), "set avatar zero extent");
        result = world_collision_candidates(lifecycle, defs(), dying);
        require(result.state == WorldCollisionCandidatesState::complete &&
                    result.entities[0].state == WorldCollisionCandidateState::candidate,
                "dying avatar with zero extent retains candidate geometry per World RoomInfo behavior");
        const auto unknown = [](std::int16_t, std::uint64_t) -> std::optional<std::int32_t> { return {}; };
        result = world_collision_candidates(lifecycle, defs(), unknown);
        require(result.state == WorldCollisionCandidatesState::unsupported && result.entities.empty(),
                "unknown avatar lifecycle fails closed even for zero extent");

        World missing_flags;
        require(object(missing_flags, 1, 16, false), "missing flags object");
        result = world_collision_candidates(missing_flags, defs(), alive);
        require(result.state == WorldCollisionCandidatesState::unsupported && result.entities.empty(),
                "missing flags fail closed");
        World missing_guid;
        require(missing_guid.create(1, EntityKind::object) && missing_guid.declare_object_word(1, 8, 0) &&
                    missing_guid.set_position(1, {16, 16, 1, 0}), "missing GUID object");
        result = world_collision_candidates(missing_guid, defs(), alive);
        require(result.state == WorldCollisionCandidatesState::unsupported, "missing GUID fails closed");
        World missing_geometry;
        require(object(missing_geometry, 1, 16), "missing geometry object");
        result = world_collision_candidates(missing_geometry, defs(false), alive);
        require(result.state == WorldCollisionCandidatesState::unsupported, "missing geometry fails closed");
        World ambiguous;
        require(object(ambiguous, 1, 16), "ambiguous definition fixture");
        const auto ambiguous_defs = DefinitionIndex({{"a", "a", 0, defs().unique(100)->definition},
                                                     {"b", "b", 0, defs().unique(100)->definition}});
        result = world_collision_candidates(ambiguous, ambiguous_defs, alive);
        require(result.state == WorldCollisionCandidatesState::unsupported, "ambiguous GUID fails closed");
        World limited;
        require(object(limited, 1, 16) && object(limited, 2, 32), "over-budget fixtures");
        result = world_collision_candidates(limited, defs(), alive, 1);
        require(result.state == WorldCollisionCandidatesState::unsupported &&
                    result.reason == WorldCollisionCandidatesReason::inventory_over_budget,
                "entity count beyond requested cap fails closed");
        result = world_collision_candidates(limited, defs(), alive, 4097);
        require(result.state == WorldCollisionCandidatesState::unsupported &&
                    result.reason == WorldCollisionCandidatesReason::inventory_over_budget,
                "configured cap above hard maximum fails closed");
        require(std::string(world_collision_candidates_state_name(result.state)) == "unsupported" &&
                    std::string(world_collision_candidates_reason_name(result.reason)) == "inventory_over_budget" &&
                    std::string(world_collision_candidate_state_name(WorldCollisionCandidateState::candidate)) == "candidate",
                "public state and reason names are stable");

        std::cout << checks << " live World collision candidate checks passed (synthetic fixtures only)\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "World collision candidate test failed: " << e.what() << '\n';
        return 1;
    }
}
