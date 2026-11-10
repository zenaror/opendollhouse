#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace odh_host {

struct AudioFormat {
    std::uint32_t sample_rate = 44100;
    std::uint16_t channels = 2;
};
struct AudioLimits {
    std::uint64_t queued_pcm_bytes = 256 * 1024;
};

// Host-wide SDL lifetime, deliberately separate from a session's device.
// application: the executable claims global SDL ownership before other SDL
// owners and releases this scope only after they finish. external: a foreign
// host owns SDL_Quit; this scope only balances its own audio subsystem leases.
// Both modes load SDL lazily. Same-policy scopes share one loaded state; mixed
// live policies reject rather than silently acquiring global cleanup rights.
// No global strong reference retains SDL. Devices retain the shared state, so
// closing the last device after its host scope still safely orders finalization.
// Construct/use/destroy scopes and devices on the same host thread. No SDL types
// or persistent/simulation state cross this API.
class LinuxAudioRuntime {
public:
    enum class Ownership { application, external };
    explicit LinuxAudioRuntime(Ownership);
    ~LinuxAudioRuntime();
    LinuxAudioRuntime(const LinuxAudioRuntime&)=delete;
    LinuxAudioRuntime& operator=(const LinuxAudioRuntime&)=delete;
private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
    friend class LinuxAudio;
};

// Optional Linux host backend. Native signed PCM16, interleaved L/R for stereo.
// No SDL types enter this API. All calls must be made by one owner thread.
// SDL_AUDIODRIVER is respected; construction opens a device, initially paused.
// Throws std::runtime_error on backend errors, std::invalid_argument on inputs.
class LinuxAudio {
public:
    explicit LinuxAudio(LinuxAudioRuntime&, AudioFormat format, AudioLimits limits = {});
    ~LinuxAudio();
    LinuxAudio(const LinuxAudio&) = delete;
    LinuxAudio& operator=(const LinuxAudio&) = delete;

    AudioFormat format() const;
    std::string driver() const;
    std::string runtime_version() const;
    // Caller provides frames * channels readable samples. Accepts only the
    // prefix fitting the bounded queue, without waiting for playback. Zero
    // frames allow nullptr. Accepted frames are copied before this returns.
    std::size_t queue(const std::int16_t* samples, std::size_t frames);
    // Queue depletion means delivery to the backend, not audible completion.
    std::size_t queued_frames() const;
    void pause();
    void start();
    // Discards queued frames; data already sent to hardware cannot be recalled.
    void clear();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace odh_host
