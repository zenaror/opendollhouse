/*
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, You can obtain one at
 * http://mozilla.org/MPL/2.0/.
 */
// Translated from FreeSims SimsVille/SimsAntics/primitives/VMGotoRelativePosition.cs
#include "simantics/primitives/VMGotoRelativePosition.h"
#include <cstring>
#include <memory>
#include <algorithm>
#include "core/io.h"
#include "simantics/engine/VMStackFrame.h"
#include "simantics/engine/VMThread.h"
#include "simantics/engine/VMRoutingFrame.h"
#include "simantics/engine/VMSimanticsException.h"
#include "simantics/entities/VMEntity.h"
#include "simantics/entities/VMAvatar.h"

namespace FSO { namespace SimAntics { namespace Primitives {

using namespace FSO::SimAntics::Engine;
using FSO::LotView::Model::LotTilePos;
using FSO::Files::Formats::IFF::Chunks::SLOTItem;
using FSO::Files::Formats::IFF::Chunks::SLOTFlags;
using FSO::Files::Formats::IFF::Chunks::SLOTFacing;

VMPrimitiveExitCode VMGotoRelativePosition::Execute(VMStackFrame* context, VMPrimitiveOperand* args)
{
    auto operand = static_cast<VMGotoRelativePositionOperand*>(args);

    if (context->Thread->IsCheck) return VMPrimitiveExitCode::GOTO_FALSE;

    VMPrimitiveExitCode resumed;
    if (context->Thread->TryResumeTS1Route(context, resumed)) return resumed;

    const auto direction = static_cast<int32_t>(operand->Direction);
    if (direction < -2 || direction > 7) return VMPrimitiveExitCode::GOTO_FALSE;

    auto obj = context->StackObject;
    if (obj == nullptr) return VMPrimitiveExitCode::GOTO_FALSE;
    // C#: var avatar = (VMAvatar)context.Caller; (unused, but the cast throws InvalidCastException for non-avatars)
    auto avatar = dynamic_cast<VMAvatar*>(context->Caller);
    if (context->Caller != nullptr && avatar == nullptr)
        throw VMSimanticsException("Specified cast is not valid. (VMAvatar)", context);
    (void)avatar;

    if (obj->Position() == LotTilePos::OUT_OF_WORLD) return VMPrimitiveExitCode::GOTO_FALSE;

    auto slotOwner = std::unique_ptr<SLOTItem>(new SLOTItem(CreateSlot(*operand)));

    auto pathFinder = context->Thread->PushNewRoutingFrame(context, !operand->NoFailureTrees());
    pathFinder->OwnedSlot = std::move(slotOwner);
    auto success = pathFinder->InitRoutes(pathFinder->OwnedSlot.get(), context->StackObject);
    (void)success;

    return VMPrimitiveExitCode::CONTINUE;
}

SLOTItem VMGotoRelativePosition::CreateSlot(const VMGotoRelativePositionOperand& operand)
{
    SLOTItem slot;
    slot.Type = 3;
    slot.Standing = 1;
    slot.RoutingPolicy = FSO::Files::Formats::IFF::Chunks::SLOTRoutingPolicy::TS1RelativePosition;
    slot.Gradient = 3.0f / 16.0f;
    slot.MinProximity = slot.MaxProximity = slot.OptimalProximity = 16;
    const auto location = static_cast<int32_t>(operand.Location);
    if (location >= 0 && location < 8) {
        // The PC primitive combines the two neighboring cardinal regions for
        // an odd location; it does not request a single diagonal destination.
        const auto mask = (location & 1)
            ? (1u << (location - 1)) | (1u << ((location + 1) & 7))
            : 1u << location;
        slot.Rsflags |= static_cast<SLOTFlags>(mask);
    } else if (operand.Location != VMGotoRelativeLocation::OnTopOf) {
        // All other encoded locations use the original anywhere-near fallback.
        slot.MaxProximity = 32;
        slot.Rsflags |= static_cast<SLOTFlags>(0x55);
    }
    if (!operand.NoFailureTrees()) slot.Rsflags |= static_cast<SLOTFlags>(0x4000);
    if (operand.AllowDiffAlt()) slot.Rsflags |= SLOTFlags::AllowDifferentAlts;
    slot.Facing = (operand.Direction == VMGotoRelativeDirection::AnyDirection)
        ? SLOTFacing::FaceAnywhere : (SLOTFacing)operand.Direction;
    return slot;
}

// region VMPrimitiveOperand Members
void VMGotoRelativePositionOperand::Read(const uint8_t* bytes)
{
    st::Reader io(bytes, VMPrimitiveOperand::OPERAND_SIZE, st::Endian::Little);
    OldTrapCount = io.ReadUInt16();
    Location = (VMGotoRelativeLocation)((int8_t)io.ReadByte());
    Direction = (VMGotoRelativeDirection)((int8_t)io.ReadByte());
    RouteCount = io.ReadUInt16();
    Flags = (VMGotoRelativeFlags)io.ReadByte();
}

void VMGotoRelativePositionOperand::Write(uint8_t* bytes)
{
    st::Writer io(st::Endian::Little);
    io.WriteUInt16(OldTrapCount);
    io.WriteByte((uint8_t)Location);
    io.WriteByte((uint8_t)Direction);
    io.WriteUInt16(RouteCount);
    io.WriteByte((uint8_t)Flags);
    std::memcpy(bytes, io.ToArray().data(), std::min(io.ToArray().size(), VMPrimitiveOperand::OPERAND_SIZE));
}
// endregion

}}}
