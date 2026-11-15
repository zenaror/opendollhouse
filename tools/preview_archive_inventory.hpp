// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "odh/formats.hpp"
#include "odh/installed_directory.hpp"
#include "odh/host_filesystem.hpp"
#include <algorithm>
#include <cctype>
#include <set>
#include <vector>
#include <memory>
#if defined(__vita__) || defined(__SWITCH__) || defined(__linux__)
#include <dirent.h>
#endif

// Internal preview discovery shared with its inventory contract tests. The
// iterator still visits every entry, including unrelated custom-content trees.
namespace odh_host::preview_detail {
struct ArchiveInventory {
    std::vector<std::string> archives;
    std::size_t filesystem_entries=0,regular_checks=0,live_directories_reused=0;
};
inline ArchiveInventory discover_archives(const std::filesystem::path& root,
        std::size_t entry_limit=200000,std::size_t archive_limit=256) {
    ArchiveInventory result;std::set<std::string> unique;
    for(const auto& entry:std::filesystem::recursive_directory_iterator(root)){
        if(++result.filesystem_entries>entry_limit)
            throw odh::FormatError("CAS preview installation index exceeds limit");
        auto extension=entry.path().extension().string();
        std::transform(extension.begin(),extension.end(),extension.begin(),
            [](unsigned char c){return char(std::tolower(c));});
        if(extension!=".far")continue;
        ++result.regular_checks;
        if(!entry.is_regular_file())continue;
        const auto full=odh::hostfs::canonical(entry.path());
        const auto relative=full.lexically_relative(root);
        if(relative.empty()||odh::hostfs::is_absolute(relative)||
           std::any_of(relative.begin(),relative.end(),[](const auto& part){return part=="..";}))
            throw odh::FormatError("CAS preview asset escapes installation");
        unique.insert(full.string());
    }
    if(unique.size()>archive_limit)throw odh::FormatError("CAS preview archive count exceeds limit");
    result.archives.assign(unique.begin(),unique.end());
    return result;
}
}

#if defined(__vita__) || defined(__SWITCH__) || defined(__linux__)
namespace odh_host::preview_detail {
using SkinPaths = std::vector<std::vector<std::filesystem::path>>;

// Device mounts have no symlinks. Their directory records already contain the
// file type (Vita's newlib exposes d_stat rather than d_type). Keep just one
// directory open, including on deeply nested custom-content installations.
inline ArchiveInventory discover_device_archives(const std::filesystem::path& root,
        const std::vector<std::string>& skin_roots, SkinPaths& skins,
        odh::LiveInstallDirectory cached_directory=nullptr,
        std::size_t entry_limit=200000, std::size_t archive_limit=256) {
    struct Directory { std::filesystem::path path; std::size_t skin_root; };
    const auto none = skin_roots.size();
    std::vector<Directory> pending{{root, none}};
    skins.assign(none, {});
    ArchiveInventory inventory;
    std::size_t loose_files = 0, loose_path_bytes = 0;
    while (!pending.empty()) {
        auto current = std::move(pending.back());
        pending.pop_back();
        // The portable source roots do not overlap.
        if (current.skin_root == none) {
            const auto current_name = current.path.string();
            const auto found = std::find_if(skin_roots.begin(), skin_roots.end(), [&](const auto& path) {
                return path.size() == current_name.size() && std::equal(path.begin(), path.end(), current_name.begin(),
                    [](unsigned char a, unsigned char b){return std::tolower(a) == std::tolower(b);});
            });
            current.skin_root = static_cast<std::size_t>(found - skin_roots.begin());
        }
        auto add = [&](std::string_view name, bool is_directory, bool regular) {
            if (++inventory.filesystem_entries > entry_limit)
                throw odh::FormatError("CAS preview installation index exceeds limit");
            const auto path = current.path / name;
            if (is_directory) {
                pending.push_back({path, current.skin_root});
                return;
            }
            if (!regular) return;
            auto ext = path.extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c){return char(std::tolower(c));});
            if (ext == ".far") {
                if (inventory.archives.size() >= archive_limit)
                    throw odh::FormatError("CAS preview archive count exceeds limit");
                inventory.archives.push_back(path.string());
            } else if (current.skin_root != none && (ext == ".cmx" || ext == ".skn" || ext == ".bmp")) {
                if (++loose_files > 65536 || (loose_path_bytes += path.native().size()) > 4u * 1024u * 1024u)
                    throw odh::FormatError("saved appearance loose skin index budget exceeded");
                skins[current.skin_root].push_back(path);
            }
        };
        if (cached_directory && cached_directory(current.path.string(), [&](const std::string& name, bool dir) {
                add(name, dir, !dir);
            })) {
            ++inventory.live_directories_reused;
            continue;
        }
        const auto close_dir=[](DIR* dir){closedir(dir);};
        std::unique_ptr<DIR, decltype(close_dir)> directory(opendir(current.path.c_str()), close_dir);
        if (!directory) throw odh::FormatError("cannot list preview directory: " + current.path.string());
        while (true) {
            errno = 0;
            const auto* entry = readdir(directory.get());
            if (!entry) {
                if (errno) throw odh::FormatError("cannot read preview directory: " + current.path.string());
                break;
            }
            const std::string_view name(entry->d_name);
            if (name == "." || name == "..") continue;
#ifdef __vita__
            const bool is_directory = SCE_S_ISDIR(entry->d_stat.st_mode);
            const bool regular = SCE_S_ISREG(entry->d_stat.st_mode);
#else
            bool is_directory = entry->d_type == DT_DIR, regular = entry->d_type == DT_REG;
            if (entry->d_type == DT_UNKNOWN) {
                const auto status = std::filesystem::symlink_status(current.path / entry->d_name);
                is_directory = std::filesystem::is_directory(status);
                regular = std::filesystem::is_regular_file(status);
                ++inventory.regular_checks;
            }
#endif
            add(name, is_directory, regular);
        }
    }
    std::sort(inventory.archives.begin(), inventory.archives.end());
    for(auto& group:skins) std::sort(group.begin(),group.end());
    return inventory;
}
}
#endif
