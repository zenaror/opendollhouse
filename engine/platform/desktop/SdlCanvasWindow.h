/* SPDX-License-Identifier: MPL-2.0 */
#pragma once
#include <SDL.h>
namespace odh::desktop {
// The engine canvas and the native window are different sizes on consoles.
// The driver presents the native window; only RenderCopy scales the canvas.
inline SDL_Window* CreateCanvasWindow(int canvasWidth,int canvasHeight,const SDL_Point* physicalSize=nullptr){
    if(physicalSize&&(physicalSize->x<=0||physicalSize->y<=0)){
        SDL_SetError("Invalid physical display size");return nullptr;
    }
    const auto flags=SDL_WINDOW_SHOWN|(physicalSize?0u:Uint32(SDL_WINDOW_RESIZABLE));
    SDL_Window* window=SDL_CreateWindow("OpenDollhouse",SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,
        physicalSize?physicalSize->x:canvasWidth,physicalSize?physicalSize->y:canvasHeight,flags);
    // A 600-pixel minimum would expand the Vita window beyond its 544-row
    // framebuffer. The PC's minimum remains an engine-canvas constraint.
    if(window&&!physicalSize)SDL_SetWindowMinimumSize(window,800,600);
    return window;
}
}
