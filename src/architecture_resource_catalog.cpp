#include "odh/architecture_resource_catalog.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <system_error>

namespace odh {
namespace {
constexpr std::uint16_t first_dynamic_id = 256;
constexpr std::uint16_t last_dynamic_id = 65533;

struct Key {
    ArchitectureResourceFamily family;
    std::string name;
    bool operator<(const Key& other) const noexcept {
        if (family != other.family) return family < other.family;
        return name < other.name;
    }
};

bool lower_ascii(std::string& value) {
    for (char& c : value) {
        const auto byte = static_cast<unsigned char>(c);
        if (byte == 0 || byte >= 0x80) return false;
        if (byte >= 'A' && byte <= 'Z') c = static_cast<char>(byte + ('a' - 'A'));
    }
    return true;
}

std::string basename(std::string name) {
    const auto slash = name.find_last_of("/\\");
    if (slash != std::string::npos) name.erase(0, slash + 1);
    return name;
}

std::optional<std::pair<ArchitectureResourceFamily, std::string>> identify(
    const std::string& raw_name, bool archive_entry,
    ArchitectureResourceCatalogDiagnostics& diagnostics) {
    std::string file = basename(raw_name);
    const auto dot = file.find_last_of('.');
    if (dot == std::string::npos) {
        ++diagnostics.ignored_extensions;
        return std::nullopt;
    }
    std::string extension = file.substr(dot);
    if (!lower_ascii(extension)) {
        ++diagnostics.unsupported_names;
        return std::nullopt;
    }
    ArchitectureResourceFamily family;
    if (extension == ".flr") family = ArchitectureResourceFamily::floor;
    else if (extension == ".wll") family = ArchitectureResourceFamily::wall;
    else {
        ++diagnostics.ignored_extensions;
        return std::nullopt;
    }
    // Simitone's TS1 loose-file provider uses Path.GetFileNameWithoutExtension;
    // its FAR provider uses the prefix before the first dot in the entry key.
    file.resize(archive_entry ? file.find('.') : dot);
    if (!lower_ascii(file)) {
        ++diagnostics.unsupported_names;
        return std::nullopt;
    }
    return std::make_pair(family, std::move(file));
}

void charge(std::size_t amount, std::size_t limit, std::size_t& used,
            const char* message) {
    if (amount > limit || used > limit - amount) throw FormatError(message);
    used += amount;
}

std::uint64_t regular_file_size(const std::string& path) {
    std::error_code ec;
    const auto status = std::filesystem::status(path, ec);
    if (ec || !std::filesystem::is_regular_file(status))
        throw FormatError("architecture resource source is not a regular file");
    const auto size = std::filesystem::file_size(path, ec);
    if (ec) throw FormatError("cannot stat architecture resource source");
    return size;
}
} // namespace

std::vector<ArchitectureCatalogResource> ArchitectureResourceCatalog::floors() const {
    std::vector<ArchitectureCatalogResource> result;
    for (const auto& resource : resources)
        if (resource.family == ArchitectureResourceFamily::floor)
            result.push_back(resource.catalog);
    return result;
}

std::vector<ArchitectureCatalogResource> ArchitectureResourceCatalog::walls() const {
    std::vector<ArchitectureCatalogResource> result;
    for (const auto& resource : resources)
        if (resource.family == ArchitectureResourceFamily::wall)
            result.push_back(resource.catalog);
    return result;
}

ArchitectureResourceCatalog discover_architecture_resources(
    const std::vector<ArchitectureResourceSource>& sources,
    const ArchitectureResourceCatalogLimits& limits) {
    if (sources.size() > limits.sources)
        throw FormatError("architecture resource source limit exceeded");

    ArchitectureResourceCatalog result;
    std::map<Key, ArchitectureResourceRecord> winners;
    std::size_t paths = 0, names = 0, candidates = 0;

    for (const auto& source : sources) {
        if (source.path.empty() || source.path.size() > limits.path_length)
            throw FormatError("architecture resource path length limit exceeded");
        charge(source.path.size(), limits.path_bytes, paths,
               "architecture resource path byte limit exceeded");
    }
    result.sources = sources;

    auto add = [&](std::size_t source_index, std::size_t entry_index,
                   const std::string& entry_name, bool archive_entry,
                   std::uint64_t offset, std::uint64_t size) {
        if (entry_name.size() > limits.name_length)
            throw FormatError("architecture resource name length limit exceeded");
        charge(entry_name.size(), limits.name_bytes, names,
               "architecture resource name byte limit exceeded");
        const auto identity = identify(entry_name, archive_entry, result.diagnostics);
        if (!identity) return;
        charge(identity->second.size(), limits.name_bytes, names,
               "architecture resource canonical name byte limit exceeded");
        // The normalized name is retained once as the catalog key and once in
        // the public record so the latter can be moved out without re-parsing.
        charge(identity->second.size(), limits.name_bytes, names,
               "architecture resource metadata byte limit exceeded");
        if (++candidates > limits.resource_entries)
            throw FormatError("architecture resource entry limit exceeded");
        ArchitectureResourceRecord record;
        record.catalog.name = identity->second;
        record.family = identity->first;
        record.source = {source_index, entry_index, entry_name, offset, size};
        const Key key{record.family, record.catalog.name};
        const auto found = winners.find(key);
        if (found != winners.end()) {
            if (record.family == ArchitectureResourceFamily::floor)
                ++result.diagnostics.duplicate_floor_names;
            else
                ++result.diagnostics.duplicate_wall_names;
            found->second = std::move(record); // Later source/manifest entry wins.
        } else {
            winners.emplace(key, std::move(record));
        }
    };

    for (std::size_t source_index = 0; source_index < sources.size(); ++source_index) {
        const auto& source = sources[source_index];
        if (source.kind == ArchitectureResourceSourceKind::loose_file) {
            const auto size = regular_file_size(source.path);
            ++result.diagnostics.loose_sources;
            const auto name = basename(source.path);
            add(source_index, 0, name, false, 0, size);
            continue;
        }

        if (source.kind != ArchitectureResourceSourceKind::far1_archive)
            throw FormatError("unsupported architecture resource source kind");
        ++result.diagnostics.far_sources;
        std::ifstream stream(source.path, std::ios::binary);
        if (!stream) throw FormatError("cannot open architecture FAR1 source");
        const auto size = regular_file_size(source.path);
        View archive(stream, size);
        Limits far_limits;
        far_limits.far_entries = limits.far_entries_per_archive;
        far_limits.far_name_length = static_cast<std::uint32_t>(
            std::min<std::size_t>(limits.name_length, UINT32_MAX));
        far_limits.far_name_bytes = limits.far_name_bytes_per_archive;
        const auto entries = far1_index(archive, far_limits);
        for (std::size_t entry_index = 0; entry_index < entries.size(); ++entry_index) {
            const auto& entry = entries[entry_index];
            add(source_index, entry_index, entry.name, true, entry.offset, entry.length);
        }
    }

    std::size_t next_floor = first_dynamic_id, next_wall = first_dynamic_id;
    const auto capacity = static_cast<std::size_t>(last_dynamic_id - first_dynamic_id + 1);
    std::size_t floor_count = 0, wall_count = 0;
    for (const auto& [key, record] : winners) {
        (void)record;
        if (key.family == ArchitectureResourceFamily::floor) ++floor_count;
        else ++wall_count;
    }
    if (floor_count > capacity || wall_count > capacity)
        throw FormatError("architecture resource dynamic ID range exhausted");

    result.resources.reserve(winners.size());
    for (auto& [key, record] : winners) {
        auto& next = key.family == ArchitectureResourceFamily::floor ? next_floor : next_wall;
        record.catalog.id = static_cast<std::uint16_t>(next++);
        result.resources.push_back(std::move(record));
    }
    return result;
}

} // namespace odh
