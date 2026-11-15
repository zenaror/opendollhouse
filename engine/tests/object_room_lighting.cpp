/* SPDX-License-Identifier: MPL-2.0 */
#include "files/formats/iff/IffFile.h"
#include "files/formats/iff/chunks/DGRP.h"
#include "files/formats/iff/chunks/SPR2.h"
#include "world/WorldState.h"
#include "world/utils/DGRPRenderer.h"
#include "world/utils/_2DWorldBatch.h"
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace FSO::Files::Formats::IFF;
using namespace FSO::Files::Formats::IFF::Chunks;
using namespace FSO::LotView;
using namespace FSO::LotView::Utils;

namespace {
void Check(bool ok, const std::string& context) {
    if (!ok) throw std::runtime_error(context);
}

void Run(bool palette, bool deferred, uint16_t room, bool rejected = false) {
    // Deliberately use on-disk flag values, not enum values as expectations.
    // Both pinned FreeSO references define Flip=1 and Luminous=4.
    constexpr std::array<uint32_t, 6> flags{{0, 1, 4, 5, 0x12, 0x17}};
    const std::array<st::Color, 2> colors{{st::Color(200,80,40), st::Color(40,160,80)}};
    IffFile iff;
    SPR2 spr;
    spr.ChunkID = 100;
    spr.ChunkProcessed = true;
    auto frame = std::make_unique<SPR2Frame>(&spr);
    frame->Width = 2; frame->Height = 1;
    frame->ZBufferData = {128, 128};
    if (palette) {
        frame->PalColors = colors.data();
        frame->PalData = {0, 1};
        frame->AlphaData = {255, 255};
    } else frame->PixelData.assign(colors.begin(), colors.end());
    spr.Frames.push_back(std::move(frame));
    iff.AddChunk(&spr);
    DGRP group;
    group.ChunkParent = &iff;
    group.Images.emplace_back(&group);
    auto& image = group.Images.back();
    image.Direction = 1; image.Zoom = 3;
    for (size_t i = 0; i < flags.size(); ++i) {
        image.Sprites.emplace_back(&group);
        auto& sprite = image.Sprites.back();
        sprite.SpriteID = 100;
        sprite.Flags = static_cast<DGRPSpriteFlags>(flags[i]);
        sprite.SpriteOffset = {float(i * 3), 0};
        sprite.ObjectOffset = {0, 0, 0};
    }

    WorldState state(20, 4, nullptr);
    _2DWorldBatch batch;
    state._2D = &batch;
    // Place the tiny fixture at the top left while retaining native selection,
    // texture preparation, sprite projection and the real depth rasterizer.
    state.WorldSpace.CadgeWidth = 0;
    state.WorldSpace.CadgeBaseLine = 1;
    Framebuffer target(20, 4);
    target.EnableRoom(deferred);
    batch.Target = &target;
    batch.OffsetPixel({0, 0}); batch.OffsetTile({0, 0, 0}); batch.SetScroll({0, 0});
    std::vector<st::Color> ambient(65536, st::Color::White());
    ambient[7] = st::Color(64, 64, 64); // 25% room illumination
    ambient[65534] = st::Color(255, 128, 64); // preview/selection sentinel
    batch.AmbientLight = ambient.data();
    DGRPRenderer renderer(&group);
    renderer.Room = room;
    const auto draw = [&](bool invalid) {
        renderer.PlacementRejected = invalid;
        target.Clear(0);
        batch.Begin(state.Camera());
        renderer.Draw(state);
        batch.End();
    };
    draw(false);
    const auto normalColor = target.Color;
    const auto normalDepth = target.Depth;
    const auto normalRoom = target.Room;
    draw(rejected);
    Check(batch.SpritesDrawn == int(flags.size()), "DGRP fixture did not render all parts");
    for (size_t i = 0; i < flags.size(); ++i) {
        const bool mirrored = i == 1 || i == 3 || i == 5;
        const bool luminous = i == 2 || i == 3 || i == 5;
        const std::string context = std::string(palette ? "palette" : "RGBA") +
            (deferred ? "/deferred" : "/direct") + "/room=" + std::to_string(room) +
            "/flags=" + std::to_string(flags[i]);
        for (size_t x = 0; x < 2; ++x) {
            const size_t source = mirrored ? 1-x : x;
            const auto& color = colors[source];
            unsigned r = color.R, g = color.G, b = color.B;
            if (room == 65533 || rejected) {
                // Disabled preview is grayscale even for luminous parts.
                r = g = b = source == 0 ? 111 : 115;
                if (rejected) { r = (3*r+255)/4; g = b = 3*g/4; }
            }
            if (!deferred && room == 7 && !luminous) { r /= 4; g /= 4; b /= 4; }
            if (!deferred && room == 65534) { g = g * 129 / 256; b /= 4; }
            const auto pixel = i*3+x;
            Check(target.Color[pixel] == Framebuffer::Pack(r,g,b,255), context+" incorrect lit or mirrored pixel");
            Check(std::isfinite(target.Depth[pixel]), context+" missing depth");
            if (deferred) {
                const uint16_t expectedRoom = room == 65533 ? 65535 :
                    (room == 7 && luminous ? 65535 : room);
                Check(target.Room[pixel] == expectedRoom, context+" incorrect deferred room");
            }
        }
        Check(target.Color[i*3+2] == 0, context+" painted outside sprite");
    }
    Check(target.Depth == normalDepth && target.Room == normalRoom,
        "placement feedback changed sprite coverage/depth/deferred lighting ownership");
    draw(false);
    Check(target.Color == normalColor && target.Depth == normalDepth && target.Room == normalRoom,
        "valid placement did not restore the exact normal frame on cached DGRP sprites");
    auto* pooled = batch.NewSprite(_2DBatchRenderMode::Z_BUFFER);
    pooled->PlacementRejected = true;
    batch.Begin(state.Camera());
    Check(!batch.NewSprite(_2DBatchRenderMode::Z_BUFFER)->PlacementRejected,
        "pooled rejected preview leaked its feedback into another sprite");
    std::cout << (palette ? "palette" : "RGBA") << '/' << (deferred ? "deferred" : "direct")
              << "/room=" << room << ": six normal/mirrored/luminous/unknown flag cases passed\n";
}
}
int main() {
    try {
        for (bool palette : {false, true}) for (bool deferred : {false, true})
            for (uint16_t room : {uint16_t(7), uint16_t(65534), uint16_t(65533)})
                for (bool rejected : {false,true}) Run(palette, deferred, room, rejected);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
