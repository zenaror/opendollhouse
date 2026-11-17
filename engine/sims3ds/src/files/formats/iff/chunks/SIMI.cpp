/*
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, You can obtain one at
 * http://mozilla.org/MPL/2.0/.
 */
// Translated from FreeSims sims.files/formats/iff/chunks/SIMI.cs
#include "files/formats/iff/chunks/SIMI.h"
#include "files/formats/iff/IffFile.h"
#include "files/Hash.h"
#include "simantics/utils/VMTS1InteractionCheckpoint.h"
#include <algorithm>

namespace FSO { namespace Files { namespace Formats { namespace IFF { namespace Chunks {

void SIMI::ReadMetadata(st::Reader& stream) {
    if (stream.size() > INT32_MAX) throw std::runtime_error("SIMI: payload too large");
    stream.seek(0);SourceBytes=uint32_t(stream.size());
    SourceSignature=Files::Hash::CalculateHash(stream.raw(stream.size()),int32_t(stream.size()));
    stream.seek(0);stream.ByteOrder(st::Endian::Little);
    stream.ReadUInt32();Version=stream.ReadUInt32();
    if (stream.ReadCString(4)!="IMIS") throw std::runtime_error("SIMI: invalid magic");
    if (Version<0x12) throw std::runtime_error("SIMI: unsupported pre-0x12 layout");
    const auto items=SerializedGlobalCount(Version);GlobalData.assign(38,0);GlobalTail.clear();
    for(size_t i=0;i<items;++i) {auto value=stream.ReadInt16();if(i<GlobalData.size())GlobalData[i]=value;else GlobalTail.push_back(value);}
    SimulationTicks=stream.ReadUInt32();SchedulerCursor=stream.ReadInt16();Unknown3=stream.ReadInt32();
    GUID1=stream.ReadInt32();GUID2=stream.ReadInt32();Unknown4=stream.ReadInt32();
    if (Version>=0x25) { LotValue=stream.ReadInt32();ObjectsValue=stream.ReadInt32();ArchitectureValue=stream.ReadInt32(); }
    else { LotValue=4500;ObjectsValue=0;ArchitectureValue=0; }
    BudgetDays.clear();UnknownTail.clear();LegacyBudgetCandidate=false;
}

void SIMI::Read(IffFile* iff, st::Reader& stream) {
    ReadMetadata(stream);
    // Old native writer emitted six marker-only zeros. This compatibility
    // candidate is not proof of origin and requires a matching legacy ODHI
    // envelope; full execution validation remains mandatory before admission.
    if (Version>=0x26 && stream.size()-stream.pos()==24 && iff) {
        const auto start=stream.pos();const auto bytes=stream.ReadBytes(24);stream.seek(start);
        if (std::all_of(bytes.begin(),bytes.end(),[](uint8_t b){return b==0;}) &&
            FSO::SimAntics::Engine::Utils::VMTS1InteractionCheckpoint::PreflightLegacyBudget(*iff)) {
            stream.ReadBytes(24);BudgetDays.assign(6,SIMIBudgetDay{});if(Version<0x34)GlobalData[28]=0;LegacyBudgetCandidate=true;return;
        }
    }
    if (Version>=0x26) for(int i=0;i<6;++i) BudgetDays.emplace_back(stream);
    if (Version<0x34) {BudgetDays.assign(6,SIMIBudgetDay{});GlobalData[28]=0;}
    UnknownTail=stream.ReadBytes(stream.size()-stream.pos());
}

bool SIMI::Write(IffFile* iff, st::Writer& stream)
{
    (void)iff;
    st::Writer& io = stream;
    io.ByteOrder(st::Endian::Little);
    {
        io.WriteInt32(0);
        const auto version = Version == 0 ? uint32_t(0x3E) : Version;
        if (version < 0x12) throw std::runtime_error("SIMI: unsupported pre-0x12 layout");
        io.WriteInt32(static_cast<int32_t>(version));
        io.WriteCString("IMIS", 4);
        const auto items = SerializedGlobalCount(version);

        for (size_t i = 0; i < items; i++)
        {
            if (i < GlobalData.size())
                io.WriteInt16(GlobalData[i]);
            else if (i >= 38 && i - 38 < GlobalTail.size())
                io.WriteInt16(GlobalTail[i - 38]);
            else
                io.WriteInt16(0);
        }

        io.WriteUInt32(SimulationTicks);
        io.WriteInt16(SchedulerCursor);
        io.WriteInt32(Unknown3);
        io.WriteInt32(GUID1);
        io.WriteInt32(GUID2);
        io.WriteInt32(Unknown4);
        if (version>=0x25) { io.WriteInt32(LotValue); io.WriteInt32(ObjectsValue); io.WriteInt32(ArchitectureValue); }

        if (version >= 0x26) for (int i = 0; i < 6; i++)
        {
            if (size_t(i) < BudgetDays.size()) BudgetDays[size_t(i)].Write(io);
            else { SIMIBudgetDay empty; empty.Write(io); }
        }
        io.WriteBytes(UnknownTail);
    }
    return true;
}

}}}}} // namespace FSO::Files::Formats::IFF::Chunks
