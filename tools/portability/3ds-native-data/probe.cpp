#include "odh/formats.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

#ifdef __3DS__
#include <3ds.h>
#endif

namespace {

void le16(std::vector<std::uint8_t>& bytes, std::size_t at, std::uint16_t value) {
    bytes.at(at) = static_cast<std::uint8_t>(value);
    bytes.at(at + 1) = static_cast<std::uint8_t>(value >> 8);
}

void le32(std::vector<std::uint8_t>& bytes, std::size_t at, std::uint32_t value) {
    for (unsigned shift = 0; shift < 32; shift += 8)
        bytes.at(at + shift / 8) = static_cast<std::uint8_t>(value >> shift);
}

void be16(std::vector<std::uint8_t>& bytes, std::size_t at, std::uint16_t value) {
    bytes.at(at) = static_cast<std::uint8_t>(value >> 8);
    bytes.at(at + 1) = static_cast<std::uint8_t>(value);
}

void be32(std::vector<std::uint8_t>& bytes, std::size_t at, std::uint32_t value) {
    for (unsigned i = 0; i < 4; ++i)
        bytes.at(at + i) = static_cast<std::uint8_t>(value >> (24 - i * 8));
}

std::vector<std::uint8_t> synthetic_far_iff_bcon() {
    const std::string signature =
        "IFF FILE 2.5:TYPE FOLLOWED BY SIZE JAMIE DOORNBOS & MAXIS 1";
    const std::string name = "fixture.iff";
    const std::size_t chunk_length = 76 + 7; // BCON: count, flags, two LE16s, unknown tail.
    std::vector<std::uint8_t> iff(64 + chunk_length, 0);
    std::copy(signature.begin(), signature.end(), iff.begin());
    std::copy("BCON", "BCON" + 4, iff.begin() + 64);
    be32(iff, 68, static_cast<std::uint32_t>(chunk_length));
    be16(iff, 72, 7);
    be16(iff, 74, 0x1234);
    std::copy("synthetic", "synthetic" + 9, iff.begin() + 76);
    const std::size_t data = 64 + 76;
    iff[data] = 2;
    iff[data + 1] = 0xA5;
    le16(iff, data + 2, 0x1234);
    le16(iff, data + 4, 0xFEDC);
    iff[data + 6] = 0x99;

    const std::size_t manifest = 16 + iff.size();
    std::vector<std::uint8_t> far(manifest + 4 + 16 + name.size(), 0);
    std::copy("FAR!byAZ", "FAR!byAZ" + 8, far.begin());
    le32(far, 8, 1);
    le32(far, 12, static_cast<std::uint32_t>(manifest));
    std::copy(iff.begin(), iff.end(), far.begin() + 16);
    le32(far, manifest, 1);
    le32(far, manifest + 4, static_cast<std::uint32_t>(iff.size()));
    le32(far, manifest + 8, static_cast<std::uint32_t>(iff.size()));
    le32(far, manifest + 12, 16);
    le32(far, manifest + 16, static_cast<std::uint32_t>(name.size()));
    std::copy(name.begin(), name.end(), far.begin() + manifest + 20);
    return far;
}

bool check_valid(const std::vector<std::uint8_t>& bytes) {
    const std::string encoded(bytes.begin(), bytes.end());
    std::istringstream stream(encoded, std::ios::binary);
    odh::View archive(stream, bytes.size());
    const auto entries = odh::far1_index(archive);
    if (entries.size() != 1 || entries[0].name != "fixture.iff" ||
        entries[0].offset != 16 || entries[0].length != 147) return false;
    const auto file = archive.subview(entries[0].offset, entries[0].length);
    unsigned visited = 0;
    const auto version = odh::iff_chunks(file, [&](const odh::Chunk& chunk) {
        if (chunk.type != "BCON" || chunk.id != 7 || chunk.flags != 0x1234 ||
            chunk.label != "synthetic") throw odh::FormatError("unexpected IFF chunk header");
        const auto bcon = odh::read_bcon(chunk.payload);
        if (bcon.flags != 0xA5 || bcon.constants.size() != 2 ||
            bcon.constants[0] != 0x1234 || bcon.constants[1] != 0xFEDC ||
            bcon.remaining_bytes != 1) throw odh::FormatError("unexpected BCON fields");
        ++visited;
    });
    return version == "2.5" && visited == 1;
}

bool run_probe() {
    const auto good = synthetic_far_iff_bcon();
    if (!check_valid(good)) return false;
    auto invalid = good;
    invalid[8] = 2; // Unsupported FAR version must be rejected, not guessed.
    try {
        check_valid(invalid);
        return false;
    } catch (const odh::FormatError&) {
        return true;
    }
}

} // namespace

int main() {
    bool passed = false;
    try {
        passed = run_probe();
    } catch (const std::exception& error) {
        std::printf("data probe exception: %s\n", error.what());
    }
#ifdef __3DS__
    gfxInitDefault();
    consoleInit(GFX_TOP, nullptr);
    const char* diagnostic = passed
        ? "ODH_3DS_NATIVE_DATA_V1:PASS\n"
        : "ODH_3DS_NATIVE_DATA_V1:FAIL\n";
    svcOutputDebugString(diagnostic, static_cast<s32>(std::strlen(diagnostic)));
#endif
    std::printf("ODH FAR/IFF/BCON synthetic probe: %s\n", passed ? "PASS" : "FAIL");
#ifdef __3DS__
    std::printf("Press START to exit.\n");
    while (aptMainLoop()) {
        hidScanInput();
        if (hidKeysDown() & KEY_START) break;
        gfxFlushBuffers();
        gfxSwapBuffers();
        gspWaitForVBlank();
    }
    gfxExit();
#endif
    return passed ? 0 : 1;
}
