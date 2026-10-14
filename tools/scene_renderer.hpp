#pragma once

#include "odh/animation.hpp"
#include "odh/compositor.hpp"
#include "odh/projection.hpp"
#include "odh/avatar_hands.hpp"
#include <memory>

struct SceneHandPart {
    bool right = false;
    odh::HandGesture gesture = odh::HandGesture::idle;
};
struct ScenePart {
    std::string appearance_entry, appearance_name, bmf_entry, texture_entry;
    // Untagged parts always render. Tagged parts match the current side gesture.
    std::optional<SceneHandPart> hand{};
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
};

// Bounded CPU host for one avatar and an optional single-part object's graphic.
// Rig, raw meshes, textures and unique DGRP sprite frames are loaded once.
// All hand variants count toward the same 16-part and resident asset budgets;
// selection skips geometry before skinning. None selects no tagged side parts.
// CFP coordinate planes are cached on first use, with exact case-folded xskill
// resolution and no anonymous fallback. Per-frame geometry is never cached.
// The host is move-only and not thread-safe. Original resources remain read-only.
class SceneRenderer {
public:
    explicit SceneRenderer(const SceneConfig&);
    ~SceneRenderer();
    SceneRenderer(SceneRenderer&&) noexcept;
    SceneRenderer& operator=(SceneRenderer&&) noexcept;
    SceneRenderer(const SceneRenderer&) = delete;
    SceneRenderer& operator=(const SceneRenderer&) = delete;

    odh::CompositeFrame render(const SceneState&, const odh::WorldProjection&,
                               odh::RasterOptions = {});
    std::uint64_t cache_samples() const; // Total decoded CFP float32 values.
    std::size_t cache_clips() const;     // Unique cached CFP resources.
    std::uint64_t vertices() const;     // Resident real mesh vertices, all variants.
    std::uint64_t triangles() const;
    std::uint64_t texture_bytes() const; // Decoded RGBA.
    std::uint64_t sprite_bytes() const;  // Unique decoded RGBA/index/Z arrays.

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
