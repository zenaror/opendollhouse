// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "odh/bitmap.hpp"
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace odh_host {
// Original catalog BMP_ optional chunk, bounded to the thumbnail profile.
std::optional<odh::BitmapImage> read_object_catalog_icon(const odh::View&,std::uint16_t strings_id);

// Owns only the requested screen's BMP or true-colour alpha TGA artwork.
// Construction reads the installation
// once; lookup, layout and painting never open files.
class OriginalUiArt {
public:
    explicit OriginalUiArt(std::map<std::string,odh::BitmapImage> images);
    OriginalUiArt(const std::filesystem::path& game_root,const std::vector<std::string>& required,
        const std::vector<std::string>& optional={},std::size_t pixel_limit=2*1024*1024);
    const odh::BitmapImage* find(const std::string& key) const;
    const odh::BitmapImage& at(const std::string& key) const;
private:
    struct ArchiveCache;
    std::shared_ptr<ArchiveCache> archive_;
    std::map<std::string,std::shared_ptr<const odh::BitmapImage>> images_;
};
}
