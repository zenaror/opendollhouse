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

// Optional Linux host backend. Native signed PCM16, interleaved L/R for stereo.
// No SDL types enter this API. All calls must be made by one owner thread.
// SDL_AUDIODRIVER is respected; construction opens a device, initially paused.
// Throws std::runtime_error on backend errors, std::invalid_argument on inputs.
class LinuxAudio {
public:
    explicit LinuxAudio(AudioFormat format, AudioLimits limits = {});
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
