#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace odh_host {

struct LinuxCasOutfitChoice {
    std::string appearance;
    std::string texture;
    std::string body_type;
    std::string handgroup_texture;
};

struct LinuxCasCatalogStats {
    std::uint32_t loose_cmx_files = 0;
    std::uint32_t far_archives = 0;
    std::uint32_t far_appearance_entries = 0;
    std::uint32_t skipped_appearance_files = 0;
    std::uint32_t texture_names = 0;
};

class LinuxCasCatalog {
public:
    struct AppearanceName { char category; std::string collection_type; std::string name; };
    const LinuxCasCatalogStats& stats() const noexcept { return stats_; }
    // Heads use an exact avatar type such as "ma" or "fc". Bodies use the
    // Simitone GeneralAvatarType prefix, such as "ma" or "mcchd".
    std::vector<LinuxCasOutfitChoice> heads(const std::string& avatar_type,
                                            const std::string& skin) const;
    std::vector<LinuxCasOutfitChoice> bodies(const std::string& avatar_type,
                                             const std::string& skin) const;

private:
    friend LinuxCasCatalog scan_linux_cas_catalog(const std::filesystem::path&);
    std::vector<AppearanceName> appearances_;
    std::vector<std::string> textures_;
    LinuxCasCatalogStats stats_;
};

// Read TS1 outfit descriptions and texture names from the installed game tree.
// It reads only asset metadata and never extracts or changes game files.
LinuxCasCatalog scan_linux_cas_catalog(const std::filesystem::path& game_root);

} // namespace odh_host
