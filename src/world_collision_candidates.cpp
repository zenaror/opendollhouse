#include "odh/world_collision_candidates.hpp"

#include <utility>

namespace odh {
namespace {
constexpr std::int16_t flags_index = 8;
constexpr std::uint16_t has_zero_extent = 1U << 2;

struct SnapshotEntity {
    std::int16_t id;
    EntityKind kind;
    std::uint64_t generation;
    std::optional<EntityPosition> position;
    std::optional<Attachment> attachment;
    std::optional<std::uint32_t> guid;
    std::optional<std::int16_t> flags;
    bool static_footprint;
    std::optional<std::int32_t> kill_timeout;
};

std::optional<std::int16_t> flags_of(const Entity& entity) {
    const auto found = entity.object_words.find(flags_index);
    return found == entity.object_words.end() ? std::nullopt
        : std::optional<std::int16_t>{found->second};
}

SnapshotEntity capture(const Entity& entity) {
    return {entity.id, entity.kind, entity.generation, entity.position, entity.attachment,
        entity.definition ? std::optional<std::uint32_t>{entity.definition->guid} : std::nullopt,
        flags_of(entity), entity.static_footprint, entity.kill_timeout};
}

bool same_attachment(const std::optional<Attachment>& a,
                     const std::optional<Attachment>& b) noexcept {
    return a.has_value() == b.has_value() &&
        (!a || (a->container == b->container && a->slot == b->slot && a->positioned == b->positioned));
}

bool same_position(const std::optional<EntityPosition>& a,
                   const std::optional<EntityPosition>& b) noexcept {
    return a.has_value() == b.has_value() && (!a || *a == *b);
}

bool matches(const World& world, const std::vector<SnapshotEntity>& snapshot) {
    std::size_t i = 0;
    std::int16_t cursor = 0;
    while (const auto next = world.next_entity(cursor)) {
        cursor = *next;
        if (i == snapshot.size()) return false;
        const auto* entity = world.find(cursor);
        if (!entity) return false;
        const auto now = capture(*entity);
        const auto& was = snapshot[i++];
        if (now.id != was.id || now.kind != was.kind || now.generation != was.generation ||
            !same_position(now.position, was.position) || !same_attachment(now.attachment, was.attachment) ||
            now.guid != was.guid || now.flags != was.flags ||
            now.static_footprint != was.static_footprint || now.kill_timeout != was.kill_timeout)
            return false;
    }
    return i == snapshot.size();
}

WorldCollisionCandidates failure(WorldCollisionCandidatesReason reason) {
    return {WorldCollisionCandidatesState::unsupported, reason, {}};
}
}

WorldCollisionCandidates world_collision_candidates(
    const World& world, const DefinitionIndex& definitions,
    const AvatarKillTimeoutResolver& kill_timeout_for, std::size_t entity_limit) {
    if (!entity_limit || entity_limit > 4096)
        return failure(WorldCollisionCandidatesReason::inventory_over_budget);

    std::vector<SnapshotEntity> snapshot;
    std::int16_t cursor = 0;
    while (const auto next = world.next_entity(cursor)) {
        cursor = *next;
        if (snapshot.size() >= entity_limit)
            return failure(WorldCollisionCandidatesReason::inventory_over_budget);
        const auto* entity = world.find(cursor);
        if (!entity) return failure(WorldCollisionCandidatesReason::inventory_incomplete);
        snapshot.push_back(capture(*entity));
    }

    std::vector<WorldCollisionCandidate> results;
    results.reserve(snapshot.size());
    for (const auto& entity : snapshot) {
        WorldCollisionCandidate result{entity.id, entity.kind, WorldCollisionCandidateState::absent,
                                       std::nullopt, entity.position, entity.static_footprint};
        // This follows query_world_room_info: unattached entities with no world
        // position are outside RoomInfo's obstacle inventory.
        if (!entity.position || entity.attachment) {
            result.position.reset();
            results.push_back(result);
            continue;
        }
        if (!entity.flags) return failure(WorldCollisionCandidatesReason::inventory_incomplete);
        const auto flags = static_cast<std::uint16_t>(*entity.flags);
        const bool zero_extent = (flags & has_zero_extent) != 0;
        CollisionShapeResult shape{};
        if (entity.kind == EntityKind::object) {
            if (zero_extent) { results.push_back(result); continue; }
            if (!entity.guid) return failure(WorldCollisionCandidatesReason::inventory_incomplete);
            const auto* definition = definitions.unique(*entity.guid);
            if (!definition) return failure(WorldCollisionCandidatesReason::inventory_incomplete);
            shape = object_collision_shape(definition->definition, *entity.position, flags, false);
        } else if (entity.kind == EntityKind::avatar) {
            if (!kill_timeout_for) return failure(WorldCollisionCandidatesReason::inventory_incomplete);
            const auto timeout = kill_timeout_for(entity.id, entity.generation);
            if (!timeout) return failure(WorldCollisionCandidatesReason::inventory_incomplete);
            // Dying avatars have no persistent obstacle unless HasZeroExtent is set.
            if (*timeout > -1 && !zero_extent) { results.push_back(result); continue; }
            shape = avatar_candidate_collision_shape(*entity.position);
        } else {
            return failure(WorldCollisionCandidatesReason::inventory_incomplete);
        }
        if (shape.state == CollisionShapeState::unsupported)
            return failure(WorldCollisionCandidatesReason::inventory_incomplete);
        if (shape.state == CollisionShapeState::present) {
            result.state = WorldCollisionCandidateState::candidate;
            result.box = shape.box;
        }
        results.push_back(result);
    }

    if (!matches(world, snapshot))
        return failure(WorldCollisionCandidatesReason::inventory_incomplete);
    return {WorldCollisionCandidatesState::complete, WorldCollisionCandidatesReason::none,
            std::move(results)};
}

const char* world_collision_candidates_state_name(WorldCollisionCandidatesState state) noexcept {
    switch (state) {
    case WorldCollisionCandidatesState::complete: return "complete";
    case WorldCollisionCandidatesState::unsupported: return "unsupported";
    }
    return "unsupported";
}
const char* world_collision_candidates_reason_name(WorldCollisionCandidatesReason reason) noexcept {
    switch (reason) {
    case WorldCollisionCandidatesReason::none: return "none";
    case WorldCollisionCandidatesReason::inventory_incomplete: return "inventory_incomplete";
    case WorldCollisionCandidatesReason::inventory_over_budget: return "inventory_over_budget";
    }
    return "invalid_reason";
}
const char* world_collision_candidate_state_name(WorldCollisionCandidateState state) noexcept {
    switch (state) {
    case WorldCollisionCandidateState::candidate: return "candidate";
    case WorldCollisionCandidateState::absent: return "absent";
    }
    return "invalid_state";
}
} // namespace odh
