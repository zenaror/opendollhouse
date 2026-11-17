// SPDX-License-Identifier: MPL-2.0
// Literal DVD wire layout; no game installation is needed for this codec gate.
#include "files/formats/iff/chunks/SIMI.h"
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

using FSO::Files::Formats::IFF::Chunks::SIMI;

static void Require(bool value, const char* text) {
    if (!value) throw std::runtime_error(text);
}

static std::vector<uint8_t> Fixture(uint32_t version, uint32_t ticks) {
    std::vector<uint8_t> bytes;
    const auto word = [&](uint16_t value) {
        bytes.push_back(uint8_t(value)); bytes.push_back(uint8_t(value >> 8));
    };
    const auto dword = [&](uint32_t value) {
        word(uint16_t(value)); word(uint16_t(value >> 16));
    };
    dword(0); dword(version);
    bytes.insert(bytes.end(), {'I','M','I','S'});
    // These boundaries come from the original serializer, independently of
    // the production codec's count helper.
    const unsigned count = version >= 0x41 ? 64 : 32;
    for (unsigned i=0; i<count; ++i) word(uint16_t(0x7000 + i));
    dword(ticks); word(0xbeef);
    for (unsigned i=0; i<(version>=0x25 ? 7u : 4u); ++i) dword(0x12340000 + i);
    if (version >= 0x26) for (unsigned i=0; i<6; ++i) {
        dword(1);
        for (unsigned j=0; j<8; ++j) dword(version<0x34 ? 1+i*8+j : 0);
    } // authentic empty records retain eight signed accumulators
    return bytes;
}

static void RoundTrip(uint32_t version, uint32_t ticks) {
    auto input = Fixture(version, ticks);
    st::Reader reader(input.data(), input.size(), st::Endian::Little);
    SIMI data; data.Read(nullptr, reader);
    Require(data.Version == version, "SIMI version changed");
    Require(data.SimulationTicks == ticks, "SIMI DWORD counter partition differs");
    Require(uint16_t(data.SchedulerCursor) == 0xbeef, "SIMI signed cursor partition differs");
    Require(data.Unknown3 == 0x12340000 && data.ArchitectureValue == (version>=0x25 ? 0x12340006 : 0),
            "SIMI fields after counter/cursor moved");
    if(version<0x25) Require(data.LotValue==4500 && data.ObjectsValue==0,"early SIMI metadata defaults");
    Require(data.LoadedPurse()==int32_t(0x24680003),"pending delta applied to logical purse");
    if (version < 0x34) {
        Require(data.GlobalData[28]==0,"legacy load did not reset Global28");
        input[12+28*2]=0; input[12+28*2+1]=0;
        for (const auto& day:data.BudgetDays)
            for (auto value:day.Values()) Require(value==0,"legacy history was not reset");
        if (version>=0x26) {
            const size_t ledgerAt=12+(version>=0x41?64:32)*2+34;
            for (size_t i=0;i<6;++i)
                for (size_t j=4;j<36;++j) input[ledgerAt+i*36+j]=0;
        }
    }
    const auto globals = data.GlobalData;
    st::Writer writer(st::Endian::Little);
    Require(data.Write(nullptr, writer), "SIMI write failed");
    Require(data.GlobalData == globals, "SIMI write erased live globals");
    Require(writer.ToArray() == input, "SIMI roundtrip changed reserved globals or metadata");
}

int main() {
    try {
        SIMI delta;delta.Unknown3=100;delta.Unknown4=-7;Require(delta.LoadedPurse()==93,"negative pending delta");
        delta.Unknown3=INT32_MAX;delta.Unknown4=1;Require(delta.LoadedPurse()==INT32_MIN,"pending delta wraps at 32 bits");
        for (auto version : {0x12u,0x26u,0x33u,0x34u,0x3eu,0x40u,0x41u,0x49u})
            for (auto ticks : {0u,58u,59u,232560u,0x89abcdefu}) RoundTrip(version,ticks);
        // A marker-zero record still contains seven values. Slot5 must not
        // be overwritten with slot0 when converted to the normal eight layout.
        st::Writer legacy(st::Endian::Little); legacy.WriteInt32(0);
        for (int i=0;i<7;++i) legacy.WriteInt32(100+i);
        auto old=legacy.ToArray(); st::Reader oldReader(old);
        SIMI::SIMIBudgetDay day(oldReader);
        Require(!oldReader.has_more() && day.MiscExpense==105 && day.ArchitectureExpense==0,
                "marker zero failed to consume seven values");
        st::Writer full(st::Endian::Little); day.Write(full);
        auto encoded=full.ToArray(); st::Reader fullReader(encoded);
        SIMI::SIMIBudgetDay restored(fullReader);
        Require(encoded.size()==36 && restored.Valid==1 && restored.MiscIncome==100 &&
                restored.MiscExpense==105 && restored.HouseholdExpense==106,
                "normal writer lost distinct ledger values");
        old.pop_back(); st::Reader shortReader(old); bool rejected=false;
        try { SIMI::SIMIBudgetDay truncated(shortReader); } catch (const std::exception&) { rejected=true; }
        Require(rejected,"truncated legacy seven-value record accepted");
        SIMI fresh;
        st::Writer writer(st::Endian::Little);
        Require(fresh.Write(nullptr,writer), "empty SIMI failed to use the supported default layout");
        auto bytes=writer.ToArray(); st::Reader reader(bytes.data(),bytes.size(),st::Endian::Little);
        SIMI reloaded; reloaded.Read(nullptr,reader);
        Require(reloaded.Version==0x3e && reloaded.SimulationTicks==0,
                "empty SIMI emitted mismatched header and payload");
        std::cout << "SIMI DWORD clock, signed cursor, version boundaries and reserved globals passed\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
