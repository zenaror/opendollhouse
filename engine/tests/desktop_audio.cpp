/* SPDX-License-Identifier: MPL-2.0 */
#include "SdlAudio.h"
#include "audio/WavFile.h"
#include <SDL.h>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <stdexcept>

static void require(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}
static uint64_t frames(simaudio::AudioMixer& mixer) {
    simaudio::AudioMixer::Guard guard(mixer);
    return mixer.FramesMixed();
}
static simaudio::SoundBufferPtr sample() {
    auto result = std::make_shared<simaudio::SoundBuffer>();
    result->Channels = 1;
    result->Rate = 48000;
    result->Samples.assign(4000, 500);
    return result;
}
struct ThrowStream final : simaudio::AudioStream {
    int reads = 0;
    int Channels() const override { return 1; }
    int Rate() const override { return 48000; }
    int Read(int16_t* output, int count) override {
        if (++reads > 1) throw std::runtime_error("injected decoder fault");
        std::fill(output, output + count, 200);
        return count;
    }
    bool Rewind() override { return true; }
};
int main(int argc, char** argv) {
    SDL_SetMainReady();
    if (argc != 2) return 2;
    SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
    try {
        using Backend = odh::desktop::SdlAudio;
        simaudio::AudioMixer mixer(48000, 8);
        Backend queued(Backend::Mode::Queued);
        int voice = mixer.Play(sample(), 1, 0, true, simaudio::AudioMixer::GroupMusic);
        require(queued.Start(mixer), "queued device start");
        const auto before = frames(mixer);
        require(queued.LatencyMs() > 0, "queued baseline filled");
        SDL_Delay(300);
        require(frames(mixer) == before && queued.LatencyMs() == 0,
                "baseline reproduces drained audio during main-thread stall");
        queued.Stop();
        mixer.StopAll();

        Backend continuous(Backend::Mode::Continuous);
        voice = mixer.Play(sample(), 1, 0, true, simaudio::AudioMixer::GroupMusic);
        require(continuous.Start(mixer), "continuous device start");
        const auto start = frames(mixer);
        // No Pump call during this stall: SDL must keep serving the active voice.
        SDL_Delay(300);
        const auto mixed = frames(mixer) - start;
        require(mixed >= 8192 && mixer.IsPlaying(voice), "callback progresses independently of main");
        continuous.SetSuspended(true);
        const auto paused = frames(mixer);
        const double position = mixer.Position(voice);
        SDL_Delay(90);
        require(frames(mixer) == paused && mixer.Position(voice) == position,
                "suspended device does not consume source");
        continuous.SetSuspended(false);
        SDL_Delay(90);
        require(frames(mixer) > paused, "resume continues playback");
        // Real main-thread voice operations while the SDL callback owns mixing.
        for (int i = 0; i < 80; ++i) {
            mixer.SetVolumePan(voice, float(i % 8) / 8, float(i % 3 - 1));
            mixer.SetGroupVolume(simaudio::AudioMixer::GroupMusic, .5f);
            int effect = mixer.Play(sample(), .4f, 0, false, simaudio::AudioMixer::GroupSFX);
            mixer.SetPaused(effect, true);
            mixer.SetPaused(effect, false);
            mixer.Stop(effect);
            SDL_Delay(1);
        }
        continuous.Stop();
        continuous.Stop();
        mixer.StopAll();

        const auto directory = std::filesystem::absolute(argv[1]);
        std::filesystem::create_directories(directory);
        const auto path = directory / "stream.wav";
        require(!std::filesystem::exists(path), "fixture path must be new");
        require(simaudio::WriteWavFile(path.string(), *sample()), "write private PCM fixture");
        voice = mixer.PlayStream(simaudio::WavStream::Open(path.string()), 1, 0, false,
                                 simaudio::AudioMixer::GroupMusic);
        require(voice > 0 && continuous.Start(mixer), "stream callback restart");
        SDL_Delay(200);
        require(!mixer.IsPlaying(voice), "finite stream reaches end without Pump");
        continuous.Stop();
        mixer.StopAll();
        voice = mixer.PlayStream(simaudio::WavStream::Open(path.string()), 1, 0, true,
                                 simaudio::AudioMixer::GroupMusic);
        require(voice > 0 && continuous.Start(mixer), "loop stream callback restart");
        SDL_Delay(200);
        require(mixer.IsPlaying(voice), "stream rewinds without Pump");
        continuous.Stop();
        mixer.StopAll();

        // Decoder failure cannot escape through SDL or close the device on its thread.
        mixer.PlayStream(std::make_unique<ThrowStream>(), 1, 0, false,
                         simaudio::AudioMixer::GroupMusic);
        require(continuous.Start(mixer), "fault test start");
        SDL_Delay(120);
        continuous.Pump(0);
        require(!continuous.Running(), "deferred decoder fault stops safely on main");
        mixer.StopAll();
        require(continuous.Start(mixer), "restart after callback error");
        SDL_Delay(40);
        continuous.Stop();
        mixer.StopAll();
        std::filesystem::remove(path);
        SDL_Quit();
        std::cout << "continuous audio: " << mixed << " frames during 300ms main stall; lifecycle, voice operations, PCM stream/end/loop and decoder fault passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "audio contract: " << error.what() << '\n';
        SDL_Quit();
        return 1;
    }
}
