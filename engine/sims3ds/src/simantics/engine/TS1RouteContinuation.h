// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <unordered_map>
#include <vector>
#include "simantics/engine/VMThread.h"
#include "simantics/engine/VMRoutingFrame.h"

namespace FSO { namespace SimAntics { namespace Engine {

// Default records have no payload in the original serializer. They form a
// lower prefix; live native descriptors, if any, form the upper suffix. They
// must never be inferred from an old native save's retained classic template.
inline constexpr uint32_t TS1RouteDefaultsLimit = 1024;

class TS1RouteSavePlan {
public:
    bool DefaultsKnown = false;
    uint32_t DefaultsCount = 0;
    uint32_t ClassicRecordCount = 0;
    bool LeafProjected = false;
    std::vector<std::shared_ptr<VMStackFrame>> NativeFrames;
    std::vector<VMStackFrame*> ClassicFrames;

    int32_t PrimitiveState(const VMStackFrame* frame) const {
        const auto found = primitiveStates.find(frame);
        if (found == primitiveStates.end()) throw std::runtime_error("TS1 route projection: foreign frame");
        return frame == projectedOwner ? 1 : found->second;
    }
    int32_t NativeDepth(int32_t liveDepth) const { return Depth(liveDepth, nativeDepth); }
    int32_t ClassicDepth(int32_t liveDepth) const { return Depth(liveDepth, classicDepth); }

private:
    friend TS1RouteSavePlan BuildTS1RouteSavePlan(const VMThread&, bool, bool);
    const VMStackFrame* projectedOwner = nullptr;
    std::unordered_map<const VMStackFrame*, int32_t> primitiveStates;
    std::vector<int32_t> nativeDepth, classicDepth;
    int32_t Depth(int32_t liveDepth, const std::vector<int32_t>& map) const {
        if (liveDepth <= 0 || size_t(liveDepth) > map.size() || map[size_t(liveDepth-1)] <= 0)
            throw std::runtime_error("TS1 route projection: invalid authored use depth");
        return map[size_t(liveDepth-1)];
    }
};

// A plan is computed without changing live frames, uses, paths or RNG. Both
// checkpoint and classic writers must consume this same projection contract.
inline TS1RouteSavePlan BuildTS1RouteSavePlan(const VMThread& thread, bool ts1,
                                            bool preserveOwnedMotion = false) {
    TS1RouteSavePlan plan;
    if (thread.TS1RouteDefaultsCount > TS1RouteDefaultsLimit ||
        (!thread.TS1RouteDefaultsKnown && thread.TS1RouteDefaultsCount != 0))
        throw std::runtime_error("TS1 route projection: invalid default record provenance");
    plan.DefaultsKnown = ts1 && thread.TS1RouteDefaultsKnown;
    plan.DefaultsCount = plan.DefaultsKnown ? thread.TS1RouteDefaultsCount : 0;
    size_t routes = 0;
    bool ownsMotion = false;
    for (const auto& frame : thread.Stack) {
        if (!frame) throw std::runtime_error("TS1 route projection: null frame");
        if (!frame->Routine) throw std::runtime_error("TS1 route projection: missing frame routine");
        if (!plan.primitiveStates.emplace(frame.get(), frame->PrimitiveState).second)
            throw std::runtime_error("TS1 route projection: duplicate frame");
        if (frame->IsRoutingFrame()) ++routes;
        ownsMotion |= frame->TS1MotionToken != 0;
    }
    // ODHI18 owns model requests by exact native frame identity. Its native
    // graph retains these frames; the independent classic writer keeps its
    // historical count-only leaf projection through the default argument.
    bool eligible = ts1 && !(preserveOwnedMotion && ownsMotion) &&
        !thread.IsCheck && routes == 1 && thread.Stack.size() >= 2 &&
        thread.Stack.back()->IsRoutingFrame();
    if (eligible) {
        const auto& owner = thread.Stack[thread.Stack.size()-2];
        const auto* route = static_cast<const VMRoutingFrame*>(thread.Stack.back().get());
        eligible = !owner->IsRoutingFrame() && owner->Routine &&
            owner->InstructionPointer < owner->Routine->Instructions.size() &&
            (owner->Routine->Instructions[owner->InstructionPointer].Opcode == 27 ||
             owner->Routine->Instructions[owner->InstructionPointer].Opcode == 45) &&
            owner->Thread == &thread && route->Thread == &thread &&
            owner->Routine == route->Routine && owner->Caller == route->Caller &&
            owner->Callee == route->Callee && owner->CodeOwner == route->CodeOwner &&
            owner->StackObjectID() == route->StackObjectID() &&
            route->SpecialResult == VMSpecialResult::Normal &&
            route->State != VMRoutingFrameState::FAILED && route->HeldResult == -1;
        // No use may end at, beyond, or move across the removed descriptor.
        int32_t previous = 0;
        for (const auto& use : thread.TS1ObjectUses) {
            if (use.StackDepth <= previous || use.StackDepth <= 0 ||
                size_t(use.StackDepth) >= thread.Stack.size() ||
                thread.Stack[size_t(use.StackDepth-1)]->IsRoutingFrame()) eligible = false;
            previous = use.StackDepth;
        }
        if (eligible && plan.DefaultsCount < TS1RouteDefaultsLimit) {
            plan.LeafProjected = true;
            plan.projectedOwner = owner.get();
            plan.DefaultsKnown = true;
            ++plan.DefaultsCount;
        }
    }
    const size_t remainingRoutes = routes - (plan.LeafProjected ? 1u : 0u);
    if (remainingRoutes > TS1RouteDefaultsLimit - plan.DefaultsCount)
        throw std::runtime_error("TS1 route projection: too many route records");
    plan.ClassicRecordCount = plan.DefaultsCount + uint32_t(remainingRoutes);
    plan.nativeDepth.resize(thread.Stack.size(), -1);
    plan.classicDepth.resize(thread.Stack.size(), -1);
    for (size_t i = 0; i < thread.Stack.size(); ++i) {
        const auto& frame = thread.Stack[i];
        if (plan.LeafProjected && i+1 == thread.Stack.size()) continue;
        plan.NativeFrames.push_back(frame);
        if (!frame->IsRoutingFrame()) {
            plan.ClassicFrames.push_back(frame.get());
            plan.nativeDepth[i] = int32_t(plan.NativeFrames.size());
            plan.classicDepth[i] = int32_t(plan.ClassicFrames.size());
        }
    }
    return plan;
}

}}}
