#include "odh_data.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace {
int checks = 0;
bool expect(bool value, const char* label) {
    ++checks;
    if (!value) std::fprintf(stderr, "FAIL %s\n", label);
    return value;
}
}

int main() {
    bool good = expect(odh_data_abi_version() == 1, "ABI version");
    const std::array<std::uint8_t, 7> input{{2, 0xA5, 0x34, 0x12, 0xDC, 0xFE, 0x99}};
    std::array<std::uint16_t, 3> words{{0x7777, 0x7777, 0x7777}};
    std::uint32_t count = 9, flags = 9, remaining = 9;

    auto status = odh_bcon_decode_v1(input.data(), input.size(), words.data(), 2,
                                     &count, &flags, &remaining);
    good &= expect(status == ODH_DATA_OK && count == 2 && flags == 0xA5 &&
                   remaining == 1 && words[0] == 0x1234 && words[1] == 0xFEDC &&
                   words[2] == 0x7777, "valid BCON and untouched spare word");

    words.fill(0x7777); count = flags = remaining = 9;
    status = odh_bcon_decode_v1(input.data(), input.size(), words.data(), 1,
                                &count, &flags, &remaining);
    good &= expect(status == ODH_DATA_BUFFER_TOO_SMALL && count == 2 && flags == 0xA5 &&
                   remaining == 1 && words[0] == 0x7777 && words[1] == 0x7777,
                   "undersized buffer returns metadata without partial words");

    count = flags = remaining = 9;
    status = odh_bcon_decode_v1(input.data(), input.size(), nullptr, 0,
                                &count, &flags, &remaining);
    good &= expect(status == ODH_DATA_BUFFER_TOO_SMALL && count == 2 && flags == 0xA5 &&
                   remaining == 1, "null words capacity zero queries required count");

    const std::array<std::uint8_t, 2> empty{{0, 0x5A}};
    count = flags = remaining = 9;
    status = odh_bcon_decode_v1(empty.data(), empty.size(), nullptr, 0,
                                &count, &flags, &remaining);
    good &= expect(status == ODH_DATA_OK && count == 0 && flags == 0x5A && remaining == 0,
                   "empty BCON accepts null words with zero capacity");

    const std::array<std::uint8_t, 3> truncated{{2, 0xA5, 0x34}};
    count = flags = remaining = 9;
    status = odh_bcon_decode_v1(truncated.data(), truncated.size(), words.data(), 3,
                                &count, &flags, &remaining);
    good &= expect(status == ODH_DATA_INVALID_FORMAT && count == 0 && flags == 0 &&
                   remaining == 0 && words[0] == 0x7777,
                   "malformed payload resets metadata and preserves words");

    count = flags = remaining = 9;
    status = odh_bcon_decode_v1(input.data(), 0, words.data(), 3,
                                &count, &flags, &remaining);
    good &= expect(status == ODH_DATA_INVALID_FORMAT && count == 0 && flags == 0 &&
                   remaining == 0 && words[0] == 0x7777,
                   "zero length nonnull payload is invalid format");

    count = flags = remaining = 9;
    status = odh_bcon_decode_v1(nullptr, 0, words.data(), 3,
                                &count, &flags, &remaining);
    good &= expect(status == ODH_DATA_INVALID_ARGUMENT && count == 0 && flags == 0 &&
                   remaining == 0 && words[0] == 0x7777,
                   "null data invalid even with zero length");

    count = flags = remaining = 9;
    status = odh_bcon_decode_v1(input.data(), input.size(), nullptr, 1,
                                &count, &flags, &remaining);
    good &= expect(status == ODH_DATA_INVALID_ARGUMENT && count == 0 && flags == 0 &&
                   remaining == 0, "null words with positive capacity invalid");

    count = flags = remaining = 9;
    status = odh_bcon_decode_v1(input.data(), input.size(), words.data(), 3,
                                nullptr, &flags, &remaining);
    good &= expect(status == ODH_DATA_INVALID_ARGUMENT && flags == 0 && remaining == 0 &&
                   words[0] == 0x7777, "missing count invalid and available metadata reset");
    count = flags = remaining = 9;
    status = odh_bcon_decode_v1(input.data(), input.size(), words.data(), 3,
                                &count, nullptr, &remaining);
    good &= expect(status == ODH_DATA_INVALID_ARGUMENT && count == 0 && remaining == 0 &&
                   words[0] == 0x7777, "missing flags invalid and available metadata reset");
    count = flags = remaining = 9;
    status = odh_bcon_decode_v1(input.data(), input.size(), words.data(), 3,
                                &count, &flags, nullptr);
    good &= expect(status == ODH_DATA_INVALID_ARGUMENT && count == 0 && flags == 0 &&
                   words[0] == 0x7777, "missing remaining invalid and available metadata reset");

    std::vector<std::uint8_t> maximum(2 + 255 * 2);
    maximum[0] = 255;
    maximum[1] = 0x7E;
    for (std::size_t i = 0; i < 255; ++i) {
        maximum[2 + i * 2] = static_cast<std::uint8_t>(i);
        maximum[3 + i * 2] = 0;
    }
    std::vector<std::uint16_t> many(255, 0x7777);
    count = flags = remaining = 9;
    status = odh_bcon_decode_v1(maximum.data(), maximum.size(), many.data(), 254,
                                &count, &flags, &remaining);
    good &= expect(status == ODH_DATA_BUFFER_TOO_SMALL && count == 255 && flags == 0x7E &&
                   remaining == 0 && many[0] == 0x7777 && many[254] == 0x7777,
                   "255-word bound and no partial write");
    status = odh_bcon_decode_v1(maximum.data(), maximum.size(), many.data(), 255,
                                &count, &flags, &remaining);
    good &= expect(status == ODH_DATA_OK && count == 255 && many[0] == 0 &&
                   many[254] == 254, "255-word decode");

    if (good) std::printf("PASS %d BCON C ABI checks\n", checks);
    return good ? 0 : 1;
}
