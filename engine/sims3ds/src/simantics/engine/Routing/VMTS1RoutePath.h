// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "simantics/engine/Routing/VMTS1MotionRequest.h"

namespace FSO { namespace SimAntics { namespace Engine { namespace Routing {

// DVD45c4fa appends orientation after the physical path has been selected.
// The final point supplies heading only; it is never a walked destination.
// Inputs are native logical X,Y rather than the original memory's Y,X.
struct TS1RouteFacing {
    int32_t Facing = -3;
    uint32_t Flags = 0;
    TS1MotionPoint Target;
    unsigned TargetHeading = 0, CallerHeading = 0;
};

// Failure leaves the original physical path intact. The original requires at
// least two physical nodes and rejects an orientation point equal to the goal.
bool TS1AppendRouteFacing(std::vector<TS1MotionPoint>& path,
                         const TS1RouteFacing& facing);

}}}}
