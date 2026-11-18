/*
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, You can obtain one at
 * http://mozilla.org/MPL/2.0/.
 */
// FSO.SimAntics.Primitives.VMLookTowards (primitives/VMLookTowards.cs)
#include "simantics/primitives/VMLookTowards.h"
#include <cmath>
#include <vector>
#include "core/io.h"
#include "simantics/engine/VMStackFrame.h"
#include "simantics/engine/VMSimanticsException.h"
#include "simantics/engine/VMThread.h"
#include "simantics/engine/VMRoutingFrame.h"
#include "simantics/engine/VMSlotParser.h"
#include "simantics/entities/VMEntity.h"
#include "simantics/entities/VMAvatar.h"
#include "simantics/entities/VMMultitileGroup.h"
#include "simantics/VM.h"
#include "simantics/VMContext.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace FSO { namespace SimAntics { namespace Primitives {

using namespace FSO::SimAntics::Engine;
using FSO::LotView::Model::LotTilePos;
using FSO::Files::Formats::IFF::Chunks::SLOTFlags;

VMPrimitiveExitCode VMLookTowards::Execute(VMStackFrame* context, VMPrimitiveOperand* args)
{
    auto operand = static_cast<VMLookTowardsOperand*>(args);
    auto sim = dynamic_cast<VMAvatar*>(context->Caller);
    if (!sim) throw VMSimanticsException("Look Towards: caller is not an avatar (invalid cast).", context);

    if (context->VM()->TS1) {
        const int mode = static_cast<int16_t>(operand->Mode);
        if (mode < 0 || mode > 3) throw VMSimanticsException("Look Towards: invalid original mode (0x27).", context);
        if (mode == 0) {
            if (!context->Thread->IsCheck) {
                const auto oid = context->StackObjectID();
                sim->SetPersonData(Model::VMPersonDataVariable::HeadSeekObject, oid);
                sim->SetPersonData(Model::VMPersonDataVariable::HeadSeekState, oid != 0);
                sim->SetPersonData(Model::VMPersonDataVariable::HeadSeekFinishAction, 0);
                sim->SetPersonData(Model::VMPersonDataVariable::HeadSeekLimitAction, 1);
                sim->SetPersonData(Model::VMPersonDataVariable::HeadSeekTimeout, 0);
            }
            return VMPrimitiveExitCode::GOTO_TRUE;
        }
        if (context->Thread->IsCheck) return VMPrimitiveExitCode::GOTO_FALSE;
        if (context->PrimitiveState == 0) {
            if (sim->GetPersonData(Model::VMPersonDataVariable::Posture) != 0)
                return VMPrimitiveExitCode::GOTO_FALSE;
            const auto self = sim->Position();
            Routing::TS1MotionPoint target;
            if (mode == 1) {
                int32_t x = self.x, y = self.y;
                const int quadrant = context->Thread->Context->World
                    ? int(context->Thread->Context->World->State.Rotation()) & 3 : 0;
                const auto rotate = [](int32_t& a, int32_t& b, int q) {
                    const auto oldA = a, oldB = b;
                    if (q == 1) { a = 1023 - oldB; b = oldA; }
                    else if (q == 2) { a = 1023 - oldA; b = 1023 - oldB; }
                    else if (q == 3) { a = oldB; b = 1023 - oldA; }
                };
                rotate(x, y, quadrant); x += 16; y += 16; rotate(x, y, (-quadrant) & 3);
                target = {x, y};
                sim->SetValue(Model::VMStackObjectVariable::RoutePrefference, 0);
            } else {
                auto object = context->Thread->Context->VM->GetObjectById(context->StackObjectID());
                if (!object) throw VMSimanticsException("Look Towards: missing original target (0x15).", context);
                target = {object->Position().x, object->Position().y};
                if (mode == 3) {
                    const auto reflect = [](int32_t origin, int32_t point) {
                        const uint32_t bits = uint32_t(origin) * 2u - uint32_t(point);
                        return bits <= 0x7fffffffu ? int32_t(bits) : int32_t(int64_t(bits) - 0x100000000LL);
                    };
                    target = {reflect(self.x, target.X), reflect(self.y, target.Y)};
                }
            }
            const std::vector<Routing::TS1MotionPoint> path{{self.x, self.y}, {self.x, self.y}, target};
            if (!sim->StartTS1MotionRequest(context, Routing::TS1MotionKind::Body, path))
                throw VMSimanticsException("Look Towards: model request is owned by another frame.", context);
            context->PrimitiveState = 1;
        } else if (!sim->OwnsTS1MotionRequest(context)) {
            // A classic/native pre-request continuation polls the original
            // idle model:0 maps to true, then stop succeeds. Do not fabricate
            // a path or confuse it with a malformed new ownership token.
            if (context->TS1MotionToken == 0
                && sim->TS1ModelMotion.Kind == Routing::TS1MotionKind::None)
                return VMPrimitiveExitCode::GOTO_TRUE;
            throw VMSimanticsException("Look Towards: missing saved model request ownership.", context);
        }
        int result = sim->PollTS1MotionRequest(context->TS1MotionToken);
        if (result == 0) result = 1;
        if (result != 2 && sim->StopTS1MotionRequest(context->TS1MotionToken)) {
            sim->DetachTS1MotionRequest(context->TS1MotionToken);
            return VMPrimitiveExitCode::GOTO_TRUE;
        }
        return VMPrimitiveExitCode::CONTINUE_NEXT_TICK;
    }

    // TSO retains its original byte-sized modes, including group averages.
    const auto legacyMode = static_cast<VMLookTowardsMode>(static_cast<uint8_t>(operand->Mode));

    auto result = VMFindLocationResult();
    result.Position = LotTilePos(sim->Position());

    LotTilePos pos = LotTilePos();
    // FIX(sims) QA4: a missing stack object was a null dereference (segfault); C# throws (the Sim resets).
    if (context->StackObject == nullptr && legacyMode != VMLookTowardsMode::HeadTowardsObject && legacyMode != VMLookTowardsMode::BodyTowardsCamera) throw VMSimanticsException("Object reference not set to an instance of an object.", context);
    switch (legacyMode)
    {
        case VMLookTowardsMode::HeadTowardsObject:
            sim->SetPersonData(Model::VMPersonDataVariable::HeadSeekObject, context->StackObjectID());
            sim->SetPersonData(Model::VMPersonDataVariable::HeadSeekState, 1);
            sim->SetPersonData(Model::VMPersonDataVariable::HeadSeekLimitAction, 1);
            sim->SetPersonData(Model::VMPersonDataVariable::HeadSeekFinishAction, 0);
            sim->SetPersonData(Model::VMPersonDataVariable::HeadSeekTimeout, 0);
            return VMPrimitiveExitCode::GOTO_TRUE;
        case VMLookTowardsMode::BodyTowardsCamera:
            return VMPrimitiveExitCode::GOTO_TRUE; //does not work in TSO
        case VMLookTowardsMode::BodyTowardsStackObj:
            result.RadianDirection = (float)GetDirectionTo(sim->Position(), context->StackObject->Position());
            break;
        case VMLookTowardsMode::BodyAwayFromStackObj:
            result.RadianDirection = (float)GetDirectionTo(sim->Position(), context->StackObject->Position());
            result.RadianDirection = (float)std::fmod((result.RadianDirection + M_PI), (M_PI * 2));
            break;
        case VMLookTowardsMode::BodyTowardsAverageStackObj:
            for (auto obj : context->StackObject->MultitileGroup->Objects)
                pos = pos + obj->Position();
            pos = pos / (int)context->StackObject->MultitileGroup->Objects.size();
            result.RadianDirection = (float)GetDirectionTo(sim->Position(), pos);
            break;
        case VMLookTowardsMode::BodyAwayFromAverageStackObj:
            for (auto obj : context->StackObject->MultitileGroup->Objects)
                pos = pos + obj->Position();
            pos = pos / (int)context->StackObject->MultitileGroup->Objects.size();
            result.RadianDirection = (float)GetDirectionTo(sim->Position(), pos);
            result.RadianDirection = (float)std::fmod((result.RadianDirection + M_PI), (M_PI * 2));
            break;
    }

    if (context->Thread->IsCheck) return VMPrimitiveExitCode::GOTO_FALSE;
    auto pathFinder = context->Thread->PushNewRoutingFrame(context, false); //use the path finder to do the turn animation.
    pathFinder->InitRoutes(std::vector<VMFindLocationResult>{ result });

    return VMPrimitiveExitCode::CONTINUE;
}

double VMLookTowards::GetDirectionTo(const LotTilePos& pos1, const LotTilePos& pos2)
{
    return std::atan2((double)(pos2.x - pos1.x), (double)(-(pos2.y - pos1.y)));
}

SLOTFlags VMLookTowards::RadianToFlags(double rad)
{
    // C# Math.Round is banker's rounding (MidpointRounding.ToEven) -> std::nearbyint (default FE_TONEAREST).
    int result = (int)(std::nearbyint((rad / (M_PI * 2)) * 8) + 80) % 8; //for best results, make sure rad is >-pi and <pi
    return (SLOTFlags)(1 << result);
}

void VMLookTowardsOperand::Read(const uint8_t* bytes)
{
    st::Reader io(bytes, 8, st::Endian::Little);
    Mode = static_cast<VMLookTowardsMode>(io.ReadInt16());
}

void VMLookTowardsOperand::Write(uint8_t* bytes)
{
    bytes[0] = (uint8_t)Mode;
    bytes[1] = uint8_t(uint16_t(Mode) >> 8);
}

}}}
