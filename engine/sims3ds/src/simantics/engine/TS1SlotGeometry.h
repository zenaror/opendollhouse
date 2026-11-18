// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "simantics/_files_contract.h"
#include "core/math.h"
#include <cstddef>
#include <cstdint>
#include <functional>

namespace FSO { namespace SimAntics { namespace Engine {

// Pure parts of the DVD's authored SLOT generator. Scene admission, chair
// lookup/probe order and bounded random reduction belong to the consumer.
struct TS1SlotGeometryContext {
    st::Point Center;
    unsigned TargetHeading = 0;
    uint8_t TargetWallMask = 0;
    bool AllowSitting = true;       // Original caller person-data65 < 2.
};

struct TS1SlotRegionCell {
    st::Point Position;             // Projection of the float local offset.
    st::Point PreferredPosition;    // Projection after local truncation.
    st::Point RadiusPoint;          // Truncated local coordinates, before rotation.
    float LocalX = 0, LocalY = 0;
    int Radial = 0, Tangent = 0, TangentEnd = 0, Step = 0;
    uint8_t Region = 0, Heading = 0;
    bool PointOnly = false;
};

enum class TS1SlotRowControl { Continue, StopAfterRow, Stop };
using TS1SlotCellVisitor = std::function<TS1SlotRowControl(const TS1SlotRegionCell&)>;

// Input/output use native logical X,Y; the original's memory stores Y,X.
// False means the coordinate cannot be represented, not a scene rejection.
bool ProjectTS1SlotPoint(st::Point center, unsigned heading, float localX,
                         float localY, st::Point& output);

// The low-mask-zero point branch bypasses region bounds/step validation.
// Status3 from scene admission must request StopAfterRow: all remaining
// tangents of that row still run before later radial rows are skipped.
size_t VisitTS1SlotRegionCells(const Files::Formats::IFF::Chunks::SLOTItem& slot,
                              const TS1SlotGeometryContext& context,
                              const TS1SlotCellVisitor& visit);

struct TS1SlotScoreInput {
    st::Point RadiusPoint;
    int32_t Weight = 0;             // Standing/Ground or Sitting, before *10.
    uint8_t Region = 0;
    uint8_t TargetWallMask = 0;
    bool Preferred = false;
    // Caller obtains the draw only for an eligible probe with bit0x2000 set.
    // This pure function neither draws nor changes the simulation RNG.
    uint32_t RandomSample = 0;
    bool PointOnly = false;
};

// Check before consuming a RandomScoring draw. Point-only slots bypass this.
bool IsTS1SlotProbeInRange(const Files::Formats::IFF::Chunks::SLOTItem& slot,
                           st::Point local);

bool ScoreTS1SlotProbe(const Files::Formats::IFF::Chunks::SLOTItem& slot,
                       const TS1SlotScoreInput& input, int32_t& output);

// DVD4829d6/4829d8: signed DWORD difference followed by a four-bit shift.
// Keep the machine's modulo32 result without a signed-overflow conversion.
int32_t TS1SlotGoalPenalty(int32_t maximumScore, int32_t score);

// DVD452129 uses integer 2:1 sectors; threshold ties remain diagonal.
int TS1SlotHeadingBetween(st::Point from, st::Point to);

}}}
