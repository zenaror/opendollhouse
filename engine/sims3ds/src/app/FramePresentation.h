/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

namespace simapp {

// Describes this frame's completed drawing, not backend readiness or permission
// to click. The desktop host reports it only after submitting the canvas.
struct FramePresentation {
    enum class Scene { Other, Loading, Map, Lot };
    Scene scene = Scene::Other;
    int house = -1, area = -1, lot_mode = -1;
    bool modal = false, family_picker = false, destination_picker = false;
    bool cheats = false, pie = false;

    bool operator==(const FramePresentation& other) const {
        return scene == other.scene && house == other.house && area == other.area &&
            lot_mode == other.lot_mode && modal == other.modal &&
            family_picker == other.family_picker && destination_picker == other.destination_picker &&
            cheats == other.cheats && pie == other.pie;
    }
};

} // namespace simapp
