// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "simantics/model/VMMotiveChange.h"
#include "core/io.h"
#include <array>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <vector>
#include <map>

namespace FSO { namespace SimAntics { namespace Engine { namespace Utils { namespace MotivePersistence {
// Frozen ODHI2-8 wire fields. Do not serialize runtime classes: float banks and
// ordered changes deliberately have no effect on this legacy byte layout.
struct LegacyChange {
    int16_t rate = 0, stop = 0;
    uint32_t motive = 0;
    bool ticked = false;
    double residual = 0;
};
struct State {
    std::array<float,16> current{}, old{};
    std::vector<Model::VMTS1MotiveChange> changes;
    int64_t lastTick = 0;
};
struct ActorPhase { uint32_t guid; int16_t neighbour; int64_t lastTick; };
struct Marker {
    bool matching = false;
    std::map<int16_t,ActorPhase> actors;
};
inline Marker ReadMarker(const std::vector<uint8_t>& bytes, uint32_t classicHash,
                         uint32_t nativeHash, size_t nativeBytes, int64_t savedTick) {
    if (bytes.size() > 20 + 32767 * 16)
        throw std::runtime_error("Interaction checkpoint: motive marker exceeds limit");
    st::Reader input(bytes);
    const auto schema = input.u32(), classic = input.u32(), native = input.u32(), extent = input.u32();
    Marker result;
    // A legacy saver can retain the unknown marker after changing either bound
    // payload. Its old schema/body/phase has no authority over that new save.
    if (classic != classicHash || native != nativeHash || extent != nativeBytes) return result;
    if (schema != 1) throw std::runtime_error("Interaction checkpoint: unsupported matching motive marker schema");
    const auto count = input.u32();
    if (count > 32767 || bytes.size() != 20 + size_t(count) * 16)
        throw std::runtime_error("Interaction checkpoint: invalid motive marker extent");
    for (uint32_t i = 0; i < count; ++i) {
        const auto id = input.s16(); const auto guid = input.u32(); const auto neighbour = input.s16();
        uint64_t phase = input.u32(); phase |= uint64_t(input.u32()) << 32;
        if (id <= 0 || !guid || savedTick < 0 || phase > uint64_t(savedTick) ||
            !result.actors.emplace(id, ActorPhase{guid,neighbour,int64_t(phase)}).second)
            throw std::runtime_error("Interaction checkpoint: invalid motive marker identity or phase");
    }
    result.matching = true;
    return result;
}
inline void ValidateFloat(float value) {
    if (!std::isfinite(value)) throw std::runtime_error("Interaction checkpoint: non-finite motive precision");
}
inline void ValidateCurrent(float value) {
    ValidateFloat(value);
    if (value < -32768.f || value > 32767.f)
        throw std::runtime_error("Interaction checkpoint: saved motive outside script range");
}
inline void ValidateChange(const Model::VMTS1MotiveChange& change) {
    if (uint32_t(change.Motive) >= 16) throw std::runtime_error("Interaction checkpoint: invalid motive precision ID");
    ValidateFloat(change.TickDelta); ValidateFloat(change.StopAt);
    // Preserve the existing classic import envelope, without quantizing the
    // actual float rate/stop. Removing integer storage is not permission to
    // accept a corrupt update outside the script input range.
    const double hourly = std::round(double(change.TickDelta) * 1800.);
    if (hourly < -32768. || hourly > 32767. || change.StopAt < -32768.f || change.StopAt > 32767.f)
        throw std::runtime_error("Interaction checkpoint: classic motive change outside script range");
}
inline State Migrate(const std::array<int16_t,16>& motives,
                     const std::array<LegacyChange,16>& changes,
                     const std::array<int16_t,7>& decay, int64_t savedTick) {
    State result;
    for (size_t i = 0; i < 16; ++i) result.current[i] = float(motives[i]);
    for (const auto& change : changes) {
        if (change.motive >= 16 || !std::isfinite(change.residual))
            throw std::runtime_error("Interaction checkpoint: invalid legacy motive change");
        if (!change.rate) continue; // Clear() historically left its residual behind.
        auto& value = result.current[change.motive];
        const bool progressing = change.rate > 0 ? value < change.stop : value > change.stop;
        // Only a pending sub-unit remainder can represent unfinished progress.
        // Over-stop whole-unit growth was a legacy bug, never saved credit.
        // A newly installed (unticked) record cannot prove whether its old
        // remainder survived Clear()/reinstall. Keep its rate, discard that
        // ambiguous credit rather than reviving a previous action's carry.
        if (change.ticked && progressing && std::abs(change.residual) < 1 &&
            ((change.rate > 0 && change.residual > 0) || (change.rate < 0 && change.residual < 0))) {
            const float pending = float(change.residual);
            value = change.rate > 0 ? std::min(float(change.stop), value + pending) :
                                      std::max(float(change.stop), value + pending);
        }
        // Persisted positive Energy rates may already have been scaled. Their
        // provenance is absent; changing them here would invent another rate.
        result.changes.push_back({Model::VMMotive(change.motive), float(change.rate) / 1800.f, float(change.stop)});
    }
    constexpr int ids[7] = {7,6,8,9,5,15,14};
    for (size_t i = 0; i < 7; ++i)
        if (std::abs(int(decay[i])) < 1000) result.current[ids[i]] -= float(decay[i]) / 1000.f;
    for (float value : result.current) ValidateCurrent(value);
    // Neither the old bank nor the true 60-tick phase existed in ODHI8.
    // Snapshot now and wait one interval: no fabricated meal or catch-up burst.
    result.old = result.current; result.lastTick = savedTick;
    return result;
}
}}}}}
