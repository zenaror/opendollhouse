/*
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, You can obtain one at
 * http://mozilla.org/MPL/2.0/.
 */
// FSO.SimAntics.Primitives.VMGetTerrainInfo (primitives/VMGetTerrainInfo.cs)
#include "simantics/primitives/VMGetTerrainInfo.h"
#include <cstring>
#include <algorithm>
#include "core/io.h"
#include "world/model/LotTilePos.h"
#include "simantics/engine/VMStackFrame.h"
#include "simantics/engine/VMThread.h"
#include "simantics/engine/VMSimanticsException.h"
#include "simantics/entities/VMEntity.h"
#include "simantics/VM.h"
#include "simantics/VMContext.h"
#include "simantics/VMArchitecture.h"

namespace FSO { namespace SimAntics { namespace Primitives {
using namespace FSO::SimAntics::Engine;
using FSO::LotView::Model::LotTilePos;

VMPrimitiveExitCode VMGetTerrainInfo::Execute(VMStackFrame* context, VMPrimitiveOperand* args) {
    auto operand = static_cast<VMGetTerrainInfoOperand*>(args);
    auto obj = (operand->UseMe()) ? context->Caller : context->StackObject;
    // C# NullReferenceException on obj.Position -> exception the thread catches.
    if (obj == nullptr) throw VMSimanticsException("NullReferenceException", context);

    if (obj->Position() == LotTilePos::OUT_OF_WORLD) return VMPrimitiveExitCode::GOTO_FALSE;

    for (int i = 0; i < 5; i++) {
        if (operand->Unknown.at(i) != 0) { }
    }

    switch (operand->Mode) {
        case 0: //four vertex altitudes, in TS1 units
        case 1: { //slope in lot axes, independent of object/camera rotation
            auto arch = context->VM()->Context->Architecture();
            if (arch == nullptr || arch->Terrain == nullptr)
                throw VMSimanticsException("NullReferenceException", context);
            const auto pos = obj->Position();
            const int16_t x = pos.TileX(), y = pos.TileY();
            const int16_t nextX = (int16_t)std::min<int32_t>(arch->Width - 1, (int32_t)x + 1);
            const int16_t nextY = (int16_t)std::min<int32_t>(arch->Height - 1, (int32_t)y + 1);
            const auto height = [&](int16_t tx, int16_t ty) -> int {
                // FreeSO GetOffset casts its linear index to ushort. Preserve
                // that mapping, including aliases, but replace unchecked C++
                // access with the exception the C# array would raise.
                if (arch->GetOffset(tx, ty) >= arch->Terrain->Heights.size())
                    throw VMSimanticsException("IndexOutOfRangeException", context);
                return arch->GetTerrainHeight(tx, ty);
            };
            if (operand->Mode == 0) {
                context->Thread->TempRegisters[0] = (int16_t)(height(x, y) / 10);
                context->Thread->TempRegisters[1] = (int16_t)(height(nextX, y) / 10);
                context->Thread->TempRegisters[2] = (int16_t)(height(x, nextY) / 10);
                context->Thread->TempRegisters[3] = (int16_t)(height(nextX, nextY) / 10);
            } else {
                const int h1 = height(x, y), h2 = height(nextX, y);
                const int h3 = height(x, nextY), h4 = height(nextX, nextY);
                // Both C# and C++ integer division truncate toward zero;
                // differences stay in int until the final short result.
                context->Thread->TempRegisters[0] = (int16_t)(((h1 - h2) + (h3 - h4)) / 20);
                context->Thread->TempRegisters[1] = (int16_t)(((h3 - h1) + (h4 - h2)) / 20);
            }
            break;
        }
        case 2: { //fixed coordinates of stack object

            //VERIFIED
            auto pos = obj->Position();
            context->Thread->TempRegisters[0] = pos.TileX(); //tile x
            context->Thread->TempRegisters[1] = (int16_t)(pos.x % 16); //sub-tile x
            context->Thread->TempRegisters[2] = pos.TileY(); //tile y
            context->Thread->TempRegisters[3] = (int16_t)(pos.y % 16); //sub-tile y
            context->Thread->TempRegisters[4] = pos.Level; //level

            break;
        }
        case 3: //grass height under stack object
            // 0-255, depending on terrain colour. (dead-alive)
            context->Thread->TempRegisters[0] = 0;
            //used for extra ball friction on (short) grass.
            break;
        case 4: //ceiling or roof exists above stack object. unused.
            context->Thread->TempRegisters[0] = 0;
            break;
    }
    return VMPrimitiveExitCode::GOTO_TRUE;
}

void VMGetTerrainInfoOperand::Read(const uint8_t* bytes) {
    st::Reader io(bytes, OPERAND_SIZE, st::Endian::Little);
    Mode = io.u8();
    Unknown1 = io.u8();
    Flags = io.u8();
    Unknown = io.bytes(5);
}

void VMGetTerrainInfoOperand::Write(uint8_t* bytes) {
    // C# writes only Mode, Unknown1 and Flags (Unknown is not written back).
    st::Writer io(st::Endian::Little);
    io.WriteByte(Mode);
    io.WriteByte(Unknown1);
    io.WriteByte(Flags);
    auto& a = io.ToArray();
    std::memcpy(bytes, a.data(), std::min(a.size(), OPERAND_SIZE));
}

}}}
