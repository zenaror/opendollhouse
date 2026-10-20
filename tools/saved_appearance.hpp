#pragma once
#include "scene_renderer.hpp"
#include "odh/objm_tail.hpp"
#include "odh/avatar_body.hpp"

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
    // Construct visual parts directly from BodySTR body/head and six hand
    // records, without a saved person or saved animation state. Each record is
    // appearance[,label=texture]; all eight appearances are required. Reuses
    // the same resource ambiguity, naming and cumulative 16-part limits.
    SceneConfig resolve_body(const odh::AvatarBodyStrings&);
    // All six BodySTR 17..22 variants are required and tagged for SceneState.
    // Raw saved hand fields are not interpreted or changed by this opt-in path.
    SavedAppearance resolve_gestures(const odh::ObjmPersonState&, const odh::AvatarBodyStrings&);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
