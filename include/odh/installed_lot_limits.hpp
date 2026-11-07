#pragma once
#include "odh/saved_world.hpp"
#include "odh/saved_objects.hpp"

namespace odh {
// Application import policy, not library defaults or a wire-format change.
// Retained source fields, decoded parser fields and live World fields are
// different accounting domains. None of these counts measures process RSS.
inline WorldLimits installed_lot_world_limits() {
    WorldLimits limits;
    limits.words = 524288;
    return limits;
}
struct InstalledLotLimits {
    odh::SavedPrefixLimits prefix{};
    std::uint64_t common_words = 131072;
    std::uint64_t tail_words = 65536;
    std::size_t definition_catalogs = 512;
    std::size_t physical_resource_banks = 512;
    std::size_t entrypoint_programs = 512;
    odh::SavedWorldLimits saved_world = [] {
        odh::SavedWorldLimits limits;
        limits.world = installed_lot_world_limits();
        limits.retained_words = 262144;
        return limits;
    }();
};
inline InstalledLotLimits installed_lot_limits() { return {}; }
} // namespace odh
