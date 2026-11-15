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

    // Another animation state uses a separate SPR2 chunk. It should stay
    // unopened until its flag actually turns on. This valid v1001 chunk
    // contains zero frames, so opening it cannot add pixels to the fixture.
    SPR2 inactive;
    inactive.ChunkID = 170;
    inactive.ChunkProcessed = false;
    inactive.ChunkData = {0xe9,0x03,0,0, 0,0,0,0, 0,0,0,0};
    iff.AddChunk(&inactive);
    image.Sprites.emplace_back(&group);
    image.Sprites.back().SpriteID = inactive.ChunkID;

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
    renderer.DynamicSpriteBaseID = 102;
    renderer.NumDynamicSprites = 69;
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
    Check(!inactive.ChunkProcessed, "hidden animation state read its sprite chunk");
    renderer.DynamicSpriteFlags2 = uint64_t{1} << 4; // sprite 170 is bit 68
    renderer.InvalidateTextures();
    draw(false);
    Check(inactive.ChunkProcessed && target.Color == normalColor,
        "newly enabled animation state was not resolved");
    auto* pooled = batch.NewSprite(_2DBatchRenderMode::Z_BUFFER);
    pooled->PlacementRejected = true;
    batch.Begin(state.Camera());
    Check(!batch.NewSprite(_2DBatchRenderMode::Z_BUFFER)->PlacementRejected,
        "pooled rejected preview leaked its feedback into another sprite");
    std::cout << (palette ? "palette" : "RGBA") << '/' << (deferred ? "deferred" : "direct")
              << "/room=" << room << ": six normal/mirrored/luminous/unknown flag cases passed\n";
}
// The ordinary sampler remains the oracle. The scene draws a cached frame with
// empty rows, narrow borders, partially transparent pixels and an opaque wall
// mask that can expose colour texels outside the frame's own alpha coverage.
void CheckSpriteCoverage(bool palette) {
    constexpr int width = 32, height = 6;
    SPR2 source;
    SPR2Frame frame(&source);
    frame.Width = width; frame.Height = height;
    const std::array<st::Color,2> colors{{{19,57,99}, {82,78,74}}};
    std::vector<st::Color> maskPixels(width * height, st::Color::White());
    std::vector<uint8_t> depths(width * height);
    frame.PalColors = colors.data();
    for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
        const auto index = size_t(y * width + x);
        const auto a = uint8_t(y == 1 || x < y + 7 || x > 24 - y ? 0 : 17 + x * 7);
        auto c = colors[x % 2]; c.A = a;
        if (palette) {
            frame.PalData.push_back(x % 2);
            frame.AlphaData.push_back(a);
        } else frame.PixelData.push_back(c);
        depths[index] = uint8_t(index);
    }
    WorldState state(28, 7, nullptr);
    _2DWorldBatch batch;
    auto* cached = batch.GetTexture(&frame);
    Texture2D ordinary = *cached;
    ordinary.drawsUntilCoverage = 0;
    Texture2D depth, mask;
    depth.Width = mask.Width = width; depth.Height = mask.Height = height;
    depth.Alpha8 = depths.data(); mask.Data = maskPixels.data();
    auto draw = [&](Texture2D& tex, bool h, bool v, bool wall, bool scaled) {
        Framebuffer fb(28, 7);
        fb.EnableRoom(true);
        fb.SetClip(2, 1, 23, 6);
        batch.Target = &fb;
        batch.Begin(state.Camera());
        auto* sprite = batch.NewSprite(wall ? _2DBatchRenderMode::WALL : _2DBatchRenderMode::Z_BUFFER);
        sprite->Pixel = &tex; sprite->Depth = &depth; sprite->Mask = wall ? &mask : nullptr;
        sprite->SrcRect = st::Rectangle(3, 0, 27, 6);
        sprite->DestRect = st::Rectangle(-2, 0, scaled ? 24 : 27, 7);
        sprite->FlipHorizontally = h; sprite->FlipVertically = v; sprite->Room = 17;
        batch.Draw(sprite); batch.End();
        return fb;
    };
    draw(*cached, false, false, false, false);
    draw(*cached, false, false, false, false);
    Check(cached->coveredRows != nullptr, "reused sparse sprite did not acquire row bounds");
    for (bool h : {false,true}) for (bool v : {false,true}) for (bool wall : {false,true})
        for (bool scaled : {false,true}) {
            const auto clipped = draw(*cached,h,v,wall,scaled);
            const auto reference = draw(ordinary,h,v,wall,scaled);
            Check(clipped.Color == reference.Color && clipped.Room == reference.Room &&
                clipped.Depth == reference.Depth, "sprite row clipping changed output");
        }
}
}
int main() {
    try {
        for (bool palette : {false,true}) CheckSpriteCoverage(palette);
        for (bool palette : {false, true}) for (bool deferred : {false, true})
            for (uint16_t room : {uint16_t(7), uint16_t(65534), uint16_t(65533)})
                for (bool rejected : {false,true}) Run(palette, deferred, room, rejected);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
