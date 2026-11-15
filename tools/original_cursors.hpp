// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "odh/cursor.hpp"
#include <array>
#include <filesystem>
#include <string_view>

namespace odh_host {
// Asset identities, preserving original file stems. These are not inferred
// gameplay meanings: the host must bind contexts from proven source behavior.
enum class OriginalCursorAsset {
    ADDPTS,Arrow,BULLDOZE,DEADEXPL,DOWN,DOWNLEFT,DOWNRIGHT,EXPLORE,HELP,HOURGLASS,LEFT,
    LiveNothing,LiveNothingPet,LiveObjectAvail,LiveObjectUnavail,LivePerson,LivePet,MOVEIN,NODROP,Pan,RIGHT,
    RemovePts,Rezone,Rezone2,SimsDelete,SimsFlip,SimsLevel,SimsLower,SimsMove,SimsPlace,SimsRaise,
    SimsRotate,SimsRotateNE,SimsRotateNW,SimsRotateSE,SimsRotateSW,UP,UPLEFT,UPRIGHT,
    balloon,edit,finger,hand,hole,limo,ship,taxi,tram,trolleybus,van,water,count
};
constexpr std::size_t original_cursor_count=static_cast<std::size_t>(OriginalCursorAsset::count);
struct OriginalCursorLimits {
    odh::CursorLimits cursor;
    std::uint32_t pixels=65536,directory_entries=512;
};
const char* original_cursor_filename(OriginalCursorAsset);
std::optional<OriginalCursorAsset> original_cursor_asset_for_path(std::string_view);
enum class OriginalCursorStatus {ready,unsupported_icon_without_hotspot};
// Eager owned bank: only the fixed allowlist under the canonical installation
// is loaded; no file reads occur in at(). A missing/ambiguous/bad cursor fails.
// The shipped MOVEIN.cur exception is an ICO, not a CUR: it has no authenticated
// cursor hotspot, so its status is unsupported and its images remain empty.
class OriginalCursors {
public:
    explicit OriginalCursors(const std::filesystem::path& game_root,OriginalCursorLimits={});
    const odh::WindowsCursor& at(OriginalCursorAsset)const;
    // Normalized installed resource path, never arbitrary filesystem I/O.
    // Unknown paths and the authenticated MOVEIN ICO exception return null.
    const odh::CursorImage* image_for_path(std::string_view path)const;
    OriginalCursorStatus status(OriginalCursorAsset)const;
    const char* diagnostic(OriginalCursorAsset)const;
private:
    std::array<odh::WindowsCursor,original_cursor_count> cursors_;
    std::array<OriginalCursorStatus,original_cursor_count> status_{};
};
}
