// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "odh/formats.hpp"
#include "odh/host_filesystem.hpp"
#include <algorithm>
#include <cctype>
#include <set>
#include <vector>

// Internal preview discovery shared with its inventory contract tests. The
// iterator still visits every entry, including unrelated custom-content trees.
namespace odh_host::preview_detail {
struct ArchiveInventory {
    std::vector<std::string> archives;
    std::size_t filesystem_entries=0,regular_checks=0;
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
