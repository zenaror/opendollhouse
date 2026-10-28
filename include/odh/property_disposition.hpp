#pragma once
#include "odh/definition_index.hpp"

namespace odh {
enum class PropertyDisposition { retain, remove, unresolved };
enum class PropertyDispositionReason {
    retained_flag, actor, build_mode, buy_mode,
    missing_flags, missing_definition, ambiguous_definition,
    missing_master, missing_build_mode, type_mismatch, invalid_definition
};
struct PropertyDispositionInspection {
    PropertyDisposition disposition = PropertyDisposition::unresolved;
    PropertyDispositionReason reason = PropertyDispositionReason::missing_definition;
    std::optional<std::uint32_t> effective_guid;
    std::optional<std::uint16_t> effective_build_mode;
};
// Non-mutating comparison-sample predicate inspection, NOT eviction authority.
// Movement flags must be current ObjectData43 (not stale saved banks in a live
// World). type is the separately decoded instance type. Missing/ambiguous
// resources never become ordinary buy objects. Master resolution is restricted
// to the same explicit resource. Does not authorize group/actor deletion,
// containment changes, inventory changes or any money/save transaction.
PropertyDispositionInspection inspect_property_disposition(
    const DefinitionIndex&, std::uint32_t guid, std::uint16_t instance_type,
    std::optional<std::uint16_t> movement_flags);
const char* property_disposition_name(PropertyDisposition) noexcept;
const char* property_disposition_reason_name(PropertyDispositionReason) noexcept;
} // namespace odh
