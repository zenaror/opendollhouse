// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "vitaboy/VitaboyMath.h"

namespace FSO { namespace Vitaboy {

// TS1's quaternion blend (DVD 005939a0). The weight belongs to the FIRST
// quaternion. Keep the caller's destination for weights outside both endpoint
// windows; the original does not clamp them. Copies also make input/output
// aliasing safe. Binary32 intermediate stores reproduce the source's PC24
// arithmetic without relying on the host's floating-point control word.
inline float TS1QuaternionRound(float value) {
    volatile float rounded = value;
    return rounded;
}

inline void TS1BlendQuaternion(const Quaternion& first, const Quaternion& second,
    float firstWeight, Quaternion& destination) {
    constexpr float endpoint = 0.01f;
    constexpr float normalizationTolerance = 0.00001f;
    const Quaternion a = first, b = second;
    if (firstWeight < 0.99f && firstWeight > endpoint) {
        float otherWeight = TS1QuaternionRound(1.0f - firstWeight);
        // The source accumulates its sign test in X, W, Z, Y order.
        float dot = TS1QuaternionRound(
            TS1QuaternionRound(a.X * b.X) + TS1QuaternionRound(a.W * b.W));
        dot = TS1QuaternionRound(dot + TS1QuaternionRound(a.Z * b.Z));
        dot = TS1QuaternionRound(dot + TS1QuaternionRound(b.Y * a.Y));
        if (dot < 0.0f) otherWeight = -otherWeight;
        auto blend = [firstWeight, otherWeight](float x, float y) {
            return TS1QuaternionRound(TS1QuaternionRound(otherWeight * y)
                + TS1QuaternionRound(firstWeight * x));
        };
        Quaternion result(blend(a.X, b.X), blend(a.Y, b.Y),
            blend(a.Z, b.Z), blend(a.W, b.W));
        float lengthSquared = TS1QuaternionRound(
            TS1QuaternionRound(result.X * result.X) + TS1QuaternionRound(result.Y * result.Y));
        lengthSquared = TS1QuaternionRound(lengthSquared + TS1QuaternionRound(result.Z * result.Z));
        lengthSquared = TS1QuaternionRound(lengthSquared + TS1QuaternionRound(result.W * result.W));
        if (std::fabs(TS1QuaternionRound(lengthSquared - 1.0f)) > normalizationTolerance) {
            if (std::fabs(lengthSquared) > normalizationTolerance) {
                const float inverse = TS1QuaternionRound(1.0f / TS1QuaternionRound(std::sqrt(lengthSquared)));
                result.X = TS1QuaternionRound(inverse * result.X);
                result.Y = TS1QuaternionRound(inverse * result.Y);
                result.Z = TS1QuaternionRound(inverse * result.Z);
                result.W = TS1QuaternionRound(inverse * result.W);
            } else result = Quaternion::Identity();
        }
        destination = result;
    } else if (std::fabs(TS1QuaternionRound(firstWeight - 1.0f)) <= endpoint) {
        destination = a;
    } else if (std::fabs(firstWeight) <= endpoint) {
        destination = b;
    }
}

}}
