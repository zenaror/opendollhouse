#include "odh/dgrp.hpp"

#include <iostream>
#include <sstream>

namespace {
int checks = 0;
void require(bool condition, const char* message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}
template<class F> void rejects(F operation, const char* message) {
    bool rejected = false;
    try { operation(); } catch (const odh::FormatError&) { rejected = true; }
    require(rejected, message);
}
void put(std::string& bytes, std::size_t offset, std::uint32_t value, unsigned width) {
    for (unsigned n = 0; n < width; ++n)
        bytes.at(offset + n) = static_cast<char>((value >> (n * 8)) & 255);
}
template<class F> void use(const std::string& bytes, F operation) {
    std::istringstream stream(bytes, std::ios::binary);
    operation(odh::View(stream, bytes.size()));
}
std::string fixture(std::uint16_t version) {
    const bool legacy = version < 20003;
    const std::size_t header = legacy ? 4 : 6, image = legacy ? 4 : 12;
    const std::size_t sprite = legacy ? (version == 20001 ? 16 : 12) : (version == 20004 ? 32 : 24);
    std::string bytes(header + image + sprite, '\0');
    put(bytes, 0, version, 2); put(bytes, 2, 1, legacy ? 2 : 4);
    if (legacy) {
        put(bytes, header, 1, 2); put(bytes, header + 2, 0x81, 1); put(bytes, header + 3, 3, 1);
    } else {
        put(bytes, header, 0x81, 4); put(bytes, header + 4, 3, 4); put(bytes, header + 8, 1, 4);
    }
    const auto at = header + image;
    if (legacy) {
        put(bytes, at, 0xbeef, 2); put(bytes, at + 2, 0xf123, 2); put(bytes, at + 4, 0x1234, 2);
        put(bytes, at + 6, 0x8001, 2); put(bytes, at + 8, 0x8000, 2); put(bytes, at + 10, 0xffff, 2);
        if (version == 20001) put(bytes, at + 12, 0x7fc01234, 4);
    } else {
        put(bytes, at, 0xf1234567, 4); put(bytes, at + 4, 0x12345678, 4);
        put(bytes, at + 8, 0x80000000, 4); put(bytes, at + 12, 0xffffffff, 4);
        put(bytes, at + 16, 0x7fc01234, 4); put(bytes, at + 20, 0xdeadbeef, 4);
        if (version == 20004) {
            put(bytes, at + 24, 0xff800000, 4); put(bytes, at + 28, 0x80000000, 4);
        }
    }
    return bytes;
}
}

int main() {
    try {
        for (std::uint16_t version = 20000; version <= 20004; ++version) {
            const auto bytes = fixture(version);
            use(bytes, [&](const odh::View& view) {
                const auto group = odh::read_dgrp(view);
                require(group.version == version && group.images.size() == 1 && group.remaining_bytes == 0,
                        "version layout and consumed extent");
                const auto& image = group.images[0];
                require(image.direction == 0x81 && image.zoom == 3 && image.sprites.size() == 1, "image fields");
                const auto& sprite = image.sprites[0];
                require(sprite.y == -1, "signed vertical offset");
                require(sprite.object_offset_bits[2] == ((version == 20001 || version >= 20003) ? 0x7fc01234U : 0U),
                        "preserved NaN bits and version-specific Z");
                if (version < 20003) {
                    require(sprite.legacy_type == 0xbeef && sprite.sprite_id == 0xf123 && sprite.frame == 0x1234
                            && sprite.flags == 0x8001 && sprite.x == -32768, "legacy fields");
                } else {
                    require(!sprite.legacy_type && sprite.sprite_id == 0xf1234567 && sprite.frame == 0x12345678
                            && sprite.flags == 0xdeadbeef && sprite.x == (-2147483647 - 1), "modern fields");
                }
                require(sprite.object_offset_bits[0] == (version == 20004 ? 0xff800000U : 0U)
                        && sprite.object_offset_bits[1] == (version == 20004 ? 0x80000000U : 0U), "version-specific X/Y raw bits");
                require(odh::dgrp_image(group, 0x81, 3) == &group.images[0], "raw multibit direction match");
                require(!odh::dgrp_image(group, 0x81, 2) && !odh::dgrp_image(group, 0x80, 3), "missing zoom and direction");
                rejects([&] { odh::read_dgrp(view, {0, 4096}); }, "image budget");
                rejects([&] { odh::read_dgrp(view, {4096, 0}); }, "sprite budget");
            });
            use(bytes + "tail", [](const odh::View& view) {
                require(odh::read_dgrp(view).remaining_bytes == 4, "trailing extent");
            });
            for (std::size_t length = 0; length < bytes.size(); ++length)
                use(bytes.substr(0, length), [](const odh::View& view) {
                    rejects([&] { odh::read_dgrp(view); }, "every truncation point");
                });
            auto empty = bytes.substr(0, version < 20003 ? 4 : 6);
            put(empty, 2, 0, version < 20003 ? 2 : 4);
            use(empty, [](const odh::View& view) {
                require(odh::read_dgrp(view, {0, 0}).images.empty(), "empty table with zero budgets");
            });
            auto enormous = bytes;
            put(enormous, 2, 0xffffffff, version < 20003 ? 2 : 4);
            use(enormous, [](const odh::View& view) {
                rejects([&] { odh::read_dgrp(view, {0xffffffff, 0xffffffff}); }, "image allocation bounded by payload");
            });
            enormous = bytes;
            put(enormous, version < 20003 ? 4 : 14, 0xffffffff, version < 20003 ? 2 : 4);
            use(enormous, [](const odh::View& view) {
                rejects([&] { odh::read_dgrp(view, {0xffffffff, 0xffffffff}); }, "sprite allocation bounded by payload");
            });
            auto two = bytes;
            put(two, 2, 2, version < 20003 ? 2 : 4);
            two += bytes.substr(version < 20003 ? 4 : 6);
            use(two, [](const odh::View& view) {
                rejects([&] { odh::read_dgrp(view, {2, 1}); }, "cumulative sprite budget");
                const auto group = odh::read_dgrp(view, {2, 2});
                require(group.images.size() == 2, "multiple image layouts");
                rejects([&] { odh::dgrp_image(group, 0x81, 3); }, "ambiguous image selection");
            });
        }
        for (const auto version : {0, 19999, 20005, 65535}) {
            auto bytes = fixture(20004); put(bytes, 0, version, 2);
            use(bytes, [](const odh::View& view) {
                rejects([&] { odh::read_dgrp(view); }, "unsupported version");
            });
        }
        odh::Dgrp rotations{20004, {}, 0};
        for (const auto direction : {0x81U, 0x06U, 0x18U, 0x60U}) rotations.images.push_back({direction, 1, {}});
        for (std::uint8_t rotation = 0; rotation < 4; ++rotation)
            require(odh::dgrp_image(rotations, 0x81, 1, rotation) == &rotations.images[rotation], "all world rotations including wrap");
        rejects([&] { odh::dgrp_image(rotations, 0x81, 1, 4); }, "invalid rotation");
        odh::Dgrp raw{20004, {{0, 1, {}}, {255, 1, {}}, {256, 1, {}}}, 0};
        for (std::uint8_t rotation = 0; rotation < 4; ++rotation) {
            require(odh::dgrp_image(raw, 0, 1, rotation) == &raw.images[0], "zero raw direction allowed");
            require(odh::dgrp_image(raw, 255, 1, rotation) == &raw.images[1], "full raw direction allowed");
        }
        std::cout << checks << " checks passed (synthetic fixtures only)\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "test failed: " << error.what() << '\n';
        return 1;
    }
}
