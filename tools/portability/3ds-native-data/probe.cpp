#include "odh/formats.hpp"
#include "odh_data.h"

#include <algorithm>
#include <array>
#include <cstddef>
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

bool check_bcon_abi() {
    if (odh_data_abi_version() != 1) return false;
    const std::array<std::uint8_t, 7> data{{2, 0xA5, 0x34, 0x12, 0xDC, 0xFE, 0x99}};
    std::array<std::uint16_t, 3> words{{0x7777, 0x7777, 0x7777}};
    std::uint32_t count = 9, flags = 9, remaining = 9;
    if (odh_bcon_decode_v1(data.data(), data.size(), words.data(), 2,
                           &count, &flags, &remaining) != ODH_DATA_OK ||
        count != 2 || flags != 0xA5 || remaining != 1 ||
        words[0] != 0x1234 || words[1] != 0xFEDC || words[2] != 0x7777)
        return false;

    words.fill(0x7777);
    count = flags = remaining = 9;
    if (odh_bcon_decode_v1(data.data(), data.size(), words.data(), 1,
                           &count, &flags, &remaining) != ODH_DATA_BUFFER_TOO_SMALL ||
        count != 2 || flags != 0xA5 || remaining != 1 || words[0] != 0x7777)
        return false;

    count = flags = remaining = 9;
    if (odh_bcon_decode_v1(data.data(), data.size(), nullptr, 0,
                           &count, &flags, &remaining) != ODH_DATA_BUFFER_TOO_SMALL ||
        count != 2 || flags != 0xA5 || remaining != 1)
        return false;
    count = flags = remaining = 9;
    if (odh_bcon_decode_v1(data.data(), data.size(), nullptr, 1,
                           &count, &flags, &remaining) != ODH_DATA_INVALID_ARGUMENT ||
        count != 0 || flags != 0 || remaining != 0)
        return false;

    if (sizeof(std::ptrdiff_t) == 4) {
        count = flags = remaining = 9;
        if (odh_bcon_decode_v1(data.data(), 0x80000000U, words.data(), 3,
                               &count, &flags, &remaining) != ODH_DATA_INVALID_ARGUMENT ||
            count != 0 || flags != 0 || remaining != 0 || words[0] != 0x7777)
            return false;
    }

    count = flags = remaining = 9;
    if (odh_bcon_decode_v1(nullptr, 0, words.data(), 3,
                           &count, &flags, &remaining) != ODH_DATA_INVALID_ARGUMENT ||
        count != 0 || flags != 0 || remaining != 0 || words[0] != 0x7777)
        return false;

    const std::array<std::uint8_t, 3> truncated{{2, 0xA5, 0x34}};
    count = flags = remaining = 9;
    if (odh_bcon_decode_v1(truncated.data(), truncated.size(), words.data(), 3,
                           &count, &flags, &remaining) != ODH_DATA_INVALID_FORMAT ||
        count != 0 || flags != 0 || remaining != 0 || words[0] != 0x7777)
        return false;

    const std::array<std::uint8_t, 2> empty{{0, 0x5A}};
    if (odh_bcon_decode_v1(empty.data(), empty.size(), nullptr, 0,
                           &count, &flags, &remaining) != ODH_DATA_OK ||
        count != 0 || flags != 0x5A || remaining != 0)
        return false;

    std::vector<std::uint8_t> maximum(2 + 255 * 2);
    maximum[0] = 255;
    maximum[1] = 0x7E;
    for (std::size_t i = 0; i < 255; ++i) {
        maximum[2 + i * 2] = static_cast<std::uint8_t>(i);
        maximum[3 + i * 2] = 0;
    }
    std::vector<std::uint16_t> many(255, 0x7777);
    if (odh_bcon_decode_v1(maximum.data(), maximum.size(), many.data(), 254,
                           &count, &flags, &remaining) != ODH_DATA_BUFFER_TOO_SMALL ||
        count != 255 || flags != 0x7E || remaining != 0 ||
        many[0] != 0x7777 || many[254] != 0x7777)
        return false;
    if (odh_bcon_decode_v1(maximum.data(), maximum.size(), many.data(), 255,
                           &count, &flags, &remaining) != ODH_DATA_OK ||
        count != 255 || many[0] != 0 || many[254] != 254)
        return false;
    return true;
}

bool run_probe() {
    const auto good = synthetic_far_iff_bcon();
    if (!check_valid(good)) return false;
    auto invalid = good;
    invalid[8] = 2; // Unsupported FAR version must be rejected, not guessed.
    bool rejected = false;
    try {
        check_valid(invalid);
    } catch (const odh::FormatError&) {
        rejected = true;
    }
    return rejected && check_bcon_abi();
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
