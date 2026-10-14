#pragma once
#include "scene_renderer.hpp"
#include "odh/objm_tail.hpp"

struct SavedAnimationAppearance {
    std::string raw, entry, cfp_entry;
    odh::AnimationHeader header;
    std::int32_t priority = 0, speed_milli = 0, normalized_frame = 0, weight_milli = 0;
    std::int32_t unknown1 = 0, unknown2 = 0;
    bool loop = false, backwards = false, end_reached = false;
    double frame = 0;
};
// Eight exact semicolon fields; no resource lookup. Header supplied separately.
SavedAnimationAppearance saved_animation_appearance(const std::string&, const odh::AnimationHeader&);
struct SavedAppearance {
    SceneConfig scene;
    std::optional<SavedAnimationAppearance> current, base;
    // Base is preserved for evidence; FreeSO's new activator does not restore it.
    bool base_executed = false;
};
class SavedAppearanceResources {
public:
    SavedAppearanceResources(const std::string& animation_far, const std::string& textures_far,
                             const std::string& skeleton_entry, const std::string& skeleton_name);
    ~SavedAppearanceResources();
    SavedAppearanceResources(SavedAppearanceResources&&) noexcept;
    SavedAppearanceResources& operator=(SavedAppearanceResources&&) noexcept;
    SavedAppearance resolve(const odh::ObjmPersonState&);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
