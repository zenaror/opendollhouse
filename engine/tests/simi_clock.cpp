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
    for (unsigned i=0; i<7; ++i) dword(0x12340000 + i);
    for (unsigned i=0; i<6; ++i) dword(0); // empty budget-history records
    return bytes;
}

static void RoundTrip(uint32_t version, uint32_t ticks) {
    const auto input = Fixture(version, ticks);
    st::Reader reader(input.data(), input.size(), st::Endian::Little);
    SIMI data; data.Read(nullptr, reader);
    Require(data.Version == version, "SIMI version changed");
    Require(data.SimulationTicks == ticks, "SIMI DWORD counter partition differs");
    Require(uint16_t(data.SchedulerCursor) == 0xbeef, "SIMI signed cursor partition differs");
    Require(data.Unknown3 == 0x12340000 && data.ArchitectureValue == 0x12340006,
            "SIMI fields after counter/cursor moved");
    const auto globals = data.GlobalData;
    st::Writer writer(st::Endian::Little);
    Require(data.Write(nullptr, writer), "SIMI write failed");
    Require(data.GlobalData == globals, "SIMI write erased live globals");
    Require(writer.ToArray() == input, "SIMI roundtrip changed reserved globals or metadata");
}

int main() {
    try {
        for (auto version : {0x12u,0x3eu,0x40u,0x41u,0x49u})
            for (auto ticks : {0u,58u,59u,232560u,0x89abcdefu}) RoundTrip(version,ticks);
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
