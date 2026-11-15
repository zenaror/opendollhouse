/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. See https://mozilla.org/MPL/2.0/. */
#pragma once
#include <SDL.h>
#include <atomic>
#include <vector>
#include "audio/AudioBackend.h"

namespace odh::desktop {

// PC retains queued mixing on the game thread. The portable callback uses the
// mixer's existing lock protocol, so rendering stalls do not drain its queue.
class SdlAudio final : public simaudio::AudioBackend {
public:
    enum class Mode { Queued, Continuous };
#if defined(ODH_PLATFORM_VITA) || defined(ODH_PLATFORM_SWITCH)
    static constexpr Mode DefaultMode = Mode::Continuous;
#else
    static constexpr Mode DefaultMode = Mode::Queued;
#endif
    explicit SdlAudio(Mode mode = DefaultMode) : mode_(mode) {}
    ~SdlAudio() override { Stop(); }
    bool Start(simaudio::AudioMixer& mixer) override;
    void Stop() override;
    void Pump(double dtMs) override;
    void SetSuspended(bool suspended) override;
    const char* Name() const override { return mode_ == Mode::Continuous ? "sdl2-continuous" : "sdl2-queued"; }
    double LatencyMs() const override;
    bool Running() const { return device_ != 0; }
private:
    static void Callback(void* context, Uint8* output, int bytes);
    static void Lock(void* mutex) { SDL_LockMutex(static_cast<SDL_mutex*>(mutex)); }
    static void Unlock(void* mutex) { SDL_UnlockMutex(static_cast<SDL_mutex*>(mutex)); }
    Mode mode_;
    SDL_mutex* mutex_ = nullptr;
    unsigned callbackFrames_ = 0;
    std::atomic<bool> callbackFailed_{false};
    SDL_AudioDeviceID device_ = 0;
    simaudio::AudioMixer* mixer_ = nullptr;
    std::vector<int16_t> buffer_;
    bool suspended_ = false;
    bool pumping_ = false;
};

}
