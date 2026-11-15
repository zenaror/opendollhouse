/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. See https://mozilla.org/MPL/2.0/. */
#pragma once
#include "simantics/_files_contract.h"
#include <algorithm>
#include <cmath>

namespace FSO { namespace SimAntics { namespace Engine {

inline bool SlotProximityValid(int min, int max, double distance)
{
    return distance >= min - 0.5 && distance <= max + 0.5;
}

inline st::Vector2 SlotFacingCenter(int flags, bool multiTile, st::Vector2 objectCenter, st::Vector2 groupCenter)
{
    return (flags & 65536) != 0 && multiTile ? groupCenter : objectCenter;
}
inline float SlotSearchRotation(int flags, bool onPoint, float objectDirection)
{
    return ((flags & 512) != 0 && !onPoint) ? 0.0f : objectDirection;
}
inline int8_t SlotDestinationLevel(int8_t baseLevel, float offsetZ)
{
    return (int8_t)(baseLevel + (int)std::floor(offsetZ / 15));
}
template<typename NextRandom>
double SlotBaseScore(int flags, double maxScore, int desired, double distance, NextRandom nextRandom)
{
    double result = nextRandom(1024) / 1024.0f;
    if ((flags & 8192) == 0) result += maxScore - std::abs(desired - distance);
    return result;
}
inline double SlotCallerDistanceScore(int flags, double score, double distance)
{
    return ((flags & 8192) == 0) ? score - distance / 3.0 : score;
}
inline double SlotStandingScore(const FSO::Files::Formats::IFF::Chunks::SLOTItem& slot,
                                bool chair, double score)
{
    return slot.SnapTargetSlot < 0 ? score * (chair ? slot.Sitting : slot.Standing) : score;
}

// SLOTEnumerationFunction from the pinned FreeSO VMSlotParser. The callback
// receives the authored proximity, which need not equal rounded-point length.
template<typename Callback>
void EnumerateSlotLocations(const FSO::Files::Formats::IFF::Chunks::SLOTItem& slot,
                            int maxProximity, Callback output)
{
    // Test the raw fields: max=0 is normalized by the parser, not by this test.
    if (slot.MaxProximity == slot.MinProximity) {
        for (int i = 0; i < 8; ++i) {
            const double angle = (i * M_PI) / 4.0;
            // C# Math.Round defaults to ties-to-even, as does nearbyint in the
            // engine's default floating-point rounding mode.
            const int x = (int)std::nearbyint(std::sin(angle) * slot.MinProximity);
            const int y = (int)std::nearbyint(std::cos(angle) * slot.MinProximity);
            output(x, y, (double)slot.MinProximity);
        }
    } else {
        const int bound = (maxProximity / slot.Resolution) * slot.Resolution;
        const bool square = ((int32_t)slot.Rsflags & (1 << 30)) != 0; // FSOSquare
        for (int x = -bound; x <= bound; x += slot.Resolution) {
            for (int y = -bound; y <= bound; y += slot.Resolution) {
                const double distance = square ? (double)std::max(std::abs(x), std::abs(y))
                    : std::sqrt((double)(x * x + y * y));
                output(x, y, distance);
            }
        }
    }
}

}}}
