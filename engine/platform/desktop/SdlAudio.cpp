/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. See https://mozilla.org/MPL/2.0/. */
#include "SdlAudio.h"
#include <algorithm>
#include <cstdio>

namespace odh::desktop {

bool SdlAudio::Start(simaudio::AudioMixer& mixer) {
    Stop();
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        std::fprintf(stderr, "audio: SDL initialization failed: %s\n", SDL_GetError());
        return false;
    }
    SDL_AudioSpec requested{};
    requested.freq = mixer.OutputRate();
    requested.format = AUDIO_S16SYS;
    requested.channels = 2;
    requested.samples = 1024;
    mixer_ = &mixer;
    if (mode_ == Mode::Continuous) {
        mutex_ = SDL_CreateMutex();
        if (!mutex_) {
            std::fprintf(stderr, "audio: mixer mutex failed: %s\n", SDL_GetError());
            Stop();
            return false;
        }
        mixer.SetLock(Lock, Unlock, mutex_);
        requested.callback = Callback;
        requested.userdata = this;
    }
    SDL_AudioSpec obtained{};
    device_ = SDL_OpenAudioDevice(nullptr, 0, &requested, &obtained, 0);
    if (!device_) {
        std::fprintf(stderr, "audio: no playback device: %s\n", SDL_GetError());
        Stop();
        return false;
    }
    callbackFrames_ = obtained.samples;
    suspended_ = false;
    buffer_.resize(2048);
    Pump(0);
    SDL_PauseAudioDevice(device_, 0);
    std::printf("audio: SDL playback at %d Hz, stereo, %s\n", mixer.OutputRate(), Name());
    return true;
}

void SdlAudio::Stop() {
    // Close waits for the last callback, before its mixer or lock can disappear.
    if (device_) SDL_CloseAudioDevice(device_);
    device_ = 0;
    if (mutex_) {
        if (mixer_) mixer_->SetLock(nullptr, nullptr, nullptr);
        SDL_DestroyMutex(mutex_);
        mutex_ = nullptr;
    }
    mixer_ = nullptr;
    callbackFrames_ = 0;
    callbackFailed_ = false;
    buffer_.clear();
}

void SdlAudio::Pump(double) {
    if (mode_ == Mode::Continuous) {
        if (callbackFailed_.load()) {
            std::fprintf(stderr, "audio: decoder failed in SDL callback; playback stopped\n");
            Stop();
        }
        return;
    }
    if (!device_ || !mixer_ || suspended_ || pumping_) return;
    pumping_ = true;
    struct PumpScope { bool& active; ~PumpScope() { active = false; } } scope{pumping_};
    // A bounded queue absorbs short UI/render stalls without accumulating delay.
    constexpr unsigned bytesPerFrame = 2 * sizeof(int16_t);
    const unsigned target = static_cast<unsigned>(mixer_->OutputRate()) / 12; // ~83 ms
    unsigned queued = SDL_GetQueuedAudioSize(device_) / bytesPerFrame;
    while (queued < target) {
        const unsigned count = std::min(1024u, target - queued);
        mixer_->Mix(buffer_.data(), static_cast<int>(count));
        if (SDL_QueueAudio(device_, buffer_.data(), count * bytesPerFrame) != 0) {
            std::fprintf(stderr, "audio: queue failed: %s\n", SDL_GetError());
            Stop();
            return;
        }
        queued += count;
    }
}

void SdlAudio::Callback(void* context, Uint8* output, int bytes) {
    if (!output || bytes <= 0) return;
    SDL_memset(output, 0, bytes);
    auto& self = *static_cast<SdlAudio*>(context);
    if (!self.mixer_ || self.callbackFailed_.load()) return;
    try {
        self.mixer_->Mix(reinterpret_cast<int16_t*>(output), bytes / (2 * sizeof(int16_t)));
    } catch (...) {
        // No exceptions, device shutdown or log IO may cross the SDL thread.
        SDL_memset(output, 0, bytes);
        self.callbackFailed_ = true;
    }
}

void SdlAudio::SetSuspended(bool suspended) {
    suspended_ = suspended;
    if (device_) SDL_PauseAudioDevice(device_, suspended ? 1 : 0);
}

double SdlAudio::LatencyMs() const {
    if (mode_ == Mode::Continuous)
        return device_ && mixer_ ? callbackFrames_ * 1000.0 / mixer_->OutputRate() : 0;
    return device_ && mixer_
        ? SDL_GetQueuedAudioSize(device_) * 1000.0 / (4.0 * mixer_->OutputRate()) : 0;
}

}
