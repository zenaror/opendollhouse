// SPDX-License-Identifier: MPL-2.0
// Portable port of FreeSO VMPathLineSegment/VMPathBezierSegment, pinned
// 7e76c1960252ddd033d8937d5edb47d326f5b69c. See src/walk_path.cpp.
#pragma once
#include "odh/rect_router.hpp"
#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace odh {
constexpr std::int32_t walk_path_hip=0x8000;
enum class WalkSegmentKind : std::uint8_t { line, cubic };
struct WalkVector {float x=0,y=0;};
struct WalkSegmentGeometry {
    WalkSegmentKind kind=WalkSegmentKind::line;
    RectRoutePoint a{},b{},c{},d{}; // HiP. Lines use a,d; b,c must be zero.
};
struct WalkSegmentSnapshot {WalkSegmentGeometry geometry{};std::int32_t total_frames=0;};
enum class WalkPathStatus : std::uint8_t { success, invalid_input, unsupported_arithmetic, budget_exhausted };
struct WalkPathLimits {
    std::size_t corridor_entries=4096,segments=4096,work_units=4194304;
};
struct WalkPathResult {
    WalkPathStatus status=WalkPathStatus::invalid_input;
    std::vector<WalkSegmentGeometry> segments;
    std::size_t work_units=0;
};
// Consumes a FOUND optimized corridor. Empty found corridors are successful
// empty paths. Source lineID groups and fractional HiP entries are retained.
// Control generation preserves the pinned net9.0 ToPoint cast semantics,
// including NaN -> zero for zero-length/opposing segment tangents.
// Rendering, routing-frame velocity/turn state and World movement are separate.
WalkPathResult generate_walk_path(const RectRouteLineResult&,RectRoutePoint destination,
    bool bezier,std::optional<float> incoming={},std::optional<float> outgoing={},
    const WalkPathLimits& limits={});
struct WalkFrameResult {WalkPathStatus status=WalkPathStatus::invalid_input;std::int32_t frames=0;};
struct WalkPathSample {
    WalkPathStatus status=WalkPathStatus::invalid_input;
    RectRoutePoint position{}; // Source LotTilePos x/y; caller supplies level.
    WalkVector visual{},velocity{}; // Tile units, matching source Vector2 outputs.
};
class WalkPathSegment {
public:
    explicit WalkPathSegment(WalkSegmentGeometry geometry={}):geometry_(geometry){}
    const WalkSegmentGeometry& geometry() const noexcept{return geometry_;}
    std::int32_t total_frames() const noexcept{return total_frames_;}
    const std::array<float,20>& arc_lengths() const noexcept{return arc_lengths_;}
    std::uint8_t last_arc_index() const noexcept{return last_arc_index_;}
    std::optional<WalkVector> last_velocity() const noexcept{return last_velocity_;}
    // Cubic calculation returns max(1,raw duration) but retains raw duration
    // until update_total_frames, exactly as the source implementation does.
    WalkFrameResult calculate_total_frames() noexcept;
    WalkPathStatus update_total_frames(std::int32_t frames) noexcept;
    void reset_to_frame(std::int32_t frame) noexcept;
    // Supported scheduler domain is 0 <= frame <= total_frames. Nonmonotonic
    // sampling requires reset_to_frame, as in the source routing retry/load.
    WalkPathSample next_point_and_velocity(std::int32_t frame) noexcept;
    WalkSegmentSnapshot snapshot() const noexcept{return {geometry_,total_frames_};}
    // Source marshal: CalculateTotalFrames; UpdateTotalFrames(saved); Reset.
    // Saved duration is clamped to one, including nonpositive source outer
    // durations. Derived lookup/last-velocity caches are never serialized. Failure leaves
    // the existing segment intact.
    WalkPathStatus restore(const WalkSegmentSnapshot&) noexcept;
private:
    WalkSegmentGeometry geometry_{};
    std::int32_t total_frames_=0;
    std::array<float,20> arc_lengths_{};
    std::uint8_t last_arc_index_=0;
    std::optional<WalkVector> last_velocity_;
    bool arc_lengths_ready_=false;
};
// Source AddToPath: line endpoints or thirty cubic subdivisions in tile units.
// Does not mutate playback caches. No partial polyline on failure.
struct WalkPolylineResult {WalkPathStatus status=WalkPathStatus::invalid_input;std::vector<WalkVector> points;};
WalkPolylineResult walk_segment_polyline(const WalkSegmentGeometry&,bool include_start=true);
const char* walk_path_status_name(WalkPathStatus) noexcept;
} // namespace odh
