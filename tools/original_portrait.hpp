// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "scene_renderer.hpp"
#include <cmath>

// The original UI presents a face at eye level. Keep this UI profile separate
// from the explicit Simitone camera defaults retained by SceneRenderer.
inline HeadPortraitCamera original_frontal_portrait_camera() {
    HeadPortraitCamera camera;
    camera.yaw_radians=0;
    camera.pitch_radians=0;
    return camera;
}

// Family-selection portraits fill the 41px interior of PeopleTemplate.bmp.
// Measured against the four original Cortes portraits; this is not a change
// to the separately rendered live HUD or pie menu.
inline HeadPortraitCamera original_family_portrait_camera() {
    auto camera=original_frontal_portrait_camera();
    camera.zoom=22;
    return camera;
}

// Simitone UIPieMenu.RotateHeadCam, with the hovered option (or pointer)
// expressed in display pixels. This profile belongs only to the action menu.
inline HeadPortraitCamera original_pie_portrait_camera(std::array<std::int32_t,2> center,
        std::array<std::int32_t,2> target) {
    auto camera=original_frontal_portrait_camera();
    camera.viewport_size=200;
    camera.zoom=5.12;
    camera.target_y_offset=0;
    camera.yaw_radians=std::atan((double(center[0])-target[0])/100.0);
    camera.pitch_radians=std::atan((double(center[1])-target[1])/100.0);
    return camera;
}
