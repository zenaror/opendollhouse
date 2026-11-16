/* SPDX-License-Identifier: MPL-2.0 */
#include "SdlAudio.h"
#include "audio/WavFile.h"
#include "audio/GameAudio.h"
#include "content/Audio.h"
#include "files/LoadProgress.h"
#include <algorithm>
#include <fstream>
#include <iterator>
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
static simaudio::SoundBufferPtr sample(int16_t value = 500) {
    auto result = std::make_shared<simaudio::SoundBuffer>();
    result->Channels = 1;
    result->Rate = 48000;
    result->Samples.assign(4000, value);
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
// Listen to the first frames while the bank index is still being built.
static void checkLoadingMusic(const std::filesystem::path& root) {
    using namespace simaudio;
    using FSO::Files::LoadProgress;
    require(!std::filesystem::exists(root), "startup fixture path must be new");
    std::filesystem::create_directories(root / "SoundData");
    std::filesystem::create_directories(root / "Music/Modes/Load");
    std::ofstream(root / "SoundData/test.hot") << "[EventMappingEquate]\n";
    const auto active = root / "Music/Modes/Load/loadloop.wav";
    const auto backup = root / "Music/Modes/Load/loadloop.old.wav";
    require(WriteWavFile(active.string(), *sample()), "write active loading loop");
    require(WriteWavFile(backup.string(), *sample(-500)), "write distinct original backup");
    auto& audio = GameAudio::Get();
    GameAudio::Config config;
    config.DataRoot = root.string();
    config.MusicVolume = 0.25f;
    auto verify = [&](const std::filesystem::path& expectedPath, int sign) {
        bool indexedWhilePlaying = false;
        LoadProgress::ProgressSink = [&](const std::string&, size_t, size_t) {
            if (indexedWhilePlaying) return;
            require(!audio.Ready() && audio.Backend(), "loading music precedes bank completion");
            int16_t pcm[128] = {};
            audio.Mixer()->Mix(pcm, 64);
            require(std::any_of(std::begin(pcm), std::end(pcm), [sign](int16_t v){return v * sign > 0;}),
                    "mixer must play the selected file while indexing");
            require(audio.Mixer()->GroupVolume(AudioMixer::GroupMusic) == config.MusicVolume, "saved loading volume");
            indexedWhilePlaying = true;
        };
        auto capture = (root / "capture.wav").string();
        bool ready = audio.Init(config, std::make_unique<WavCaptureBackend>(capture), true);
        LoadProgress::ProgressSink = {};
        require(ready && indexedWhilePlaying, "loading loop audible during indexing");
        auto tracks = FSO::Content::Audio::Get()->ListMusic("Music/Modes/Load/");
        require(tracks.size() == 1 && tracks.front() == expectedPath.string(), "HIT must use the same single loading loop");
        audio.SetMusicMode(MusicMode::Load);
        require(audio.ActiveVoices() == 1, "repeated loading state keeps one loop");
        audio.SetMusicMode(MusicMode::Fade);
        int16_t tail[256] = {};
        audio.Mixer()->Mix(tail, 128);
        require(audio.ActiveVoices() == 0, "leaving the loading screen stops the early loop");
        audio.Shutdown();
    };
    verify(active, 1);
    std::ofstream(active, std::ios::binary | std::ios::trunc) << "damaged";
    verify(backup, -1);
    std::filesystem::remove(active);
    verify(backup, -1);
    std::filesystem::remove(backup);
    std::string selected;
    require(!FSO::Content::Audio::OpenLoadingMusic(root.string(), selected) && selected.empty(), "missing music");
    std::filesystem::remove_all(root);
}

// Tiny streams expose song-boundary behavior without waiting for retail tracks.
static void checkEditingMusic(const std::filesystem::path& root) {
    using namespace simaudio;
    require(!std::filesystem::exists(root), "editing fixture must be new");
    std::filesystem::create_directories(root / "SoundData");
    std::ofstream(root / "SoundData/test.hot") <<
        "[EventMappingEquate]\nkSetMusicMode=36\n[EventMapping]\nbkground_buy1=kSetMusicMode,1\nbkground_build=kSetMusicMode,2\nbkground_live=kSetMusicMode,0\nbkground_nhood1=kSetMusicMode,3\nbkground_fade=kSetMusicMode,9\n";
    auto track = std::make_shared<SoundBuffer>();
    track->Channels=1;track->Rate=48000;track->Samples.assign(48000,500);
    for (const char* mode : {"Buy", "Build", "Nhood"}) {
        auto folder=root / "Music/Modes" / mode;
        std::filesystem::create_directories(folder);
        require(WriteWavFile((folder / "song1.wav").string(),*track),"editing test song");

    }
    auto& audio=GameAudio::Get();
    GameAudio::Config config; config.DataRoot=root.string(); config.OutputRate=48000;
    require(audio.Init(config),"editing audio init");
    std::vector<std::string> starts;
    audio.Log=[&](const std::string& event){if(event.find(": music-start ")!=std::string::npos)starts.push_back(event);};
    auto advance=[&](int milliseconds) {
        int16_t pcm[960];
        for(int elapsed=0;elapsed<milliseconds;elapsed+=10){audio.Update(10);audio.Mixer()->Mix(pcm,480);}
    };
    audio.SetMusicMode(MusicMode::Buy);advance(100);
    require(starts.size()==1,"Buy starts one track");
    audio.SetMusicMode(MusicMode::Build);advance(200);
    require(starts.size()==1,"Build preserves the current Buy song");
    audio.SetMusicMode(MusicMode::Buy);advance(1100);
    require(starts.size()==2 && starts.back().find("bkground_buy1:")==0,"returning to Buy cancels pending Build");
    audio.SetMusicMode(MusicMode::Build);advance(1100);
    require(starts.size()==3 && starts.back().find("bkground_build:")==0,"Build playlist starts at next song boundary");
    audio.SetMusicMode(MusicMode::Buy);advance(100);
    require(starts.size()==3,"Buy preserves the current Build song");
    audio.SetMusicMode(MusicMode::Live);advance(100);
    audio.SetMusicMode(MusicMode::Build);advance(3500);
    require(starts.size()>3 && starts.back().find("bkground_build:")==0,
            "Live cancels pending Buy and re-entering Build restarts after fade");
    audio.SetMusicMode(MusicMode::Neighborhood);advance(3200);
    const auto count=starts.size();
    require(count>0 && starts.back().find("bkground_nhood1:")==0,"map music starts");
    audio.BeginResidentialLoad();advance(100);
    require(audio.ActiveVoices()>0,"map music fades rather than ending at the click");
    advance(600);
    require(audio.ActiveVoices()==0 && starts.size()==count,"residential load fades without starting boot music");
    audio.SetMusicMode(MusicMode::Neighborhood);advance(200);
    const auto beforeFailure=starts.size();
    audio.BeginResidentialLoad();advance(100);
    audio.SetMusicMode(MusicMode::Neighborhood);advance(700);
    require(starts.size()==beforeFailure+1 && audio.ActiveVoices()>0 &&
            starts.back().find("bkground_nhood1:")==0,
            "quick load failure restores map music even while its previous song fades");
    audio.Log={};audio.Shutdown();std::filesystem::remove_all(root);
}

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
        checkLoadingMusic(directory / "startup");
        checkEditingMusic(directory / "editing");
        SDL_Quit();
        std::cout << "continuous audio: " << mixed << " frames during 300ms main stall; lifecycle, voice operations, PCM stream/end/loop and decoder fault passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "audio contract: " << error.what() << '\n';
        SDL_Quit();
        return 1;
    }
}
