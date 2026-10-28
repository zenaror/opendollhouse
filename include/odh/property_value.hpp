#pragma once
#include "odh/saved_world.hpp"

namespace odh {
enum class PropertyValueStatus { resolved, unresolved };
enum class PropertyValueReason {
    none, incomplete_group, member_budget, empty_group, invalid_id,
    duplicate_member, leader_absent, missing_entity, missing_identity,
    identity_mismatch, missing_current_value, missing_repair_state
};
struct PropertyGroupValue {
    PropertyValueStatus status = PropertyValueStatus::unresolved;
    PropertyValueReason reason = PropertyValueReason::incomplete_group;
    std::optional<std::int16_t> leader_current_value;
    std::optional<std::int16_t> value;
    bool repair_discount = false;
};
// Non-mutating inspection of comparison-sample00432249's group value rule.
// The caller must establish that the supplied validated group is COMPLETE for
// the current World; this API cannot discover omitted runtime group members.
// Reads live ObjectData41 and every member's ObjectData15, not original saved
// banks. Identity checks do not replace resource/group provenance validation.
// This is neither an asset/refund calculation nor permission to sell a group.
PropertyGroupValue inspect_property_group_value(const World&, const SavedMultitileGroup&,
    bool complete_group, std::size_t member_limit = 4096);
const char* property_value_status_name(PropertyValueStatus) noexcept;
const char* property_value_reason_name(PropertyValueReason) noexcept;
} // namespace odh
