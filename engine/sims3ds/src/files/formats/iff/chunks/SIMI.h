/*
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, You can obtain one at
 * http://mozilla.org/MPL/2.0/.
 */
// Translated from FreeSims sims.files/formats/iff/chunks/SIMI.cs
#pragma once
#include <cstddef>
#include <array>
#include "files/formats/iff/AbstractIffChunk.h"

namespace FSO { namespace Files { namespace Formats { namespace IFF { namespace Chunks {

class SIMI : public IffChunk
{
public:
    IFF_CHUNK_TYPE("SIMI")

    uint32_t Version = 0;
    uint32_t SourceBytes = 0, SourceSignature = 0;
    bool LegacyBudgetCandidate = false;
    std::vector<int16_t> GlobalData;
    // Preserve serialized globals beyond the runtime's existing 38 entries.
    std::vector<int16_t> GlobalTail;
    uint32_t SimulationTicks = 0;
    int16_t SchedulerCursor = 0;
    int32_t Unknown3 = 0;
    int32_t GUID1 = 0;
    int32_t GUID2 = 0; // Historical parser name: the DVD shared random state.
    uint32_t SavedRandomState() const { return static_cast<uint32_t>(GUID2); }
    static std::size_t RandomStateOffset(uint32_t version) {
        return 12 + SerializedGlobalCount(version) * 2 + 14;
    }
    int32_t Unknown4 = 0;
    int32_t LotValue = 0;
    int32_t ObjectsValue = 0;
    int32_t ArchitectureValue = 0;

    static std::size_t SerializedGlobalCount(uint32_t version) {
        return version >= 0x41 ? 64 : 32;
    }

    class SIMIBudgetDay
    {
    public:
        // Wire format marker: zero has seven values, nonzero has eight.
        int32_t Valid = 1;
        int32_t MiscIncome = 0;
        int32_t JobIncome = 0;

        int32_t ServiceExpense = 0;
        int32_t FoodExpense = 0;
        int32_t BillsExpense = 0;

        int32_t MiscExpense = 0;
        int32_t HouseholdExpense = 0;
        int32_t ArchitectureExpense = 0;

        std::array<int32_t,8> Values() const {
            return {MiscIncome, JobIncome, ServiceExpense, FoodExpense, BillsExpense,
                    MiscExpense, HouseholdExpense, ArchitectureExpense};
        }
        void SetValues(const std::array<int32_t,8>& values) {
            MiscIncome=values[0]; JobIncome=values[1]; ServiceExpense=values[2];
            FoodExpense=values[3]; BillsExpense=values[4]; MiscExpense=values[5];
            HouseholdExpense=values[6]; ArchitectureExpense=values[7];
        }

        SIMIBudgetDay()
        {

        }

        explicit SIMIBudgetDay(st::Reader& io)
        {
            Valid = io.ReadInt32();
            MiscIncome = io.ReadInt32();
            JobIncome = io.ReadInt32();
            ServiceExpense = io.ReadInt32();
            FoodExpense = io.ReadInt32();
            BillsExpense = io.ReadInt32();
            MiscExpense = io.ReadInt32();
            HouseholdExpense = io.ReadInt32();
            if (Valid != 0) ArchitectureExpense = io.ReadInt32();
        }

        void Write(st::Writer& io)
        {
            // DVD writers always emit the complete eight-value record.
            io.WriteInt32(1);
            io.WriteInt32(MiscIncome);
            io.WriteInt32(JobIncome);
            io.WriteInt32(ServiceExpense);
            io.WriteInt32(FoodExpense);
            io.WriteInt32(BillsExpense);
            io.WriteInt32(MiscExpense);
            io.WriteInt32(HouseholdExpense);
            io.WriteInt32(ArchitectureExpense);
        }
    };

    std::vector<SIMIBudgetDay> BudgetDays;
    std::vector<uint8_t> UnknownTail;

    int32_t PurchaseValue() const
    {
        return LotValue + ObjectsValue + (ArchitectureValue * 7) / 10;
    }

    int32_t LoadedPurse() const {
        const uint32_t bits=uint32_t(Unknown3)+uint32_t(Unknown4);
        return bits<=uint32_t(INT32_MAX) ? int32_t(bits) : int32_t(int64_t(bits)-0x100000000LL);
    }

    // Read bounded owner metadata only; never treats the ledger as admitted.
    void ReadMetadata(st::Reader& stream);
    void Read(IffFile* iff, st::Reader& stream) override;

    bool Write(IffFile* iff, st::Writer& stream) override;
};

}}}}} // namespace FSO::Files::Formats::IFF::Chunks
