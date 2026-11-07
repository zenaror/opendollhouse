#pragma once
#include "odh/property_disposition.hpp"
#include "odh/property_value.hpp"

namespace odh {
enum class PropertyObjectAssetsStatus { resolved, unresolved };
enum class PropertyObjectAssetBucket { unresolved, excluded, primary, architecture_objects };
enum class PropertyObjectAssetsReason {
    none, incomplete_inventory, invalid_dimension, inventory_budget,
    invalid_partition, missing_identity, ambiguous_definition, identity_mismatch,
    missing_master, definition_type_mismatch, missing_flags, missing_build_mode,
    invalid_definition, group_value_unresolved, arithmetic_overflow,
    own_type7, primary_candidate, excluded_build0, excluded_build4, architecture_fallback
};
struct PropertyObjectAssetGroup {
    std::int16_t leader=0;
    PropertyObjectAssetBucket bucket=PropertyObjectAssetBucket::unresolved;
    PropertyObjectAssetsReason reason=PropertyObjectAssetsReason::none;
    std::optional<bool> in_world;
    std::optional<std::int16_t> value;
    PropertyValueReason value_reason=PropertyValueReason::none;
};
struct PropertyObjectAssets {
    PropertyObjectAssetsStatus status=PropertyObjectAssetsStatus::unresolved;
    PropertyObjectAssetsReason reason=PropertyObjectAssetsReason::incomplete_inventory;
    std::optional<std::int32_t> primary, architecture_objects;
    std::vector<PropertyObjectAssetGroup> groups;
};
// Comparison00444cf9's two OBJECT value buckets only, not the complete asset or
// refund formula. No deletion, money, execution or save publication occurs.
// Supply a COMPLETE current group partition, INCLUDING singletons. The caller
// certifies actual runtime group membership/root semantics; identities cannot
// detect an omitted member disguised as another otherwise valid group.
// Groups need not share a definition asset/master. Each member's own GUID and
// canonical multitile master are checked separately against the index. An
// optional group.master_guid asserts homogeneity and must match every member;
// absence does not assert that the group is a singleton or all masters absent.
// All non-UI-ghost live IDs must occur exactly once (maximum4096). Uses current
// definition identities, ObjectData43/41/15 and each root's OWN position. Saved
// records/banks, EntityKind and parent positions never replace those inputs.
// Spatial comparison00422665 uses signed low-byte floor(coord/16), ignores
// level, and accepts tiles1..dimension-2. Absent position is out of world.
// Any unresolved result leaves BOTH totals absent; per-group rows may explain
// partial classifications but are never an authoritative partial total.
PropertyObjectAssets inspect_property_object_assets(const World&, const DefinitionIndex&,
    const std::vector<SavedMultitileGroup>& complete_partition,
    bool complete_inventory, unsigned dimension);
// Exposes the comparison coordinate rule independently of World's stricter
// position setter, so negative/raw coordinates can be inspected without
// creating an invalid World. Invalid dimensions return false.
bool property_object_position_in_world(const std::optional<EntityPosition>&, unsigned dimension) noexcept;
const char* property_object_assets_reason_name(PropertyObjectAssetsReason) noexcept;
const char* property_object_asset_bucket_name(PropertyObjectAssetBucket) noexcept;
} // namespace odh
