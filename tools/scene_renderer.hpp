#pragma once

#include "odh/animation.hpp"
#include "odh/compositor.hpp"
#include "odh/projection.hpp"
#include "odh/avatar_hands.hpp"
#include "odh/avatar_head_seek.hpp"
#include "odh/world.hpp"
#include "odh/avatar_display.hpp"
#include <functional>
#include <memory>
#include <optional>

struct SceneHandPart {
    bool right = false;
    odh::HandGesture gesture = odh::HandGesture::idle;
};
struct ScenePart {
    std::string appearance_entry, appearance_name, bmf_entry, texture_entry;
    // Untagged parts always render. Tagged parts match the current side gesture.
    std::optional<SceneHandPart> hand{};
    // Traditional TS1 CAS assets are loose CMX/SKN/BMP files. The renderer
    // reads them through the same Mesh/Bitmap structures as archive assets.
    bool loose_skin_assets = false;
    // Optional exact FAR provenance for mixed base/expansion/CC parts. Empty
    // fields retain the legacy SceneConfig animation/textures archives.
    std::string appearance_far, mesh_far, texture_far;
    bool loose_mesh_asset=false, loose_texture_asset=false;
};

struct SceneConfig {
    std::string animation_far, skeleton_entry, skeleton_name, textures_far;
    // Both empty, with object_id0, selects an avatar-only scene.
    std::string object_far, object_entry;
    std::uint16_t object_id = 0;
    std::vector<ScenePart> parts;
    // Independent rig provenance. Empty retains the legacy animation FAR;
    // CFP and unqualified part resolution keep animation_far unchanged.
    std::string skeleton_far;
};

struct SceneAnimationLayer {
    const odh::AnimationHeader* animation=nullptr;
    double frame=0;
    float weight=0;
};
struct SceneState {
    // Graphics tile coordinates: X/Y planar, Z up. VM conversion belongs to caller.
    std::array<float, 3> avatar_tile{}, object_tile{};
    std::uint8_t avatar_direction = 0, object_direction = 0;
    // Borrowed only during render(); caller owns the immutable header.
    const odh::AnimationHeader* animation = nullptr;
    double frame = 0;
    odh::AvatarHands hands{};
    std::uint16_t owner_id = 0; // Optional pixel-ownership token for scene diagnostics.
    // Simitone's head icon leaves the HEAD bone intact and collapses every
    // other absolute bone transform to zero. Camera framing remains caller-owned.
    bool head_only = false;
    // Native fresh carry profile: frame0 weight1 local-channel overlay after
    // the ordinary clip. Imported nonzero carry frames are not represented.
    const odh::AnimationHeader* carry_animation = nullptr;
    double carry_frame=0; // Original carry is sampled statically at its saved frame.
    // Ordered native locomotion collection. When present this supplies the
    // primary pose; animation/frame above remain the lane0 diagnostic identity.
    std::vector<SceneAnimationLayer> animation_layers;
    std::optional<float> avatar_radians;
    // No World position (e.g. in a departing vehicle): retain the scene/camera
    // and objects but emit no avatar geometry, pose sampling or bone anchors.
    bool avatar_visible = true;
    std::uint16_t display_flags=0;bool is_ghost=false;
    std::optional<std::array<float,3>> ground_normal;
    std::optional<std::uint64_t> simulation_tick;
    std::optional<odh::AvatarHeadSeekSnapshot> head_seek;
};

// World remains the sole runtime bank owner. Legacy layouts without these
// words retain opaque/visible defaults; original persons have all source fields.
inline void apply_avatar_display(SceneState& state,const odh::Entity& body){
    const auto hidden=body.object_words.find(34);if(hidden!=body.object_words.end()&&hidden->second!=0)state.avatar_visible=false;
    state.display_flags=body.person_data.size()>74?static_cast<std::uint16_t>(body.person_data[74]):0;
    state.is_ghost=body.person_data.size()>68&&body.person_data[68]>0;
}

// Final same-draw scale-one bone attachment points in graphics tile coordinates.
// Slot order is R_FINGER0, HEAD, PELVIS; missing bones remain absent.
// Positions already include SceneState.avatar_tile (and its terrain offset).
struct ScenePoseAnchors {
    std::array<std::optional<std::array<float,3>>,3> avatar_slots{};
    std::optional<std::array<float,12>> neck_matrix;
    std::optional<std::array<float,3>> headline_target;
};

// CPU portrait profile based on Simitone's UIIconCache defaults. Projection is
// independent of the lot's isometric WorldProjection; visual calibration is
// not implied by matching these camera settings.
struct HeadPortraitCamera {
    std::uint32_t viewport_size = 74;
    double vertical_fov_radians = 1.0471975511965976; // PI/3
    double zoom = 13.0;
    double distance = 13.5;
    double yaw_radians = 0.4636476090008061;  // atan(0.5)
    double pitch_radians = -0.4636476090008061; // atan(-0.5)
    double target_y_offset = 0.1;
    double near_plane = 1.0;
};

// Must resolve the exact selected header, not merely an equal animation name.
// The owned planes remain alive through the draw. An AnimationCatalog adapter
// can return an aliasing shared_ptr over decode(selected)->planes, without a
// second allocation, decode, or renderer sample ledger.
using SceneCfpResolver = std::function<std::shared_ptr<const odh::CfpPlanes>(const odh::AnimationHeader&)>;

// Bounded CPU host for one avatar and an optional single-part object's graphic.
// Rig, raw meshes, textures and unique DGRP sprite frames are loaded once.
// All hand variants count toward the same 16-part and resident asset budgets;
// selection skips geometry before skinning. None selects no tagged side parts.
// CFP coordinate planes are cached on first use, with exact case-folded xskill
// resolution and no anonymous fallback. Per-frame geometry is never cached.
// The host is move-only and not thread-safe. Original resources remain read-only.
class SceneRenderer {
public:
    explicit SceneRenderer(const SceneConfig&, SceneCfpResolver = {});
    ~SceneRenderer();
    SceneRenderer(SceneRenderer&&) noexcept;
    SceneRenderer& operator=(SceneRenderer&&) noexcept;
    SceneRenderer(const SceneRenderer&) = delete;
    SceneRenderer& operator=(const SceneRenderer&) = delete;

    odh::CompositeFrame render(const SceneState&, const odh::WorldProjection&,
                               odh::RasterOptions = {}, ScenePoseAnchors* anchors=nullptr);
    // Current composed bone facts using the exact draw sampling path, without rasterizing.
    ScenePoseAnchors pose_anchors(const SceneState&);
    // Perspective view around HEAD; 74px by default, bounded to 16..512px.
    // Always applies the head-only pose.
    odh::CompositeFrame render_head_portrait(const SceneState&,
        HeadPortraitCamera = {}, odh::RasterOptions = {});
    // Renderer-owned legacy cache only. An external resolver owns/accounts its
    // cache; resolved planes are borrowed with shared ownership for each draw.
    std::uint64_t cache_samples() const;
    std::size_t cache_clips() const;
    std::uint64_t vertices() const;     // Resident real mesh vertices, all variants.
    std::uint64_t triangles() const;
    std::uint64_t texture_bytes() const; // Decoded RGBA.
    std::uint64_t sprite_bytes() const;  // Unique decoded RGBA/index/Z arrays.

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
