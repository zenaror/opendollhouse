#pragma once
#include <filesystem>
#include "scene_renderer.hpp"
#include "odh/objm_tail.hpp"
#include "odh/avatar_body.hpp"
#include "odh/wardrobe.hpp"

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
// Bounded source-catalog scan receipts. Metadata failures are isolated to their
// resource; I/O, archive and aggregate-budget failures still abort construction.
struct SavedAppearanceCatalogDiagnostics {
    std::uint64_t malformed_resources=0;
    std::vector<std::string> resource_errors; // At most16 entries, each <=512 bytes.
};
class SavedAppearanceResources {
public:
    SavedAppearanceResources(const std::string& animation_far, const std::string& textures_far,
                             const std::string& skeleton_entry, const std::string& skeleton_name,
                             const std::string& skins_directory = {},
                             const std::vector<std::string>& far_sources = {},
                             const std::vector<std::string>& extra_skin_directories = {});
    ~SavedAppearanceResources();
    SavedAppearanceResources(SavedAppearanceResources&&) noexcept;
    SavedAppearanceResources& operator=(SavedAppearanceResources&&) noexcept;
    SavedAppearance resolve(const odh::ObjmPersonState&);
    const SavedAppearanceCatalogDiagnostics& diagnostics() const;
    // Metadata-only export of THIS provider's selected outfit names/files.
    // No skeleton, scene, mesh, image or animation execution is required.
    static std::vector<odh::WardrobeCatalogAsset> wardrobe_catalog(
        const std::filesystem::path& game_root,const std::string& animation_far,
        const std::string& textures_far,const std::string& skins_directory,
        const std::vector<std::string>& far_sources,const std::vector<std::string>& extra_skin_directories);

    // Construct visual parts directly from BodySTR body/head and six hand
    // records, without a saved person or saved animation state. Each record is
    // appearance[,label=texture]; all eight appearances are required. Reuses
    // the same resource ambiguity, naming and cumulative 16-part limits.
    SceneConfig resolve_body(const odh::AvatarBodyStrings&);
    // Gameplay humans use the skeleton named by BodySTR[0], independently
    // of PersonData age and of the resolver's explicit tool-default rig.
    // Missing/unsupported profiles fail; never stretch child meshes on adult bones.
    SceneConfig resolve_human_body(const odh::AvatarBodyStrings&);
    // Add runtime BoundAppearances names using the same strict OBJM accessory resolver.
    void append_accessories(SceneConfig&, const std::vector<std::string>& names);
    // All six BodySTR 17..22 variants are required and tagged for SceneState.
    // Raw saved hand fields are not interpreted or changed by this opt-in path.
    SavedAppearance resolve_gestures(const odh::ObjmPersonState&, const odh::AvatarBodyStrings&);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
