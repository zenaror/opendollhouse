// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "simantics/VM.h"
#include "simantics/VMContext.h"
#include "files/formats/iff/IffRawFile.h"
#include "files/formats/iff/IffFile.h"
#include "files/Hash.h"
#include "files/formats/iff/chunks/SIMI.h"
#include <stdexcept>

namespace FSO { namespace SimAntics { namespace Utils {
// A nonfinancial execution snapshot may still carry an authentic classic SIMI.
// Keep both logical random states synchronized even without a family budget.
inline void SyncTS1RandomState(VM& vm, Files::Formats::IFF::IffRawFile& house) {
    if (!vm.TS1) return;
    using Files::Formats::IFF::Chunks::SIMI;
    Files::Formats::IFF::IffRawFile::Chunk* chunk = nullptr;
    for (auto& item : house.Chunks) if (item.Type == "SIMI") {
        if (chunk || item.ID != 1) throw std::runtime_error("TS1 random state: ambiguous SIMI");
        chunk = &item;
    }
    if (!chunk) return; // Resource-free native snapshots have no classic owner.
    SIMI original; st::Reader reader(chunk->Data, st::Endian::Little);
    original.Read(nullptr, reader);
    auto next = chunk->Data;
    const auto shared = SIMI::RandomStateOffset(original.Version);
    const auto owner = SIMI::SimulationRandomStateOffset(original.Version);
    if (shared + 4 > next.size()) throw std::runtime_error("TS1 random state: truncated SIMI");
    for (size_t k=0;k<4;++k) {
        next[shared+k]=uint8_t(uint32_t(vm.Context->RandomSeed)>>(8*k));
        next[owner+k]=uint8_t(uint32_t(vm.Context->TS1SimulationRandomSeed)>>(8*k));
    }
    chunk->Data = std::move(next);
}
// Synchronize financial bytes into a caller-owned staged IFF. No VM state is
// changed. Authentic SIMI parsing precedes replacement and retains unknown tail.
inline void SyncTS1Budget(VM& vm, Files::Formats::IFF::IffRawFile& house) {
    if (!vm.TS1 || !vm.TS1Budget.Initialized) return;
    using Files::Formats::IFF::Chunks::SIMI;
    Files::Formats::IFF::IffRawFile::Chunk* chunk = nullptr;
    for (auto& item : house.Chunks) if (item.Type == "SIMI") {
        if (chunk || item.ID != 1) throw std::runtime_error("TS1 finance: ambiguous SIMI");
        chunk = &item;
    }
    if (!chunk) throw std::runtime_error("TS1 finance: initialized budget requires SIMI");
    const auto& raw = chunk->Data;
    st::Reader reader(raw, st::Endian::Little);
    SIMI original;
    try { original.Read(nullptr, reader); }
    catch (const std::exception&) {
        if (!vm.TS1Budget.LegacyCandidateValidated) throw;
        const auto objm=house.Find("ObjM",1),odhi=house.Find("ODHI",1);
        if (!objm || !odhi || Files::Hash::CalculateHash(raw)!=vm.TS1Budget.LegacySIMIHash ||
            Files::Hash::CalculateHash(objm->Data)!=vm.TS1Budget.LegacyOBJMHash ||
            Files::Hash::CalculateHash(odhi->Data)!=vm.TS1Budget.LegacyODHIHash)
            throw std::runtime_error("TS1 finance: legacy candidate differs from validated snapshot");
        Files::Formats::IFF::IffFile candidate;candidate.Read(house.Write());
        auto accepted=candidate.Get<SIMI>(1);
        if (!accepted || !accepted->LegacyBudgetCandidate)
            throw std::runtime_error("TS1 finance: legacy candidate no longer matches compatible layout");
        original=*accepted;
    }
    const auto tickAt = 12 + SIMI::SerializedGlobalCount(original.Version) * 2;
    const auto ledgerAt = tickAt + 6 + (original.Version>=0x25 ? 7 : 4) * 4;
    if (raw.size() < ledgerAt) throw std::runtime_error("TS1 finance: truncated SIMI owner");
    if (vm.GlobalState.size() <= 14) throw std::runtime_error("TS1 finance: missing budget mirrors");
    auto signed16 = [](int32_t n) { uint32_t v=uint32_t(n)&0xffffu; return v<=0x7fffu ? int32_t(v) : int32_t(v)-65536; };
    const int32_t purse=vm.TS1Budget.Purse;
    if (vm.GlobalState[13]!=signed16(purse%10000) || vm.GlobalState[14]!=signed16(purse/10000))
        throw std::runtime_error("TS1 finance: purse and globals disagree");
    auto next=raw;
    auto put16=[&](size_t at,int16_t n){const auto bits=uint16_t(n);next[at]=uint8_t(bits);next[at+1]=uint8_t(bits>>8);};
    put16(12+13*2,vm.GlobalState[13]);put16(12+14*2,vm.GlobalState[14]);
    const auto bits=uint32_t(purse);for(size_t k=0;k<4;++k)next[tickAt+6+k]=uint8_t(bits>>(k*8));
    // Every staged continuation save also carries the DVD shared RNG slot.
    const auto randomAt=SIMI::RandomStateOffset(original.Version);
    const auto randomState=static_cast<uint32_t>(vm.Context->RandomSeed);
    for(size_t k=0;k<4;++k)next[randomAt+k]=uint8_t(randomState>>(k*8));
    const auto simulationAt=SIMI::SimulationRandomStateOffset(original.Version);
    const auto simulationState=static_cast<uint32_t>(vm.Context->TS1SimulationRandomSeed);
    for(size_t k=0;k<4;++k)next[simulationAt+k]=uint8_t(simulationState>>(k*8));
    for(size_t k=0;k<4;++k)next[tickAt+6+12+k]=0; // consumed owner+94 delta
    const bool upgrade=original.Version<0x34;
    if (upgrade) {
        next[4]=0x34;next[5]=next[6]=next[7]=0;
        if (vm.GlobalState.size()<=28) throw std::runtime_error("TS1 finance: missing day global");
        put16(12+28*2,vm.GlobalState[28]); // old load reset it; later ticks may advance it
    }
    if (original.Version>=0x26 || upgrade) {
        st::Writer writer(st::Endian::Little);
        for (const auto& values : vm.TS1Budget.Days) { SIMI::SIMIBudgetDay day;day.SetValues(values);day.Write(writer); }
        next.resize(ledgerAt);
        if (original.Version<0x25) { st::Writer defaults(st::Endian::Little);defaults.WriteInt32(4500);defaults.WriteInt32(0);defaults.WriteInt32(0);auto b=defaults.ToArray();next.insert(next.end(),b.begin(),b.end()); }
        auto records=writer.ToArray();next.insert(next.end(),records.begin(),records.end());
        next.insert(next.end(),original.UnknownTail.begin(),original.UnknownTail.end());
    }
    chunk->Data=std::move(next);
}
}}}
