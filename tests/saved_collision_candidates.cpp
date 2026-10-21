#include "odh/saved_collision_candidates.hpp"

#include <iostream>
#include <stdexcept>

namespace {
int checks = 0;
void require(bool value, const char* message) {
    ++checks;
    if (!value) throw std::runtime_error(message);
}
odh::SavedWorldEntity record(std::int16_t id, bool avatar = false) {
    odh::SavedWorldEntity result{};
    result.saved.object_id = static_cast<std::uint16_t>(id);
    result.saved.guid = 0x12345678;
    result.saved.prefix.x = 160;
    result.saved.prefix.y = 160;
    result.saved.prefix.level = 1;
    result.saved.prefix.object_data[1] = 0;
    result.definition.guid = result.saved.guid;
    result.definition.tile_width = 32;
    result.definition.footprint_mask = 0;
    result.serialized_object_type = avatar ? 2 : 1;
    if (avatar) result.tail.person.emplace();
    return result;
}
odh::Entity entity(const odh::SavedWorldEntity& saved, bool avatar = false) {
    odh::Entity result{};
    result.id = static_cast<std::int16_t>(saved.saved.object_id);
    result.kind = avatar ? odh::EntityKind::avatar : odh::EntityKind::object;
    result.position = odh::EntityPosition{saved.saved.prefix.x, saved.saved.prefix.y, 1, 0};
    result.definition = odh::EntityType{saved.saved.guid, std::nullopt};
    return result;
}
}

int main() {
    try {
        using odh::SavedCollisionCandidateReason;
        using odh::SavedCollisionCandidateState;
        auto saved = record(1);
        auto restored = entity(saved);
        auto result = odh::saved_collision_candidate(saved, &restored);
        require(result.state == SavedCollisionCandidateState::candidate && result.box == odh::CollisionBox{144,144,176,176} &&
                result.reason == SavedCollisionCandidateReason::none &&
                result.position == odh::EntityPosition{160,160,1,0},
                "uncontained object with complete OBJD yields geometry-only candidate");

        saved.definition.tile_width.reset();
        result = odh::saved_collision_candidate(saved, &restored);
        require(result.state == SavedCollisionCandidateState::unsupported && !result.box &&
                result.reason == SavedCollisionCandidateReason::collision_metadata_missing,
                "missing object collision metadata never becomes a free cell");

        saved = record(1);
        restored = entity(saved);
        saved.saved.prefix.object_data[8] = 4;
        result = odh::saved_collision_candidate(saved, &restored);
        require(result.state == SavedCollisionCandidateState::absent && !result.box,
                "saved HasZeroExtent flag yields no persistent object obstacle");

        saved = record(1);
        restored = entity(saved);
        restored.attachment = odh::Attachment{2,0,false};
        result = odh::saved_collision_candidate(saved, &restored);
        require(result.state == SavedCollisionCandidateState::absent && !result.box,
                "contained object does not contribute a persistent obstacle");

        saved = record(2, true);
        restored = entity(saved, true);
        result = odh::saved_collision_candidate(saved, &restored);
        require(result.state == SavedCollisionCandidateState::unsupported &&
                result.reason == SavedCollisionCandidateReason::avatar_kill_timeout_unavailable &&
                result.box == odh::CollisionBox{157,157,163,163},
                "avatar box is retained as geometry but KillTimeout uncertainty remains unsupported");
        saved.saved.prefix.object_data[8] = 4;
        result = odh::saved_collision_candidate(saved, &restored);
        require(result.state == SavedCollisionCandidateState::candidate && result.box,
                "avatar HasZeroExtent state resolves the Simitone KillTimeout obstacle branch");

        saved = record(3);
        restored = entity(saved);
        saved.saved.prefix.object_data[40] = static_cast<std::int16_t>(0x8000);
        result = odh::saved_collision_candidate(saved, &restored);
        require(result.state == SavedCollisionCandidateState::unsupported && result.box &&
                result.reason == SavedCollisionCandidateReason::architectural_door_runtime_state_unavailable,
                "architectural door geometry is not treated as static runtime clearance");
        saved.saved.prefix.object_data[8] = 4;
        result = odh::saved_collision_candidate(saved, &restored);
        require(result.state == SavedCollisionCandidateState::unsupported && !result.box &&
                result.reason == SavedCollisionCandidateReason::architectural_door_runtime_state_unavailable,
                "architectural door runtime uncertainty takes precedence over HasZeroExtent absence");

        saved = record(4);
        restored = entity(saved);
        restored.position->x++;
        result = odh::saved_collision_candidate(saved, &restored);
        require(result.state == SavedCollisionCandidateState::unsupported &&
                result.reason == SavedCollisionCandidateReason::world_position_mismatch,
                "live position divergence invalidates the saved snapshot candidate");

        result = odh::saved_collision_candidate(saved, nullptr);
        require(result.state == SavedCollisionCandidateState::unsupported &&
                result.reason == SavedCollisionCandidateReason::entity_missing,
                "missing restored entity is unsupported");

        std::cout << checks << " saved collision candidate checks passed (geometry only)\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Saved collision candidate test failed: " << error.what() << '\n';
        return 1;
    }
}
