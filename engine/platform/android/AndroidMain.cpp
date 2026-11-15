/* SPDX-License-Identifier: MPL-2.0 */
// SDL invokes the same one-canvas application used on desktop. Platform-specific
// paths and startup arguments are supplied by GameActivity, not by another VM.
#include <SDL.h>
#include <SDL_main.h>
#include "../desktop/main.cpp"
