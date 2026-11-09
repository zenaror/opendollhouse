// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "odh/formats.hpp"

namespace odh {
// Effective pinned FreeSO SLOT.Read height, WITHOUT changing raw fields.
// The record must belong to this supported table. Versions4..7 have no raw
// height field; their source runtime default is5, as is explicit raw height0.
// Missing required extensions, foreign records and unknown versions stay unknown.
std::optional<std::int32_t> source_slot_height(const SlotTable&, const SlotRecord&) noexcept;
} // namespace odh
