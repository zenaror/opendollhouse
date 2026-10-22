#pragma once

#include "odh/definition_index.hpp"
#include <filesystem>

namespace odh {

struct CharacterCatalogLimits {
    std::uint32_t directory_entries = 8192;
    std::uint32_t files = 4096;
    std::uint32_t chunks_per_file = 4096;
    std::uint64_t file_bytes = 64 * 1024 * 1024;
    std::uint64_t total_file_bytes = 512 * 1024 * 1024;
    std::uint32_t person_definitions = 8192;
    std::uint64_t provenance_bytes = 256 * 1024;
};

// Reads a bounded, deterministically ordered snapshot of OBJD138 persons in
// one explicit UserData/Characters directory. Other files and non-person
// definitions are ignored. An unreadable or malformed .iff fails the scan.
std::vector<DefinitionCandidate> read_character_catalog(
    const std::filesystem::path& directory,
    const CharacterCatalogLimits& limits = {});

} // namespace odh
