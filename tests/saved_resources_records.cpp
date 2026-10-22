#include "saved_resources.hpp"

#include <iostream>
#include <stdexcept>
#include <string>

namespace {
unsigned checks = 0;

void require(bool value, const char* message) {
    ++checks;
    if (!value) throw std::runtime_error(message);
}

template<class F>
void rejects(F&& action, const char* message) {
    ++checks;
    try { action(); }
    catch (const odh::FormatError&) { return; }
    throw std::runtime_error(message);
}

template<class F>
void rejects_with(F&& action, const char* detail, const char* message) {
    ++checks;
    try { action(); }
    catch (const odh::FormatError& error) {
        if (std::string(error.what()).find(detail) != std::string::npos) return;
        throw std::runtime_error(message);
    }
    throw std::runtime_error(message);
}

template<class F>
void rejects_with_all(F&& action, const std::vector<std::string>& details, const char* message) {
    ++checks;
    try { action(); }
    catch (const odh::FormatError& error) {
        const std::string text = error.what();
        for (const auto& detail : details)
            if (text.find(detail) == std::string::npos) throw std::runtime_error(message);
        return;
    }
    throw std::runtime_error(message);
}

constexpr std::uint32_t guid = 0x12345678;

odh::ObjectDefinition definition() {
    return {138, guid, 4, 0, 0, false, 32, 0};
}

odh::DefinitionIndex unique_index() {
    return odh::DefinitionIndex({{"fixture", "house.iff", 1, definition()}});
}

struct Fixture {
    odh::Objm objects;
    odh::Objt types;
    std::vector<odh::SavedObjectPrefix> prefixes;
    std::vector<odh::ObjmCommonState> common;
    std::vector<std::optional<odh::ObjmTailState>> tails;

    Fixture() {
        objects.blocks.push_back({13, {}});
        odh::ObjtEntry entry{};
        entry.guid = guid;
        entry.metadata = odh::ObjtMetadata{0, 0, 0, 0, 4, 4};
        types.entries.push_back(entry);

        odh::SavedObjectPrefix prefix{};
        prefix.object_id = 7;
        prefix.type_id = 1;
        prefix.guid = guid;
        prefix.prefix.object_data[11] = 7;
        prefixes.push_back(prefix);

        odh::ObjmCommonState common_state{};
        common_state.block_offset = 13;
        common.push_back(common_state);
        odh::ObjmTailState tail{};
        tail.block_offset = 13;
        tails.push_back(tail);
    }
};

void run() {
    Fixture fixture;
    const auto records = saved_world_records(fixture.objects, fixture.types,
        fixture.prefixes, fixture.common, fixture.tails, unique_index());
    require(records.size() == 1, "one predecoded saved record is reused");
    require(records[0].saved.object_id == 7 && records[0].saved.guid == guid &&
        records[0].saved.prefix.object_data[11] == 7,
        "saved prefix identity and banks are retained");
    require(records[0].common.block_offset == 13 && records[0].tail.block_offset == 13,
        "predecoded common and tail states are retained");
    require(records[0].definition.guid == guid && records[0].serialized_object_type == 4,
        "unique OBJD and serialized OBJT type are resolved");
    fixture.prefixes[0].prefix.object_data[11] = 99;
    fixture.common[0].block_offset = 99;
    fixture.tails[0]->block_offset = 99;
    require(records[0].saved.prefix.object_data[11] == 7 && records[0].common.block_offset == 13 &&
        records[0].tail.block_offset == 13,
        "returned record owns copies independent from decoded snapshots");

    const odh::DefinitionIndex missing({});
    rejects_with([&] { (void)saved_world_records(fixture.objects, fixture.types,
        fixture.prefixes, fixture.common, fixture.tails, missing); },
        "definition missing for object 7 GUID 305419896", "missing GUID diagnostic retains object and GUID");

    const odh::DefinitionIndex ambiguous({
        {"fixture-a", "house-a.iff", 1, definition()},
        {"fixture-b", "house-b.iff", 2, definition()}});
    rejects_with_all([&] { (void)saved_world_records(fixture.objects, fixture.types,
        fixture.prefixes, fixture.common, fixture.tails, ambiguous); },
        {"(2 candidates)", "fixture-a!house-a.iff#1", "fixture-b!house-b.iff#2"},
        "ambiguous GUID diagnostic retains every candidate");

    auto bad = fixture;
    bad.tails[0].reset();
    rejects_with([&] { (void)saved_world_records(bad.objects, bad.types,
        bad.prefixes, bad.common, bad.tails, unique_index()); },
        "tail unavailable for object 7", "missing predecoded tail is rejected");
    bad = fixture;
    bad.types.entries[0].metadata.reset();
    rejects_with([&] { (void)saved_world_records(bad.objects, bad.types,
        bad.prefixes, bad.common, bad.tails, unique_index()); },
        "object type unavailable for object 7", "missing OBJT metadata is rejected");

    bad = fixture;
    bad.objects.blocks.clear();
    rejects([&] { (void)saved_world_records(bad.objects, bad.types,
        bad.prefixes, bad.common, bad.tails, unique_index()); },
        "block and decoded-prefix counts must match");
    bad = fixture;
    bad.common.clear();
    rejects([&] { (void)saved_world_records(bad.objects, bad.types,
        bad.prefixes, bad.common, bad.tails, unique_index()); },
        "decoded-common and prefix counts must match");
    bad = fixture;
    bad.tails.clear();
    rejects([&] { (void)saved_world_records(bad.objects, bad.types,
        bad.prefixes, bad.common, bad.tails, unique_index()); },
        "decoded-tail and prefix counts must match");
}
}

int main() {
    try {
        run();
        std::cout << checks << " saved resource record checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "saved resource record test failed: " << error.what() << '\n';
        return 1;
    }
}
