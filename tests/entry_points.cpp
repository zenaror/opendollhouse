#include "odh/entry_points.hpp"

#include <array>
#include <iostream>
#include <limits>
#include <sstream>
#include <utility>

namespace {
unsigned checks = 0;
void check(bool value) {
    ++checks;
    if (!value) throw std::runtime_error("entry point assertion");
}
void put16(std::string& bytes, std::size_t offset, std::uint16_t value) {
    bytes.at(offset) = static_cast<char>(value & 255);
    bytes.at(offset + 1) = static_cast<char>(value >> 8);
}
void put32(std::string& bytes, std::size_t offset, std::uint32_t value) {
    put16(bytes, offset, static_cast<std::uint16_t>(value));
    put16(bytes, offset + 2, static_cast<std::uint16_t>(value >> 16));
}
odh::EntryPointTable objd(const std::string& bytes) {
    std::istringstream stream(bytes);
    return odh::read_objd_entry_points(odh::View(stream, bytes.size()));
}
odh::EntryPointTable objf(const std::string& bytes, const odh::EntryPointLimits& limits = {}) {
    std::istringstream stream(bytes);
    return odh::read_objf(odh::View(stream, bytes.size()), limits);
}
template<class F> void rejected(F&& operation) {
    bool failed = false;
    try { operation(); } catch (const odh::FormatError&) { failed = true; }
    check(failed);
}
std::string objf_fixture(std::uint32_t count) {
    std::string bytes(16 + static_cast<std::size_t>(count) * 4, '\0');
    put32(bytes, 0, 0xdeadbeef);
    put32(bytes, 4, 0x89abcdef);
    bytes.replace(8, 4, "fJBO");
    put32(bytes, 12, count);
    for (std::uint32_t i = 0; i < count; ++i) {
        put16(bytes, 16 + static_cast<std::size_t>(i) * 4,
              static_cast<std::uint16_t>(0xff00 + i));
        put16(bytes, 18 + static_cast<std::size_t>(i) * 4,
              static_cast<std::uint16_t>(0x8000 + i));
    }
    return bytes;
}
} // namespace

int main() { try {
    std::string legacy(162, '\0');
    put32(legacy, 0, 138);
    // Independent fixture in disk-field order, with value 0x8000 + event id.
    // FreeSO OBJD words + VMEntity.GenerateFunctionTable define these ids.
    const std::array<std::pair<std::size_t, std::uint16_t>, 30> fields = {{
        {10, 0x8001}, // Main
        {12, 0x8010}, // Gardening
        {24, 0x8011}, // WashHands
        {34, 0x800f}, // Portal
        {42, 0x8005}, // AllowIntersection
        {48, 0x8012}, // PrepareFood
        {50, 0x8013}, // CookFood
        {52, 0x8014}, // PlaceSurface
        {54, 0x8015}, // Dispose
        {56, 0x8016}, // Eat
        {58, 0x8017}, // PickupFromSlot
        {60, 0x8018}, // WashDish
        {62, 0x8019}, // EatSurface
        {64, 0x801a}, // Sit
        {66, 0x801b}, // Stand
        {86, 0x8000}, // Init
        {88, 0x8009}, // Place
        {90, 0x800c}, // UserPickup
        {94, 0x8002}, // Load
        {96, 0x800b}, // UserPlace
        {100, 0x8007}, // RoomChange
        {104, 0x8003}, // Cleanup
        {106, 0x800d}, // LevelInfo
        {110, 0x800e}, // ServingSurface
        {118, 0x801c}, // Clean
        {120, 0x8004}, // QueueSkipped
        {124, 0x8006}, // WallAdjacencyChanged
        {148, 0x800a}, // Pickup
        {156, 0x8008}, // DynamicMultiTileUpdate
        {160, 0x801d}  // Repair
    }};
    for (const auto& field : fields) put16(legacy, field.first, field.second);
    put16(legacy, 18, 8); // ObjectType is not the Portal BHAV at byte 34.
    auto table = objd(legacy);
    check(table.version == 138 && !table.reserved && table.remaining_bytes == 0);
    check(table.source == odh::EntryPointSource::objd_legacy && table.entries.size() == 33);
    for (std::size_t i = 0; i < 33; ++i) {
        check(table.entries[i].condition == 0);
        check(table.entries[i].action == (i < 30 ? 0x8000 + i : 0));
    }
    check(table.entries[15].action == 0x800f && table.entries[15].action != 8);
    for (std::size_t size = 0; size < 162; ++size)
        rejected([&] { objd(legacy.substr(0, size)); });
    auto changed = legacy;
    for (auto version : {0U, 136U, 139U, 140U, 141U, 142U, 0xffffffffU}) {
        put32(changed, 0, version);
        rejected([&] { objd(changed); });
    }
    changed = legacy;
    for (auto flag : {1U, 2U, 0xffffU}) {
        put16(changed, 44, static_cast<std::uint16_t>(flag));
        rejected([&] { objd(changed); });
    }
    changed = legacy;
    put16(changed, 86, 0);
    put16(changed, 160, 0xffff);
    table = objd(changed + "uninterpreted tail");
    check(table.remaining_bytes == 18 && table.entries[0].action == 0 &&
          table.entries[29].action == 0xffff);
    std::istringstream parent("before" + legacy + "after");
    table = odh::read_objd_entry_points(odh::View(parent, 173).subview(6, 162));
    check(table.entries[0].action == 0x8000 && table.remaining_bytes == 0);

    auto stored = objf_fixture(3);
    table = objf(stored);
    check(table.version == 0x89abcdef && table.reserved == 0xdeadbeef &&
          table.source == odh::EntryPointSource::objf && table.remaining_bytes == 0);
    check(table.entries.size() == 3);
    for (std::size_t i = 0; i < 3; ++i) {
        check(table.entries[i].condition == 0xff00 + i);
        check(table.entries[i].action == 0x8000 + i);
    }
    for (std::size_t size = 0; size < stored.size(); ++size)
        rejected([&] { objf(stored.substr(0, size)); });
    for (std::size_t i = 8; i < 12; ++i) {
        changed = stored;
        changed[i] = '\0';
        rejected([&] { objf(changed); });
    }
    for (auto version : {0U, 1U, 2U, 0xffffffffU}) {
        changed = stored;
        put32(changed, 4, version);
        check(objf(changed).version == version); // Structural, no version policy.
    }
    table = objf(objf_fixture(0) + "tail");
    check(table.entries.empty() && table.remaining_bytes == 4);
    table = objf(stored + "tail");
    check(table.entries.size() == 3 && table.remaining_bytes == 4);
    std::istringstream stored_parent("before" + stored + "after");
    table = odh::read_objf(odh::View(stored_parent, 39).subview(6, stored.size()));
    check(table.entries[2].condition == 0xff02 && table.remaining_bytes == 0);
    auto limits = odh::EntryPointLimits{};
    limits.entries = 2;
    rejected([&] { objf(stored, limits); });
    limits = {};
    limits.decoded_bytes = 11;
    rejected([&] { objf(stored, limits); });
    limits.decoded_bytes = 12;
    check(objf(stored, limits).entries.size() == 3);
    limits = {};
    limits.payload_bytes = stored.size() - 1;
    rejected([&] { objf(stored, limits); });
    limits.payload_bytes = stored.size();
    check(objf(stored, limits).entries.size() == 3);
    rejected([&] { objf(stored + "tail", limits); });
    limits.payload_bytes = 15;
    rejected([&] { objf(objf_fixture(0), limits); });
    limits = {};
    limits.entries = 0;
    limits.decoded_bytes = 0;
    check(objf(objf_fixture(0), limits).entries.empty());
    rejected([&] { objf(stored, limits); });
    check(objf(objf_fixture(256)).entries.size() == 256);
    rejected([&] { objf(objf_fixture(257)); });
    auto forged = objf_fixture(0);
    put32(forged, 12, 0xffffffffU);
    rejected([&] { objf(forged); });
    limits = {};
    limits.entries = std::numeric_limits<std::uint32_t>::max();
    limits.decoded_bytes = std::numeric_limits<std::uint64_t>::max();
    rejected([&] { objf(forged, limits); }); // Extent checked before allocation.
    std::cout << checks << " entry point checks passed\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
} }
