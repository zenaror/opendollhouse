#pragma once
#include "odh/prepared_architecture.hpp"
#include "odh/room_map.hpp"
#include <functional>
#include <string>

namespace odh {
// TS1 subset of pinned VMArchitectureCommand: the seven wall/surface commands.
enum class ArchitectureEditType : std::uint8_t {
    wall_line, wall_delete, wall_rect, pattern_dot, pattern_fill, floor_rect, floor_fill
};
struct ArchitectureEditCommand {
    ArchitectureEditType type=ArchitectureEditType::wall_line;
    std::int32_t x=0,y=0,x2=0,y2=0;
    std::int8_t level=1;
    std::uint16_t pattern=0,style=0;
};
enum class ArchitectureEditMaterialKind { wall_style, wallpaper, floor };
enum class ArchitectureEditLookup { unknown, absent, available };
struct ArchitectureEditMaterial {
    ArchitectureEditLookup status=ArchitectureEditLookup::unknown;
    std::int32_t price=0;
    bool purchasable=false; // Source command admission; distinct from the five UI styles.
};
struct ArchitectureEditRectangleClip {
    std::int32_t x=0,y=0,width=0,height=0;
    std::int8_t levels=2;
};
struct ArchitectureEditServices {
    std::optional<ArchitectureEditRectangleClip> rectangle_clip;
    std::function<ArchitectureEditMaterial(ArchitectureEditMaterialKind,std::uint16_t)> material;
    // Values are in the prepared/runtime resource domain. These callbacks must
    // be pure. null means missing authority, false means a source-valid denial.
    std::function<std::optional<bool>(std::int32_t,std::int32_t,std::int8_t)> in_clip;
    std::function<std::optional<bool>(const PreparedArchitecture&,std::int32_t,std::int32_t,std::int8_t,const SavedWallTile&)> wall_valid;
    std::function<std::optional<bool>(const PreparedArchitecture&,std::int32_t,std::int32_t,std::int8_t,std::uint16_t)> floor_valid;
    std::function<std::optional<bool>(std::int32_t,std::int32_t,std::int8_t)> supported;
    std::function<std::optional<bool>(std::int32_t,std::int32_t,std::int8_t)> below_outside;
};
struct ArchitectureEditLimits {
    std::size_t commands=4096,work=1024*1024;
    std::uint32_t dimension=64;
};
enum class ArchitectureEditStatus { ready, unsupported, invalid, budget };
struct ArchitectureEditReceipt {
    std::size_t command=0;
    std::int32_t changed_units=0,cost=0;
    bool rejected=false,geometry_written=false;
};
struct ArchitectureEditResult {
    ArchitectureEditStatus status=ArchitectureEditStatus::invalid;
    std::string diagnostic;
    std::optional<PreparedArchitecture> candidate;
    std::optional<GeometricRoomMap> rooms;
    std::vector<ArchitectureEditReceipt> receipts;
    std::int32_t cost=0; // Positive charge, negative refund, source int32 math.
};
// RegenerateSupported(level2) from an authenticated ObjectSupport ground grid.
// ObjectSupport is the source category8 event state, NOT SupportStrength and
// NOT inferred occupancy. Missing/malformed authority returns absence.
std::optional<std::vector<std::uint8_t>> regenerate_architecture_support(
    const PreparedArchitecture&,const GeometricRoomMap&,
    const std::vector<std::uint8_t>& ground_object_support,
    std::string* diagnostic=nullptr);

// Atomic candidate preparation only: original grids, World and funds unchanged.
// Money approval and publication are one caller-owned journal transaction.
ArchitectureEditResult prepare_architecture_edit(const PreparedArchitecture&,
    const std::vector<ArchitectureEditCommand>&,const ArchitectureEditServices&,
    const ArchitectureEditLimits& = {});
} // namespace odh
