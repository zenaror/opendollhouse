// SPDX-License-Identifier: MPL-2.0
#include "simantics/engine/Routing/VMTS1RoutePath.h"
#include "simantics/engine/TS1SlotGeometry.h"

namespace FSO { namespace SimAntics { namespace Engine { namespace Routing {
namespace {
int32_t SignedWord(uint32_t bits) {
    return bits <= 0x7fffffffu ? int32_t(bits)
        : int32_t(int64_t(bits)-INT64_C(0x100000000));
}
TS1MotionPoint AddHeading(TS1MotionPoint p, unsigned heading) {
    // DVD4222bd scales BOTH direction-table components by16, including
    // diagonals. A normalized sine/cosine vector would shorten those steps.
    static constexpr int32_t offset[8][2]={{0,-16},{16,-16},{16,0},{16,16},
        {0,16},{-16,16},{-16,0},{-16,-16}};
    return {SignedWord(uint32_t(p.X)+uint32_t(offset[heading&7][0])),
            SignedWord(uint32_t(p.Y)+uint32_t(offset[heading&7][1]))};
}
}

bool TS1AppendRouteFacing(std::vector<TS1MotionPoint>& path,
                         const TS1RouteFacing& input) {
    if (path.size()<2) return false;
    const auto goal=path.back();
    auto tangent=goal;
    if (input.Facing==-3) {
        const auto previous=path[path.size()-2];
        if (goal.X==previous.X && goal.Y==previous.Y) {
            tangent=AddHeading(goal,input.CallerHeading);
        } else {
            tangent={SignedWord(uint32_t(goal.X)*2u-uint32_t(previous.X)),
                     SignedWord(uint32_t(goal.Y)*2u-uint32_t(previous.Y))};
        }
    } else if (input.Facing==-1 || input.Facing==-2) {
        if ((input.Flags&255u) && (input.Flags&0x100u)) {
            // The original direct-target branch precedes the opposite-facing
            // adjustment, and uses the actual target rather than group center.
            tangent=input.Target;
        } else {
            const auto heading=goal.X==input.Target.X && goal.Y==input.Target.Y
                ? int(input.TargetHeading&7)
                : TS1SlotHeadingBetween({goal.X,goal.Y},{input.Target.X,input.Target.Y});
            tangent=AddHeading(goal,unsigned(heading+(input.Facing==-1?4:0)));
        }
    } else {
        tangent=AddHeading(goal,input.TargetHeading+uint32_t(input.Facing));
    }
    if (goal.X==tangent.X && goal.Y==tangent.Y) return false;
    path.push_back(tangent);
    return true;
}

}}}}
