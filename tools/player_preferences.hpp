// SPDX-License-Identifier: MPL-2.0
#pragma once
namespace odh_host {
// Presentation preferences. Platform adapters own persistence, not the World
// or the game save. Defaults preserve the existing native presentation.
struct PlayerPreferences {
    bool lighting=true;
    bool auto_center=true;
    bool edge_scrolling=true;
};
}
