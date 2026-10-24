#include "odh/household_arrival.hpp"

namespace odh {
HouseholdArrivalPreflight preflight_staged_household_arrival(
    const World& world, std::int16_t avatar_id, std::uint16_t lot_width,
    std::uint16_t lot_height, const HouseholdArrivalAssessor& assess) {
    HouseholdArrivalPreflight result;
    const auto* avatar = world.find(avatar_id);
    if (!avatar || avatar->kind != EntityKind::avatar || !avatar->definition ||
        avatar->definition->guid == 0 || avatar->position || avatar->attachment || avatar->ui_ghost) {
        result.diagnostic = "arrival requires an unpositioned unattached staged avatar";
        return result;
    }
    if (!assess) {
        result.diagnostic = "arrival requires a complete placement assessor";
        return result;
    }
    const Entity* mailbox = nullptr;
    auto cursor = std::int16_t{0};
    while (const auto id = world.next_entity(cursor, EntityKind::object)) {
        cursor = *id;
        const auto* entity = world.find(*id);
        if (!entity || entity->ui_ghost || !entity->definition) continue;
        const auto guid = entity->definition->guid;
        if (guid != 0xef121974U && guid != 0x1d95c9b0U) continue;
        if (mailbox || !entity->position || entity->attachment) {
            result.diagnostic = "arrival mailbox is ambiguous, unpositioned or attached";
            return result;
        }
        mailbox = entity;
    }
    if (!mailbox) {
        result.diagnostic = "arrival mailbox is unavailable";
        return result;
    }
    result.mailbox_id = mailbox->id;
    // Invalid bounds/coordinates are malformed host input and throw FormatError,
    // just as household_spawn_candidates does. No state has been changed.
    const auto candidates = household_spawn_candidates(*mailbox->position, lot_width, lot_height);
    for (const auto& candidate : candidates) {
        ++result.attempts;
        // Simitone still calls SetPosition for horizontally out-of-bounds
        // candidates; PositionValid rejects those before the checks below.
        // Preserve the attempt order without asking the read-only assessor to
        // claim evidence for a position the known lot bounds already block.
        // The lot's story count is not an input here; the floor gate owns that.
        if (candidate.x < 0 || candidate.y < 0 || candidate.level < 1 ||
            candidate.x / 16 >= lot_width || candidate.y / 16 >= lot_height) continue;
        const auto assessment = assess(world, avatar_id, candidate);
        const auto cell = evaluate_avatar_candidate_preflight(assessment.cell);
        if (cell.status == AvatarCandidatePreflightStatus::blocked ||
            assessment.position_change == CandidateCellGateStatus::blocked) continue;
        if (cell.status != AvatarCandidatePreflightStatus::clear ||
            assessment.position_change != CandidateCellGateStatus::allowed) {
            result.diagnostic = "arrival cell or PositionChange semantics are unsupported";
            return result;
        }
        result.status = AvatarCandidatePreflightStatus::clear;
        result.position = candidate;
        return result;
    }
    result.status = AvatarCandidatePreflightStatus::blocked;
    result.diagnostic = "all mailbox arrival candidates are blocked";
    return result;
}
} // namespace odh
