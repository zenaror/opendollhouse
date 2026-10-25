#pragma once

#include "odh/animation.hpp"
#include "odh/compositor.hpp"
#include "odh/projection.hpp"
#include "odh/avatar_hands.hpp"
#include <functional>
#include <memory>

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
};

struct SceneConfig {
    std::string animation_far, skeleton_entry, skeleton_name, textures_far;
    // Both empty, with object_id0, selects an avatar-only scene.
    std::string object_far, object_entry;
    std::uint16_t object_id = 0;
    std::vector<ScenePart> parts;
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
};

// CPU portrait profile based on Simitone's UIIconCache defaults. Projection is
// independent of the lot's isometric WorldProjection; visual calibration is
// not implied by matching these camera settings.
struct HeadPortraitCamera {
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
                               odh::RasterOptions = {});
    // Fixed 74x74 portrait view around HEAD; always applies the head-only pose.
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
