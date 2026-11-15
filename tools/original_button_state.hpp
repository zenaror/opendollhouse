// SPDX-License-Identifier: MPL-2.0
#pragma once
namespace odh_host {
// Authored four-frame TS1 controls: resting, selected/pressed, hover, disabled.
// This chooses existing artwork only; it never changes a control's action.
inline constexpr unsigned original_button_frame(bool enabled,bool selected,bool hovered,bool captured=false) noexcept {
    return !enabled?3U:selected||(hovered&&captured)?1U:hovered?2U:0U;
}
}
