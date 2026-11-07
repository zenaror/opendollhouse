// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "saved_object_renderer.hpp"
#include "odh/buy_mode.hpp"

namespace odh_host {
// Authored resource preview, not a simulated initial state: Graphic0=0 and
// empty dynamic flags, with no World, Init, entity allocation or price publication.
// requested must be the exact unique candidate owned by definitions. Expands
// abstract masters using their exact IFF chunk order (at most 64 actual parts);
// direct subparts remain singletons. IDs are local presentation tokens only.
// Only direction 0 is supported: rotated group offsets are not inferred.
// Missing/ambiguous/changed resources and unsupported directions throw FormatError.
std::vector<SavedObjectVisual> prepare_catalog_object_visuals(
    const odh::DefinitionCandidate& requested,const odh::DefinitionIndex& definitions,
    std::uint8_t direction=0);
// Placement preview retains the planner's actual base and signed VM offsets;
// rotation/terrain projection happens later. Reads authored Graphic0 only,
// never constructs a World or executes initialization. This resource-only
// path retains legacy construction offsets; it does not claim native unsigned
// low-byte Y (for example253), signed16 Z, or post-Init/ghost-preview parity.
// Native existing-object previews instead carry exact live host-supplied
// authored fields and relative headings in BuyModePreviewGroup.
odh::BuyModePreviewGroup prepare_catalog_buy_group(
    const odh::DefinitionCandidate&,const odh::DefinitionIndex&);
}
