/* SPDX-License-Identifier: MPL-2.0 */
#pragma once
#include <SDL.h>
#include "app/Input.h"

namespace odh::desktop {
// EA/Maxis The Sims PC controls. Kept separate from text-editing events:
// Home/End/Enter remain edits while a field owns the keyboard.
inline simapp::DesktopCommand OriginalHotkey(SDL_Keycode key, SDL_Keymod modifiers,
                                            bool repeat, bool textOwner) {
    using C = simapp::DesktopCommand;
    if (repeat || textOwner || (modifiers & (KMOD_ALT | KMOD_GUI))) return C::None;
    if (modifiers & KMOD_CTRL) return C::None;
    const bool shift = (modifiers & KMOD_SHIFT) != 0;
    if (shift && key != SDLK_EQUALS && key != SDLK_PLUS && key != SDLK_MINUS && key != SDLK_UNDERSCORE && key != SDLK_BACKQUOTE)
        return C::None;
    switch (key) {
    case SDLK_p: case SDLK_BACKQUOTE: return C::Pause;
    case SDLK_F1: return C::Live;
    case SDLK_F2: return C::Buy;
    case SDLK_F3: return C::Build;
    case SDLK_1: return C::SpeedNormal;
    case SDLK_2: return C::SpeedFast;
    case SDLK_3: return C::SpeedUltra;
    case SDLK_EQUALS: case SDLK_PLUS: case SDLK_KP_PLUS: return C::ZoomIn;
    case SDLK_MINUS: case SDLK_UNDERSCORE: case SDLK_KP_MINUS: return C::ZoomOut;
    case SDLK_HOME: return C::WallsUp;
    case SDLK_END: return C::WallsDown;
    case SDLK_SPACE: case SDLK_n: return C::NextSim;
    case SDLK_RETURN: case SDLK_KP_ENTER: return C::CenterSim;
    case SDLK_z: return C::CatalogPrevious;
    case SDLK_x: return C::CatalogNext;
    default: return C::None;
    }
}
inline bool PlainGameplayKey(SDL_Keymod modifiers, bool textOwner) {
    return !textOwner && !(modifiers & (KMOD_CTRL | KMOD_ALT | KMOD_GUI | KMOD_SHIFT));
}
}
