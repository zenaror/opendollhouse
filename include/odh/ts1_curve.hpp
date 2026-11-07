// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "odh/animation.hpp"
#include <array>
#include <string_view>
namespace odh {
struct Ts1CurvePoint {std::int32_t x=0,y=0;};
struct Ts1CurveLimits {std::size_t text_bytes=65536,points=4096;};
// Pinned TS1Curve: stable order by X, last point at equal X, float interpolation.
class Ts1Curve {
public:
    explicit Ts1Curve(std::vector<Ts1CurvePoint>);
    float point(float input) const;
    const std::vector<Ts1CurvePoint>& points() const noexcept {return points_;}
private:std::vector<Ts1CurvePoint> points_;
};
Ts1Curve parse_ts1_curve(std::string_view,const Ts1CurveLimits& = {});
using Ts1InteractionCurves=std::array<Ts1Curve,9>;
// Global STR501 (adult) / STR503 (child), English/default source data.
Ts1InteractionCurves read_ts1_interaction_curves(const StringTable&,const Ts1CurveLimits& = {});
}
