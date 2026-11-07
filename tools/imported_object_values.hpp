// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "odh/saved_world.hpp"
#include "odh/session_bundle.hpp"
#include "odh/object_group.hpp"
#include <functional>

namespace odh_host {
using ImportedObjectSourceKey=std::function<std::string(const std::string&)>;
// Fresh original-IFF import ONLY, before source EP2 or other simulation writes.
// Does not seed reopened native sessions, execute Init, read OD41, or mutate
// World. Host supplies the portable source-key policy for its selected game.
// Authenticates each retained imported object against World and exact IFF/FAR
// OBJD. Groups retain source order, individual masters and signed saved offsets;
// price origin may differ from BaseObject and no common master is invented.
// Missing/ambiguous master leaves InitialPrice unknown; malformed/changed
// member resources, incomplete groups or budgets fail before returning state.
odh::SessionObjectTradeLedger prepare_imported_object_values(const odh::SavedWorld&,
    const odh::DefinitionIndex&,ImportedObjectSourceKey);
// Source CreateObjectInstance admission, before the first member Init. The
// requested group definition, not any individual part, owns the catalog lookup:
// an authenticated missing catalog entry has InitialPrice0. No purchase/debit,
// imported-OBJD price policy or World mutation occurs. Returned group_id0 is
// allocated by ObjectValueRegistry::admit; member source order stays unchanged.
odh::SessionObjectValueGroup prepare_constructed_object_values(const odh::World&,
    const odh::DefinitionIndex&,const odh::ObjectGroup&,ImportedObjectSourceKey);
}
