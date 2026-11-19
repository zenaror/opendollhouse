/*
* This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
* If a copy of the MPL was not distributed with this file, You can obtain one at
* http://mozilla.org/MPL/2.0/. 
*/
// Translated from FreeSims sims.files/formats/iff/chunks/BHAV.cs
#include "files/formats/iff/chunks/BHAV.h"
#include "files/formats/iff/IffFile.h"
#include <stdexcept>

namespace FSO { namespace Files { namespace Formats { namespace IFF { namespace Chunks {

void BHAV::Read(IffFile* iff, st::Reader& stream)
{
    (void)iff;
    st::Reader& io = stream;
    io.ByteOrder(st::Endian::Little);
    {
        auto version = io.ReadUInt16();
        Type = Args = 0; Locals = Flags = 0;
        PreviousDecoderLocals = 0; SourceParsedArgs = SourceParsedLocals = 0;
        SourceFormatVersion = version;
        uint32_t count = 0;

        if (version == 0x8000)
        {
            count = io.ReadUInt16();
            io.Skip(8);
            this->Args = 4; // DVD474add: the oldest format defaults to four.
        }
        else if (version == 0x8001)
        {
            count = io.ReadUInt16();
            const auto metadata = io.ReadBytes(8);
            if (metadata.size() != 8) throw std::runtime_error("Truncated BHAV 8001 header");
            // DVD474acc..df: wire byte5 declares args, capped at four.
            this->Args = metadata[1] < 4 ? metadata[1] : 4;
            this->Locals = metadata[2] < 4 ? metadata[2] : 4;
        }
        else if (version == 0x8002)
        {
            count = io.ReadUInt16();
            this->Type = io.ReadByte();
            this->Args = io.ReadByte();
            PreviousDecoderLocals = io.ReadUInt16();
            this->Locals = PreviousDecoderLocals & 255; // DVD474a90: wire byte6 only.
            this->Flags = io.ReadUInt16();
            io.Skip(2);
        }
        else if (version == 0x8003)
        {
            this->Type = io.ReadByte();
            this->Args = io.ReadByte();
            this->Locals = io.ReadByte();
            io.Skip(2);
            this->Flags = io.ReadUInt16();
            count = io.ReadUInt32();
        }
        SourceParsedArgs = Args;
        SourceParsedLocals = uint8_t(Locals);
        if (version == 0x8003) PreviousDecoderLocals = Locals;

        Instructions = std::vector<BHAVInstruction>(count);
        for (uint32_t i = 0; i < count; i++)
        {
            BHAVInstruction instruction;
            instruction.Opcode = io.ReadUInt16();
            instruction.TruePointer = io.ReadByte();
            instruction.FalsePointer = io.ReadByte();
            auto operand = io.ReadBytes(8);
            for (size_t j = 0; j < 8 && j < operand.size(); j++) instruction.Operand[j] = operand[j];
            Instructions[i] = instruction;
        }
    }
}

bool BHAV::Write(IffFile* iff, st::Writer& stream)
{
    (void)iff;
    st::Writer& io = stream;
    io.ByteOrder(st::Endian::Little);
    {
        io.WriteUInt16(0x8003);
        io.WriteByte(Type);
        io.WriteByte(Args);
        io.WriteByte((uint8_t)Locals);
        io.WriteBytes(std::vector<uint8_t>{ 0, 0 });
        io.WriteUInt16(Flags);
        io.WriteUInt32((uint16_t)Instructions.size());

        for (auto& inst : Instructions)
        {
            io.WriteUInt16(inst.Opcode);
            io.WriteByte(inst.TruePointer);
            io.WriteByte(inst.FalsePointer);
            io.WriteBytes(inst.Operand.data(), inst.Operand.size());
        }
    }
    return true;
}

}}}}} // namespace FSO::Files::Formats::IFF::Chunks
