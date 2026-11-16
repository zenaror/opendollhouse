// SPDX-License-Identifier: MPL-2.0
#include "simantics/VM.h"
#include "simantics/VMContext.h"
#include "simantics/engine/VMThread.h"
#include "simantics/entities/VMAvatar.h"
#include "world/World.h"
#include <cmath>
#include <cstdio>

namespace {
class SilentSound final : public FSO::HIT::HITSound {
public:
    bool Tick() override { return true; }
    void Pause() override {}
    void Resume() override {}
    void Dispose() override {}
};
}

int main() {
    using namespace FSO::LotView;
    using namespace FSO::SimAntics;
    VM::UseWorld(true);
    FSO::Files::Formats::IFF::Chunks::OBJD definition;
    definition.GUID = 0x12345678;
    FSO::Files::Formats::IFF::IffFile iff;
    FSO::Content::GameObjectResource resource(&iff, nullptr, nullptr, "sound-test", true);
    FSO::Content::GameObject object;
    object.OBJ = &definition;
    object.GUID = definition.GUID;
    object.Resource = &resource;

    for (const int scale : {1, 2}) {
        World world(800 / scale, 600 / scale);
        world.PresentationScale = scale;
        world.State.Zoom(scale == 2 ? WorldZoom::Far : WorldZoom::Medium);
        world.State.CenterTile({24, 24});
        VMContext context(&world);
        VMAvatar sim(&object);
        sim.Thread = std::make_unique<Engine::VMThread>(&context, &sim, 8);
        auto sound = std::make_shared<SilentSound>();
        FSO::SimAntics::Model::Sound::VMSoundEntry entry;
        entry.Sound = sound;
        entry.Pan = true;
        sim.SoundThreads.push_back(entry);

        // Move the source across the screen. The VM must use this world's
        // camera and width even when the renderer uses a smaller lot surface.
        auto& space = world.State.WorldSpace;
        for (const float fraction : {0.25f, 0.75f}) {
            const auto tile = space.GetTileAtPosWithScroll(
                {fraction * space.WorldPxWidth, 0.5f * space.WorldPxHeight});
            sim.WorldUI->Position = {tile.X, tile.Y, 0};
            const auto pixel = space.GetScreenFromTile(sim.WorldUI->DrawPosition()) + space.GetScreenOffset();
            const auto expectedPan = pixel.X / space.WorldPxWidth - 0.5f;
            sound->VolumeSet = false;
            sim.TickSounds();
            if (!sound->VolumeSet || !std::isfinite(sound->Pan) ||
                (fraction < 0.5f ? sound->Pan >= 0 : sound->Pan <= 0) ||
                std::abs(sound->Pan - expectedPan) > 0.001f) {
                std::fprintf(stderr, "scale %d: expected pan %.3f, observed %.3f (set=%d)\n",
                             scale, expectedPan, sound->Pan, int(sound->VolumeSet));
                return 1;
            }
        }
    }
}
