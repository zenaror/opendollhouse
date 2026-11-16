// SPDX-License-Identifier: MPL-2.0
#include "app/LotScenePresentation.h"
#include "app/WorldPicking.h"
#include "world/World.h"
#include <algorithm>
#include <array>
#include <cmath>

int main() {
    // Deliberately odd extents and distinct row boundaries expose overwriting
    // unread source pixels when the live overlay borrows the output canvas.
    const std::array<uint32_t, 6> pixels{1, 2, 3, 4, 5, 6};
    for (bool inPlace : {false, true}) {
        std::array<uint32_t, 24> output{};
        std::copy(pixels.begin(), pixels.end(), output.begin());
        simapp::ExpandLotScene2x(inPlace ? output.data() : pixels.data(), output.data(), 3, 2);
        for (int y = 0; y < 4; ++y) for (int x = 0; x < 6; ++x)
            if (output[y * 6 + x] != pixels[(y / 2) * 3 + x / 2]) return 1;
    }

    using namespace FSO::LotView;
    World desktop(800,600);
    for (int zoom = 1; zoom <= 3; ++zoom) {
        const int scale = zoom == 1 ? 1 : 2;
        World handheld(800/scale,600/scale);
        handheld.PresentationScale = scale;
        desktop.State.Zoom(WorldZoom(zoom));
        handheld.State.Zoom(WorldZoom(zoom - (scale == 2 ? 1 : 0)));
        if (desktop.ViewZoom() != handheld.ViewZoom()) return 2;
        for (int direction = 0; direction < 4; ++direction) {
            for (auto* world : {&desktop, &handheld}) {
                world->State.CenterTile({31.25f,28.5f});
                world->State.Rotation(WorldRotation(direction));
            }
            st::Vector2 desktopBasis[2],handheldBasis[2];
            desktop.GetScrollBasis(desktopBasis); handheld.GetScrollBasis(handheldBasis);
            if (desktopBasis[0] != handheldBasis[0] || desktopBasis[1] != handheldBasis[1]) return 3;
            const auto a = desktop.State.WorldSpace.GetTileAtPosWithScroll({731,409});
            const auto b = handheld.State.WorldSpace.GetTileAtPosWithScroll({731.f/scale,409.f/scale});
            if (std::abs(a.X-b.X)>0.01f || std::abs(a.Y-b.Y)>0.01f) return 4;
            const auto p = simapp::TileToScreen(desktop,{33,27,0});
            const auto q = simapp::TileToScreen(handheld,{33,27,0});
            if (std::abs(p.X-q.X*scale)>3 || std::abs(p.Y-q.Y*scale)>3) return 5;
        }
    }
    return 0;
}
