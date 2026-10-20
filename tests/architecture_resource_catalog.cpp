#include "odh/architecture_resource_catalog.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
int checks = 0;
void require(bool condition, const char* message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}
template<class Action>
void rejects(Action action) {
    bool rejected = false;
    try { action(); } catch (const odh::FormatError&) { rejected = true; }
    require(rejected, "catalog input or budget should be rejected");
}
void put32(std::string& out, std::uint32_t value) {
    for (unsigned i = 0; i < 4; ++i)
        out.push_back(static_cast<char>((value >> (8U * i)) & 0xffU));
}
std::string far1(const std::vector<std::pair<std::string, std::string>>& entries) {
    std::string out = "FAR!byAZ";
    put32(out, 1);
    put32(out, 0);
    const auto header_size = out.size();
    std::vector<std::uint32_t> offsets;
    for (const auto& entry : entries) {
        offsets.push_back(static_cast<std::uint32_t>(out.size()));
        out += entry.second;
    }
    const auto manifest = static_cast<std::uint32_t>(out.size());
    for (unsigned i = 0; i < 4; ++i)
        out[12 + i] = static_cast<char>((manifest >> (8U * i)) & 0xffU);
    put32(out, static_cast<std::uint32_t>(entries.size()));
    for (std::size_t i = 0; i < entries.size(); ++i) {
        put32(out, static_cast<std::uint32_t>(entries[i].second.size()));
        put32(out, static_cast<std::uint32_t>(entries[i].second.size()));
        put32(out, offsets[i]);
        put32(out, static_cast<std::uint32_t>(entries[i].first.size()));
        out += entries[i].first;
    }
    (void)header_size;
    return out;
}
struct Temp {
    std::filesystem::path path = std::filesystem::temp_directory_path() /
        ("odh-architecture-resource-catalog-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    Temp() { std::filesystem::create_directory(path); }
    ~Temp() { std::filesystem::remove_all(path); }
    std::string write(const std::string& name, const std::string& bytes) {
        const auto target = path / name;
        std::filesystem::create_directories(target.parent_path());
        std::ofstream out(target, std::ios::binary);
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        if (!out) throw std::runtime_error("failed writing synthetic resource");
        return target.string();
    }
};
}

int main() {
    try {
        Temp temp;
        const auto first_floor = temp.write("one/FloorOak.FLR", "floor-one");
        const auto second_floor = temp.write("two/flooroak.flr", "floor-two-longer");
        const auto ignored = temp.write("two/sprite.spr", "not catalogued");
        const auto archive_path = temp.write("pack.far", far1({
            {"nested/FLOOROAK.FLR", "archive-floor"},
            {"nested/WallBrick.WLL", "brick-a"},
            {"ignore/texture.iff", "ignore"},
            {"overrides/wallbrick.wll", "brick-winner"},
            {"materials/WallMixed.material.WLL", "wall-mixed"}
        }));

        const std::vector<odh::ArchitectureResourceSource> sources = {
            {first_floor, odh::ArchitectureResourceSourceKind::loose_file},
            {second_floor, odh::ArchitectureResourceSourceKind::loose_file},
            {ignored, odh::ArchitectureResourceSourceKind::loose_file},
            {archive_path, odh::ArchitectureResourceSourceKind::far1_archive}
        };
        const auto catalog = odh::discover_architecture_resources(sources);
        require(catalog.resources.size() == 3 && catalog.resources[0].family == odh::ArchitectureResourceFamily::floor &&
                catalog.resources[1].family == odh::ArchitectureResourceFamily::wall &&
                catalog.resources[2].family == odh::ArchitectureResourceFamily::wall,
                "catalog separates floor/wall families in stable order");
        const auto& floor = catalog.resources[0];
        const auto& wall = catalog.resources[1];
        const auto& mixed_wall = catalog.resources[2];
        require(floor.catalog.name == "flooroak" && floor.catalog.id == 256 &&
                floor.source.source_index == 3 && floor.source.entry_index == 0 &&
                floor.source.entry_name == "nested/FLOOROAK.FLR" && floor.source.offset == 16 &&
                floor.source.size == std::string("archive-floor").size(),
                "later FAR source wins and retains reopenable entry provenance");
        require(wall.catalog.name == "wallbrick" && wall.catalog.id == 256 &&
                wall.source.source_index == 3 && wall.source.entry_index == 3 &&
                wall.source.entry_name == "overrides/wallbrick.wll" &&
                wall.source.size == std::string("brick-winner").size(),
                "later FAR manifest duplicate wins within explicit source order");
        require(mixed_wall.catalog.name == "wallmixed" && mixed_wall.catalog.id == 257 &&
                mixed_wall.source.entry_index == 4,
                "FAR entries use first-dot provider key while classifying final extension");
        require(catalog.diagnostics.duplicate_floor_names == 2 &&
                catalog.diagnostics.duplicate_wall_names == 1 &&
                catalog.diagnostics.ignored_extensions == 2 &&
                catalog.diagnostics.loose_sources == 3 && catalog.diagnostics.far_sources == 1,
                "duplicates, ignored extensions, and source families are diagnosed");
        require(catalog.sources.size() == sources.size() &&
                catalog.sources[floor.source.source_index].path == archive_path &&
                catalog.floors().size() == 1 && catalog.floors()[0].id == floor.catalog.id &&
                catalog.walls().size() == 2 && catalog.walls()[0].id == wall.catalog.id &&
                catalog.walls()[1].id == mixed_wall.catalog.id,
                "handles refer to once-stored explicit source list and feed alias catalog API");

        const auto alpha = temp.write("alpha.flr", "a");
        const auto zeta = temp.write("zeta.flr", "z");
        const auto sorted_a = odh::discover_architecture_resources({
            {zeta, odh::ArchitectureResourceSourceKind::loose_file},
            {alpha, odh::ArchitectureResourceSourceKind::loose_file}});
        const auto sorted_b = odh::discover_architecture_resources({
            {alpha, odh::ArchitectureResourceSourceKind::loose_file},
            {zeta, odh::ArchitectureResourceSourceKind::loose_file}});
        require(sorted_a.resources[0].catalog.name == "alpha" && sorted_a.resources[0].catalog.id == 256 &&
                sorted_a.resources[1].catalog.name == "zeta" && sorted_a.resources[1].catalog.id == 257 &&
                sorted_b.resources[0].catalog.id == 256 && sorted_b.resources[1].catalog.id == 257,
                "unique resource IDs follow canonical sort, independent of input listing order");

        const auto malformed = temp.write("malformed.far", "not-a-FAR archive");
        rejects([&] { (void)odh::discover_architecture_resources({
            {malformed, odh::ArchitectureResourceSourceKind::far1_archive}}); });

        auto limits = odh::ArchitectureResourceCatalogLimits{};
        limits.sources = 1;
        rejects([&] { (void)odh::discover_architecture_resources(sources, limits); });
        limits = {};
        limits.resource_entries = 1;
        rejects([&] { (void)odh::discover_architecture_resources({
            {alpha, odh::ArchitectureResourceSourceKind::loose_file},
            {zeta, odh::ArchitectureResourceSourceKind::loose_file}}, limits); });
        limits = {};
        limits.path_length = 2;
        rejects([&] { (void)odh::discover_architecture_resources({
            {alpha, odh::ArchitectureResourceSourceKind::loose_file}}, limits); });
        limits = {};
        limits.name_bytes = 2;
        rejects([&] { (void)odh::discover_architecture_resources({
            {archive_path, odh::ArchitectureResourceSourceKind::far1_archive}}, limits); });
        limits = {};
        limits.far_entries_per_archive = 1;
        rejects([&] { (void)odh::discover_architecture_resources({
            {archive_path, odh::ArchitectureResourceSourceKind::far1_archive}}, limits); });

        std::cout << checks << " architecture resource catalog checks passed (synthetic)\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Architecture resource catalog test failed: " << error.what() << '\n';
        return 1;
    }
}
