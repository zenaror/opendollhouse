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
// Source ConvertAnimation fields (at least six); no resource lookup. Header supplied separately.
SavedAnimationAppearance saved_animation_appearance(const std::string&, const odh::AnimationHeader&);
struct SavedAppearance {
    SceneConfig scene;
    std::optional<SavedAnimationAppearance> current, base, carry;
    // Base is preserved for evidence; FreeSO's new activator does not restore it.
    bool base_executed = false;
};
// Bounded source-catalog scan receipts. Metadata failures are isolated to their
// resource; I/O, archive and aggregate-budget failures still abort construction.
struct SavedAppearanceCatalogDiagnostics {
    std::uint64_t malformed_resources=0;
    std::vector<std::string> resource_errors; // At most16 entries, each <=512 bytes.
};
// One bounded metadata owner, with separate ordered indices for TS1 clothing
// collection selection. assets retains the legacy wardrobe export order/ordinals;
// index vectors add no second copy of the asset strings/content receipts.
struct SavedAppearanceResourceCatalog {
    std::vector<odh::WardrobeCatalogAsset> assets;
    std::vector<std::uint32_t> appearance_order,texture_order;
};
class SavedAppearanceResources {
public:
    enum class Declarations { All, VisualFirst };
    SavedAppearanceResources(const std::string& animation_far, const std::string& textures_far,
                             const std::string& skeleton_entry, const std::string& skeleton_name,
                             const std::string& skins_directory = {},
                             const std::vector<std::string>& far_sources = {},
                             const std::vector<std::string>& extra_skin_directories = {},
                             const std::vector<std::vector<std::filesystem::path>>* installed_skins = nullptr,
                             const std::filesystem::path& name_cache = {},
                             Declarations declarations = Declarations::All);
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

    static SavedAppearanceResourceCatalog resource_catalog(
        const std::filesystem::path& game_root,const std::string& animation_far,
        const std::string& textures_far,const std::string& skins_directory,
        const std::vector<std::string>& far_sources,const std::vector<std::string>& extra_skin_directories);

    // Construct visual parts directly from BodySTR body/head and six hand
    // records, without a saved person or saved animation state. Each record is
    // appearance[,label=texture]; body/head are required. An authored empty
    // hand record emits no geometry for that side/gesture, with no fallback.
    // Nonempty records retain strict syntax/resource and cumulative limits.
    SceneConfig resolve_body(const odh::AvatarBodyStrings&);
    // Gameplay humans use the skeleton named by BodySTR[0], independently
    // of PersonData age and of the resolver's explicit tool-default rig.
    // Missing/unsupported profiles fail; never stretch child meshes on adult bones.
    SceneConfig resolve_human_body(const odh::AvatarBodyStrings&);
    // Gameplay type selects its rig from installed skeleton names. Human
    // profiles retain required body/head and optional authored hand gestures;
    // cat/dog profiles have no human handgroup. Empty heads and a proved
    // absent optional head appearance produce no head geometry; incomplete
    // BCF/CMX name metadata and failures of selected resources remain errors.
    // Cat/kat classification never substitutes another literal skeleton name.
    SceneConfig resolve_avatar_body(const odh::AvatarBodyStrings&);
    // Live head portrait: exact current skeleton and optional body/head parts.
    // No BodySTR defaults, hand substitutions or saved-person state.
    SceneConfig resolve_live_portrait(const std::string& skeleton,const std::string& body,
        const std::string& head,const std::array<std::string,6>& hands,
        const std::vector<std::string>& accessories);
    // Add runtime BoundAppearances names using the same strict OBJM accessory resolver.
    void append_accessories(SceneConfig&, const std::vector<std::string>& names);
    // Nonempty BodySTR 17..22 variants are tagged for SceneState; an empty
    // record selects no hand geometry for that side/gesture, never idle fallback.
    // Raw saved hand fields are not interpreted or changed by this opt-in path.
    SavedAppearance resolve_gestures(const odh::ObjmPersonState&, const odh::AvatarBodyStrings&);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
