// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <cmath>

namespace FSO { namespace SimAntics { namespace Engine { namespace Routing {

// DVD model 00588fd4 and the pre-FSM update at 0058dc76. Model states
// are source state numbers here, not VMRoutingFrameState enum values.
inline float TS1TargetMotionRate(int walkStyle, int modelState, int species) {
    float factor = 1.0f;
    if (walkStyle == 1 && (modelState == 0 || (modelState >= 4 && modelState <= 9)))
        factor = (modelState != 0 && species > 1) ? 0.0f : 8.0f;
    return 1.2f + 0.2f * factor;
}
inline float TS1AdvanceMotionRate(float current, float target) {
    // Only increasing rates are limited; decreasing rates change immediately.
    return current < target && (current < target - 0.2f || target + 0.2f < current)
        ? current + 0.2f : target;
}

struct TS1ConstrainedPoint { int X = 0, Y = 0; };

// DVD 0048329c constrains physical placement to the selected path segment.
// 0045eecb interpolates END toward START, then truncates(value + 0.5).
// The model's float proposal remains separate from this integer position.
inline TS1ConstrainedPoint TS1ConstrainToSegment(int x, int y,
    int startX, int startY, int endX, int endY) {
    TS1ConstrainedPoint result{startX, startY};
    const double dx = double(startX) - endX, dy = double(startY) - endY;
    const double lengthSquared = dx * dx + dy * dy;
    if (!(lengthSquared > 0)) return result;
    float t = static_cast<float>(((double(y) - endY) * dy + (double(x) - endX) * dx) / lengthSquared);
    t = t < 0 ? 0 : t > 1 ? 1 : t;
    const int sampleX = int(endX + dx * t + 0.5);
    const int sampleY = int(endY + dy * t + 0.5);
    const double startDX = double(startX) - x, startDY = double(startY) - y;
    const double sampleDX = double(sampleX) - x, sampleDY = double(sampleY) - y;
    if (float(sampleDX * sampleDX + sampleDY * sampleDY) <
        float(startDX * startDX + startDY * startDY)) {
        result.X = sampleX; result.Y = sampleY;
    }
    return result;
}

struct TS1LookaheadSample {
    bool Found = false;
    int X = 0, Y = 0;
};

// Source 0058e4e4..0058eb35 projects onto the current path segment, then
// samples one position unit farther along it until outside half the radius.
// Exhausting the segment advances the cursor instead of forcing a stop at
// every corner. Float stores and integer truncation precede the distance test.
inline TS1LookaheadSample TS1SegmentLookahead(float x, float y, int startX, int startY,
    int endX, int endY, float radius) {
    TS1LookaheadSample result;
    const float dx = float(endX - startX), dy = float(endY - startY);
    const float lengthSquared = dx * dx + dy * dy;
    if (!(lengthSquared > 0) || !std::isfinite(lengthSquared) ||
        !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(radius) || radius < 0) return result;
    const float length = std::sqrt(lengthSquared);
    float projection = ((x - startX) * dx + (y - startY) * dy) / lengthSquared;
    projection = projection < 0 ? 0 : projection > 1 ? 1 : projection;
    const float increment = 1.0f / length;
    const float threshold = radius * 0.5f * radius * 0.5f;
    while (projection < 1) {
        const int sampleX = int(startX + projection * dx);
        const int sampleY = int(startY + projection * dy);
        const float px = sampleX - x, py = sampleY - y;
        if (threshold <= px * px + py * py) {
            result.Found = true; result.X = sampleX; result.Y = sampleY; return result;
        }
        const float next = projection + increment;
        if (!(next > projection)) return result;
        projection = next;
    }
    return result;
}

struct TS1TurnSelection {
    bool Animate = false;
    int ClipCode = 10;
    float Difference = 0; // eighth-turn units, normalized to [-4,4).
    float AuthoredTurn = 0;
    float Residual = 0;
};

// DVD state3 at 0058df41/0058f4f2. These boundaries select the authored
// root-turn clip; state4 applies only the residual to the model heading.
inline TS1TurnSelection TS1SelectTurn(float difference, float rate, int species) {
    TS1TurnSelection result;
    if (!std::isfinite(difference) || !std::isfinite(rate) || rate <= 0) return result;
    difference = std::fmod(difference, 8.0f);
    if (difference >= 4.0f) difference -= 8.0f;
    if (difference < -4.0f) difference += 8.0f;
    result.Difference = difference;
    const float threshold = (species > 1 ? 0.0625f : 0.25f) / rate;
    if (difference < threshold && difference > -threshold) return result;
    result.Animate = true;
    if (difference < -3.0f) { result.ClipCode = 5; result.AuthoredTurn = -4; }
    else if (difference > 3.0f) { result.ClipCode = 4; result.AuthoredTurn = 4; }
    else if (difference < -1.5f) { result.ClipCode = 7; result.AuthoredTurn = -2; }
    else if (difference > 1.5f) { result.ClipCode = 6; result.AuthoredTurn = 2; }
    else if (difference < -0.5f) { result.ClipCode = 9; result.AuthoredTurn = -1; }
    else if (difference > 0.5f) { result.ClipCode = 8; result.AuthoredTurn = 1; }
    result.Residual = difference - result.AuthoredTurn;
    return result;
}

// DVD walking states 9/10 at 0058edf0. Asset Distance is in the Vitaboy
// coordinate scale; positions use sixteenths of a lot tile. The nominal
// animation tick is 42ms even when the model's clock advances by 50ms.
inline float TS1MotionStep(float distance, float durationMs, float rate) {
    if (!std::isfinite(distance) || !std::isfinite(durationMs) || !std::isfinite(rate) ||
        durationMs <= 0.0f || rate <= 0.0f || distance < 0.0f) return 0.0f;
    const float displacement = distance * (1.0f / 3.0f) * 16.0f;
    const float ticks = (durationMs / rate) / 42.0f;
    if (!std::isfinite(displacement) || !std::isfinite(ticks) || !(ticks > 0.0f)) return 0.0f;
    const float magnitude = displacement / ticks;
    return std::isfinite(magnitude) && magnitude >= 0.0f ? magnitude : 0.0f;
}

}}}}
