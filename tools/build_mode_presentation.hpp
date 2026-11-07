// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "odh/architecture_edit.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace odh_host {
// Ephemeral host presentation across internal lot reloads, never saved World
// data. IDs are exact prepared-catalog identities, in wall_style/wallpaper/floor
// order. Absence is valid only when that material category has no entries.
struct BuildModePresentationState {
    odh::ArchitectureEditType tool=odh::ArchitectureEditType::wall_line;
    std::array<std::optional<std::uint16_t>,3> selected_material_ids;
    std::size_t page=0; // Zero-based page in the active tool's material category.
};
}
