#include "odh/character_catalog.hpp"

#include <algorithm>
#include <fstream>

namespace odh {
namespace {
bool iff_extension(const std::filesystem::path& path) {
    auto extension = path.extension().string();
    for (auto& ch : extension)
        if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
    return extension == ".iff";
}
}

std::vector<DefinitionCandidate> read_character_catalog_if_present(
    const std::filesystem::path& directory,const CharacterCatalogLimits& limits){
    if(directory.empty())throw FormatError("optional character catalog has no source path");
    for(const auto& component:directory)if(component=="..")
        throw FormatError("optional character catalog path contains parent traversal");
    std::error_code error;
    const auto status=std::filesystem::symlink_status(directory,error);
    if(error==std::errc::no_such_file_or_directory||
       (!error&&status.type()==std::filesystem::file_type::not_found))return {};
    // Reuse the required reader for every existing/error case. In particular
    // a dangling symlink is an existing symlink, never a missing catalog.
    return read_character_catalog(directory,limits);
}

std::vector<DefinitionCandidate> read_character_catalog(
    const std::filesystem::path& directory, const CharacterCatalogLimits& limits) {
    std::error_code error;
    const auto directory_status=std::filesystem::symlink_status(directory,error);
    if (error || std::filesystem::is_symlink(directory_status) ||
        !std::filesystem::is_directory(directory_status))
        throw FormatError("character catalog path is not a readable directory");

    std::vector<std::filesystem::directory_entry> files;
    std::uint32_t entries_seen=0;
    for (std::filesystem::directory_iterator it(directory, error), end;
         !error && it != end; it.increment(error)) {
        if(entries_seen>=limits.directory_entries)
            throw FormatError("character catalog directory entry count exceeds configured limit");
        ++entries_seen;
        const auto& entry = *it;
        if (!iff_extension(entry.path())) continue;
        if (files.size() >= limits.files)
            throw FormatError("character catalog file count exceeds configured limit");
        const auto status = entry.symlink_status(error);
        if (error) throw FormatError("cannot inspect character catalog entry");
        if (std::filesystem::is_symlink(status))
            throw FormatError("character catalog does not follow .iff symlinks");
        if (std::filesystem::is_regular_file(status)) files.push_back(entry);
    }
    if (error) throw FormatError("cannot enumerate character catalog directory");
    std::sort(files.begin(), files.end(), [](const auto& a, const auto& b) {
        return a.path().filename().native() < b.path().filename().native();
    });

    const auto source = directory.lexically_normal().string();
    if (source.size() > limits.provenance_bytes)
        throw FormatError("character catalog path exceeds provenance limit");
    std::uint64_t total_bytes = 0;
    std::uint64_t provenance_bytes = source.size();
    std::vector<DefinitionCandidate> result;
    for (const auto& entry : files) {
        const auto size = entry.file_size(error);
        if (error) throw FormatError("cannot size character IFF");
        if (size > limits.file_bytes || size > limits.total_file_bytes - total_bytes)
            throw FormatError("character catalog file byte budget exceeded");
        total_bytes += size;
        std::ifstream stream(entry.path(), std::ios::binary);
        if (!stream) throw FormatError("cannot open character IFF");
        const View iff(stream, size);
        Limits chunk_limits;
        chunk_limits.iff_chunks = limits.chunks_per_file;
        iff_chunks(iff, [&](const Chunk& chunk) {
            if (chunk.type != "OBJD") return;
            const auto definition = read_object_definition(chunk.payload);
            if (definition.version != 138 || definition.object_type != 2) return;
            const auto asset = entry.path().filename().string();
            if (result.size() >= limits.person_definitions ||
                source.size() > limits.provenance_bytes - provenance_bytes ||
                asset.size() > limits.provenance_bytes - provenance_bytes - source.size())
                throw FormatError("character catalog definition/provenance budget exceeded");
            provenance_bytes += source.size() + asset.size();
            result.push_back({source, asset, chunk.id, definition});
        }, chunk_limits);
    }
    return result;
}

} // namespace odh
