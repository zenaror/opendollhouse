/* SPDX-License-Identifier: MPL-2.0 */
/* Linux test interposer: fail one checked SDL operation after an owned arm file. */
#include <SDL.h>
#include <dlfcn.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int inject(const char *operation) {
    static int fired;
    const char *kind = getenv("ODH_PRESENT_FAULT_KIND");
    const char *arm = getenv("ODH_PRESENT_FAULT_ARM");
    if (fired || !kind || !arm || strcmp(kind, operation) || access(arm, F_OK))
        return 0;
    fired = 1;
    SDL_SetError("presentation fault fixture: %s", operation);
    return 1;
}

int SDL_UpdateTexture(SDL_Texture *texture, const SDL_Rect *rect,
                      const void *pixels, int pitch) {
    typedef int (*Function)(SDL_Texture *, const SDL_Rect *, const void *, int);
    static Function real;
    if (inject("upload")) return -1;
    if (!real) real = (Function)dlsym(RTLD_NEXT, "SDL_UpdateTexture");
    return real ? real(texture, rect, pixels, pitch) : -1;
}

int SDL_RenderClear(SDL_Renderer *renderer) {
    typedef int (*Function)(SDL_Renderer *);
    static Function real;
    if (inject("clear")) return -1;
    if (!real) real = (Function)dlsym(RTLD_NEXT, "SDL_RenderClear");
    return real ? real(renderer) : -1;
}

int SDL_RenderCopy(SDL_Renderer *renderer, SDL_Texture *texture,
                   const SDL_Rect *source, const SDL_Rect *destination) {
    typedef int (*Function)(SDL_Renderer *, SDL_Texture *, const SDL_Rect *, const SDL_Rect *);
    static Function real;
    if (inject("copy")) return -1;
    if (!real) real = (Function)dlsym(RTLD_NEXT, "SDL_RenderCopy");
    return real ? real(renderer, texture, source, destination) : -1;
}
