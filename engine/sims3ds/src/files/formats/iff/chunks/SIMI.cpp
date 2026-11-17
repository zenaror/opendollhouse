/*
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, You can obtain one at
 * http://mozilla.org/MPL/2.0/.
 */
// Translated from FreeSims sims.files/formats/iff/chunks/SIMI.cs
#include "files/formats/iff/chunks/SIMI.h"
#include "files/formats/iff/IffFile.h"

namespace FSO { namespace Files { namespace Formats { namespace IFF { namespace Chunks {

void SIMI::Read(IffFile* iff, st::Reader& stream)
{
    (void)iff;
    st::Reader& io = stream;
    io.ByteOrder(st::Endian::Little);
    {
        io.ReadUInt32(); //pad
        Version = io.ReadUInt32();
        std::string magic = io.ReadCString(4);
        (void)magic;
        if (Version < 0x12) throw std::runtime_error("SIMI: unsupported pre-0x12 layout");
        const auto items = SerializedGlobalCount(Version);

        GlobalData = std::vector<int16_t>(38);
        GlobalTail.clear();

        for (size_t i=0; i<items; i++)
        {
            auto dat = io.ReadInt16();
            if (i < GlobalData.size())
                GlobalData[i] = dat;
            else GlobalTail.push_back(dat);
        }

        SimulationTicks = io.ReadUInt32();
        SchedulerCursor = io.ReadInt16();
        Unknown3 = io.ReadInt32();
        GUID1 = io.ReadInt32();
        GUID2 = io.ReadInt32();
        Unknown4 = io.ReadInt32();
        LotValue = io.ReadInt32();
        ObjectsValue = io.ReadInt32();
        ArchitectureValue = io.ReadInt32();

        //int Unknown3 (0)
        //int GUID1
        //int GUID2 (changes on bulldoze)
        //int Unknown4 (0)
        //int LotValue
        //int ObjectsValue
        //int ArchitectureValue 

        //the sims tracked a sim's budget over the past few days of gameplay.
        //this drove the budget window, which never actually came of much use to anyone ever.

        BudgetDays.clear();
        BudgetDays.reserve(6);
        for (int i=0; i<6; i++)
        {
            BudgetDays.push_back(SIMIBudgetDay(io));
        }
    }
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
        io.WriteInt32(LotValue);
        io.WriteInt32(ObjectsValue);
        io.WriteInt32(ArchitectureValue);

        for (int i = 0; i < 6; i++)
        {
            if (size_t(i) < BudgetDays.size()) BudgetDays[size_t(i)].Write(io);
            else io.WriteInt32(0);
        }
    }
    return true;
}

}}}}} // namespace FSO::Files::Formats::IFF::Chunks
