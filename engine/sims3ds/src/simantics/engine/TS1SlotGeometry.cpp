// SPDX-License-Identifier: MPL-2.0
#include "simantics/engine/TS1SlotGeometry.h"
#include "common/TS1/TS1AutonomyFloat.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace FSO { namespace SimAntics { namespace Engine {
using Files::Formats::IFF::Chunks::SLOTItem;
using Common::TS1::AutonomyFloat::Round;
namespace {
float FromBits(uint32_t bits) {
    float value; std::memcpy(&value, &bits, sizeof(value)); return value;
}
bool Truncate(float value, int& result) {
    if (!std::isfinite(value) || double(value) < std::numeric_limits<int32_t>::min() ||
        double(value) > std::numeric_limits<int32_t>::max()) return false;
    result = int(std::trunc(value)); return true;
}
float RoundAway(float value) {
    return value >= 0 ? std::floor(Round(value + .5f))
                      : std::ceil(Round(value - .5f));
}
bool RegionBounds(const SLOTItem& slot) {
    return slot.MinProximity >= 1 && slot.MinProximity <= 512 &&
        slot.MaxProximity >= slot.MinProximity && slot.MaxProximity <= 512 &&
        slot.OptimalProximity >= slot.MinProximity &&
        slot.OptimalProximity <= slot.MaxProximity;
}
int32_t Word(uint32_t value) {
    return value <= 0x7fffffff ? int32_t(value)
        : int32_t(int64_t(value) - INT64_C(0x100000000));
}
int32_t WeightScore(int32_t value) { return Word(uint32_t(value) * 10u); }
uint8_t ExpandedWalls(uint8_t mask) {
    unsigned result = mask;
    for (unsigned i = 0; i < 8; ++i) if (mask & (1u << i))
        result |= (1u << ((i + 7) & 7)) | (1u << ((i + 1) & 7));
    return uint8_t(result);
}
}

bool ProjectTS1SlotPoint(st::Point center, unsigned heading, float x, float y,
                         st::Point& output) {
    if (!std::isfinite(x) || !std::isfinite(y)) return false;
    heading &= 7;
    if (heading == 2 || heading == 6) {
        int saved;
        if (!Truncate(x, saved)) return false;
        const float oldY = y;
        x = heading == 2 ? -oldY : oldY;
        y = heading == 2 ? float(saved) : -float(saved);
    } else if (heading == 4) {
        x = -x; y = -y;
    } else if (heading & 1) {
        // DVD4550b0/5c38c0: angle arithmetic and x87 FSIN/FCOS produce
        // these stored binary32 coefficients. In particular headings5/7 are
        // not identical sign-flipped copies of the heading1 coefficients.
        static constexpr uint32_t cosine[] = {
            0x3f3504f3,0xbf3504f3,0xbf3504f1,0x3f3504f7};
        static constexpr uint32_t sine[] = {
            0x3f3504f3,0x3f3504f3,0xbf3504f5,0xbf3504ef};
        const auto i = heading >> 1;
        const float c = FromBits(cosine[i]), s = FromBits(sine[i]);
        const float vx = Round(y * .1875f), vz = Round(x * .1875f);
        const float nx = Round(Round(vz * s) + Round(vx * c));
        const float nz = Round(Round(vx * -s) + Round(vz * c));
        x = RoundAway(Round(nz * FromBits(0x40aaaaab)));
        y = RoundAway(Round(nx * FromBits(0x40aaaaab)));
    }
    int px, py;
    if (!Truncate(Round(float(center.X) + x), px) ||
        !Truncate(Round(float(center.Y) + y), py)) return false;
    output = st::Point(px, py); return true;
}

size_t VisitTS1SlotRegionCells(const SLOTItem& slot,
                              const TS1SlotGeometryContext& context,
                              const TS1SlotCellVisitor& visit) {
    if (!visit || slot.Type != 3) return 0;
    const uint32_t flags = uint32_t(slot.Rsflags);
    const unsigned mask = flags & 255;
    const unsigned base = flags & 0x200 ? 0 : context.TargetHeading & 7;
    const uint8_t wallMask = flags & 0x800 ? 0 : context.TargetWallMask;
    auto cell = [&](float x, float y, unsigned region, unsigned heading,
                    int radial, int tangent, int end, int step,
                    bool point, TS1SlotRegionCell& out) {
        out.LocalX=x;out.LocalY=y;out.Region=uint8_t(region);out.Heading=uint8_t(heading);
        out.Radial=radial;out.Tangent=tangent;out.TangentEnd=end;out.Step=step;out.PointOnly=point;
        int ix,iy;
        if (!Truncate(x,ix) || !Truncate(y,iy)) return false;
        out.RadiusPoint=st::Point(ix,iy);
        return ProjectTS1SlotPoint(context.Center,heading,x,y,out.Position) &&
            ProjectTS1SlotPoint(context.Center,heading,float(ix),float(iy),out.PreferredPosition);
    };
    if (!mask) {
        TS1SlotRegionCell value;
        if (!cell(slot.Offset.X,slot.Offset.Y,0,base,0,0,0,0,true,value)) return 0;
        visit(value); return 1;
    }
    if (!RegionBounds(slot)) return 0;
    const int32_t sitting = context.AllowSitting ? WeightScore(slot.Sitting) : 0;
    const int step = sitting != 0 ? 16 : slot.Resolution;
    if (step <= 0) return 0; // Outside the source's usable region-descriptor domain.
    const float factor = Round(16.0f / float(step));
    int start,end;
    if (!Truncate(Round(Round(-float(slot.MinProximity)*factor)*.0625f),start) ||
        !Truncate(Round(Round(-float(slot.MaxProximity)*factor)*.0625f),end)) return 0;
    if (sitting > 0) { ++start;--end;if (start >= 0) start=-1; }
    size_t count = 0;
    for (unsigned region = 0; region < 8; ++region) {
        const unsigned bit = 1u << region;
        if (!(mask & bit) || (wallMask & bit)) continue;
        const unsigned heading = (base + region) & 7;
        for (int radial=start;radial>=end;--radial) {
            bool stopAfterRow=false;
            const int tangentEnd=-radial-int(bool(mask & (1u << ((region+2)&7))));
            for (int tangent=radial;tangent<=tangentEnd;++tangent) {
                TS1SlotRegionCell value;
                const float x=Round(float(tangent*step)+slot.Offset.X);
                const float y=Round(float(radial*step)+slot.Offset.Y);
                if (!cell(x,y,bit,heading,radial,tangent,tangentEnd,step,false,value)) continue;
                ++count;
                const auto control=visit(value);
                if (control==TS1SlotRowControl::Stop) return count;
                stopAfterRow |= control==TS1SlotRowControl::StopAfterRow;
            }
            if (stopAfterRow) break;
        }
    }
    return count;
}

bool IsTS1SlotProbeInRange(const SLOTItem& slot, st::Point local) {
    if (!RegionBounds(slot)) return false;
    const int64_t x=local.X,y=local.Y,max=slot.MaxProximity;
    if (x < -max || x > max || y < -max || y > max) return false;
    const int64_t radius2=x*x+y*y;
    return radius2>=int64_t(slot.MinProximity)*slot.MinProximity && radius2<=max*max;
}

int32_t TS1SlotGoalPenalty(int32_t maximumScore, int32_t score) {
    return Word((uint32_t(maximumScore) - uint32_t(score)) << 4);
}

bool ScoreTS1SlotProbe(const SLOTItem& slot, const TS1SlotScoreInput& input,
                       int32_t& output) {
    if (input.PointOnly) { output=100;return true; }
    if (WeightScore(input.Weight)==0 || !IsTS1SlotProbeInRange(slot,input.RadiusPoint)) return false;
    const int64_t x=input.RadiusPoint.X,y=input.RadiusPoint.Y;
    const int64_t radius2=x*x+y*y;
    int radius,penalty;
    if (!Truncate(Round(std::sqrt(float(radius2))),radius) ||
        !Truncate(Round(float(std::abs(radius-slot.OptimalProximity))*slot.Gradient),penalty)) return false;
    uint32_t score=uint32_t(WeightScore(input.Weight))-uint32_t(penalty)+100u;
    const uint32_t flags=uint32_t(slot.Rsflags);
    if (flags & 0x2000) score += input.RandomSample % 20u - 10u;
    const uint8_t walls=flags & 0x800 ? 0 : input.TargetWallMask;
    if (!(ExpandedWalls(walls)&input.Region)) score+=25;
    if (input.Preferred) score+=20;
    output=Word(score);return true;
}

}}}
