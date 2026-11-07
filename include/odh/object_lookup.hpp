// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "odh/dynamic_object_groups.hpp"

namespace odh {
enum class ObjectLookupTableStatus { unavailable,absent,present };
struct ObjectLookupTable {
    ObjectLookupTableStatus status=ObjectLookupTableStatus::unavailable;
    const Ttab* table=nullptr;
};
struct ObjectLookupServices {
    // Same actual group/World owner, not another membership cache. Null admits
    // only a proven OBJD.MasterID==0; unknown legacy MT stays unavailable.
    DynamicObjectGroups* groups=nullptr;
    // Exact immutable resource cache. The provider must keep a present borrowed
    // table alive and unchanged for the WHOLE synchronous query. No pointer is
    // retained in the result; absent and unavailable are deliberately distinct.
    std::function<ObjectLookupTable(WorldEntityReference)> table_for;
    // Read-only authority checks must not mutate World/groups/resource owners.
    std::function<bool()> authority;
};
struct ObjectLookupEligibility {
    WorldEntityReference reference{},interaction_group{};
    // Unshifted source selector masks, not a fabricated complete native cache
    // DWORD: mask2=OD60!=0; mask8=self interaction group with an eligible TTAB row.
    std::uint16_t flags=0;
};
struct ObjectLookupResult {
    std::optional<ObjectLookupEligibility> eligibility;
    std::string diagnostic;
};
// Internal original-source profile of431edd/4359a1. Derives actual current
// eligibility for the native inventory consumer; does NOT implement44582f's
// mutable World lookup cache or write any mask/instance bank. Exact retained
// bodies are readable, matching postLoad; the inventory owner decides live
// enumeration. Unknown source data is unavailable, never known-false.
ObjectLookupResult object_lookup_eligibility(const World&,WorldEntityReference,const ObjectLookupServices&);
} // namespace odh
