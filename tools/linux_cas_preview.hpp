// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "linux_simitone_cas_member.hpp"
#include "odh/bitmap.hpp"
#include "odh/avatar_body.hpp"
#include <filesystem>
#include <memory>
namespace odh_host {
// Snapshot of the actual live Vitaboy selections, never immutable BodySTR.
// Outfit identities/skin are part of the cache key even when their selected
// named resources happen to agree. Empty head/body means no such binding.
struct LiveAvatarPortraitInput {
    std::string skeleton,body,head;
    std::array<std::string,6> hands;
    std::vector<std::string> accessories;
    std::uint64_t head_outfit=0,body_outfit=0;
    int skin_tone=0;
};
class LinuxCasPreview {
public:
    explicit LinuxCasPreview(const std::filesystem::path& game_root,
                             const std::filesystem::path& cache_root = {});
    ~LinuxCasPreview();
    // Actual selected assets, magenta outside their rasterized silhouette.
    // portrait selects the same frontal face profile used by the live HUD.
    const odh::BitmapImage* image(const SimitoneCasPersonInput&,bool portrait);
    const odh::BitmapImage* portrait(const odh::AvatarBodyStrings&,bool child);
    // Portable gameplay HUD service. Uses BodySTR's actual human/animal rig,
    // the existing SavedAppearanceResources and complete SceneRenderer path.
    // Returns a composed 35x41 HUD cell with transparent background. Preserve
    // its canvas at consumers: pets must not be enlarged to fill a button.
    // Versioned, bounded memory cache only; no save, world or VM is created.
    const odh::BitmapImage* hud_portrait(const odh::AvatarBodyStrings&);
    // Queue fallback only. Same frontal renderer, current parts/accessories.
    const odh::BitmapImage* live_portrait(const LiveAvatarPortraitInput&);
    // Live 3D action-menu head; no HUD fitting and no world animation state.
    // Owns one renderer and one 200px frame, valid until the next pie call.
    const odh::BitmapImage* pie_portrait(const odh::AvatarBodyStrings&,
        std::array<std::int32_t,2> center,std::array<std::int32_t,2> target);
    // Entire quadruped in the PC pet selector, using the chosen appearance and rig.
    odh::BitmapImage pet_body(const odh::AvatarBodyStrings&);
    const std::string& portrait_source() const;
    const std::string& diagnostic() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
