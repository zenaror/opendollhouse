// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "linux_simitone_cas_member.hpp"
#include "odh/bitmap.hpp"
#include "odh/avatar_body.hpp"
#include <filesystem>
#include <memory>
namespace odh_host {
class LinuxCasPreview {
public:
    explicit LinuxCasPreview(const std::filesystem::path& game_root);
    ~LinuxCasPreview();
    // Actual selected assets, magenta outside their rasterized silhouette.
    // portrait selects the same frontal face profile used by the live HUD.
    const odh::BitmapImage* image(const SimitoneCasPersonInput&,bool portrait);
    const odh::BitmapImage* portrait(const odh::AvatarBodyStrings&,bool child);
    const std::string& diagnostic() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
