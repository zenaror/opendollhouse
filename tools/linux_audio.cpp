#include "linux_audio.hpp"

#include <algorithm>
#include <cstring>
#include <dlfcn.h>
#include <limits>
#include <stdexcept>

namespace odh_host {
namespace {
// Isolated Linux SDL2 ABI subset, verified against official SDL2 headers:
// https://github.com/libsdl-org/SDL/blob/SDL2/include/SDL_audio.h
// https://github.com/libsdl-org/SDL/blob/SDL2/include/SDL.h
// https://github.com/libsdl-org/SDL/blob/SDL2/include/SDL_version.h
// SDL2 uses the ordinary C calling convention on Linux. These are local ABI
// declarations, not an SDL header replacement for the rest of the program.
struct AudioSpec {
    int freq;
    std::uint16_t format;
    std::uint8_t channels, silence;
    std::uint16_t samples, padding;
    std::uint32_t size;
    void (*callback)(void*, std::uint8_t*, int);
    void* userdata;
};
struct Version { std::uint8_t major, minor, patch; };
static_assert(sizeof(int) == 4 && sizeof(Version) == 3, "SDL2 Linux ABI");
static_assert(offsetof(AudioSpec, callback) == 16, "SDL2 audio callback ABI");
static_assert(offsetof(AudioSpec, userdata) == 16 + sizeof(void*), "SDL2 audio userdata ABI");
static_assert(sizeof(AudioSpec) == 16 + 2 * sizeof(void*), "SDL2 audio spec ABI");
constexpr std::uint32_t init_audio = 0x00000010u;
std::uint16_t native_s16() {
    const std::uint16_t value = 1;
    return *reinterpret_cast<const unsigned char*>(&value) ? 0x8010 : 0x9010;
}

template<class T> T symbol(void* library, const char* name) {
    dlerror();
    void* address = dlsym(library, name);
    const char* error = dlerror();
    if (error || !address)
        throw std::runtime_error(std::string("SDL2 symbol ") + name + ": " +
                                 (error ? error : "missing"));
    static_assert(sizeof(T) == sizeof(address), "Linux function pointer ABI");
    T result;
    std::memcpy(&result, &address, sizeof(result));
    return result;
}
} // namespace

struct LinuxAudio::Impl {
    void* library = nullptr;
    bool initialized = false;
    std::uint32_t device = 0;
    AudioFormat format;
    std::uint32_t capacity_bytes = 0, frame_bytes = 0;
    Version version{};
    int (*init)(std::uint32_t) = nullptr;
    void (*quit)(std::uint32_t) = nullptr;
    const char* (*error)() = nullptr;
    const char* (*current_driver)() = nullptr;
    void (*get_version)(Version*) = nullptr;
    std::uint32_t (*open)(const char*, int, const AudioSpec*, AudioSpec*, int) = nullptr;
    void (*close)(std::uint32_t) = nullptr;
    int (*queue_audio)(std::uint32_t, const void*, std::uint32_t) = nullptr;
    std::uint32_t (*queued_bytes)(std::uint32_t) = nullptr;
    void (*pause_audio)(std::uint32_t, int) = nullptr;
    void (*clear_audio)(std::uint32_t) = nullptr;

    ~Impl() {
        if (device) close(device);
        if (initialized) quit(init_audio); // Balance only our initialization.
        if (library) dlclose(library);
    }
    [[noreturn]] void fail(const char* operation) const {
        const char* detail = error();
        throw std::runtime_error(std::string("SDL2 ") + operation + ": " +
                                 (detail && *detail ? detail : "unknown error"));
    }
    std::uint32_t queued() const {
        const auto bytes = queued_bytes(device);
        if (bytes % frame_bytes || bytes > capacity_bytes)
            throw std::runtime_error("SDL2: queued bytes violate frame/capacity contract");
        return bytes;
    }
};

LinuxAudio::LinuxAudio(AudioFormat format, AudioLimits limits)
    : impl_(std::make_unique<Impl>()) {
    auto& p = *impl_;
    if (format.sample_rate < 8000 || format.sample_rate > 192000 ||
        (format.channels != 1 && format.channels != 2))
        throw std::invalid_argument("audio: requires PCM16 mono/stereo, 8000..192000 Hz");
    p.frame_bytes = static_cast<std::uint32_t>(format.channels) * sizeof(std::int16_t);
    if (!limits.queued_pcm_bytes ||
        limits.queued_pcm_bytes > std::numeric_limits<std::uint32_t>::max() ||
        limits.queued_pcm_bytes % p.frame_bytes)
        throw std::invalid_argument("audio: queue budget must be positive, frame-aligned and fit uint32");
    p.format = format;
    p.capacity_bytes = static_cast<std::uint32_t>(limits.queued_pcm_bytes);
    p.library = dlopen("libSDL2-2.0.so.0", RTLD_NOW | RTLD_LOCAL);
    if (!p.library) {
        const char* detail = dlerror();
        throw std::runtime_error(std::string("SDL2 load: ") + (detail ? detail : "unknown error"));
    }
    p.init = symbol<decltype(p.init)>(p.library, "SDL_InitSubSystem");
    p.quit = symbol<decltype(p.quit)>(p.library, "SDL_QuitSubSystem");
    p.error = symbol<decltype(p.error)>(p.library, "SDL_GetError");
    p.current_driver = symbol<decltype(p.current_driver)>(p.library, "SDL_GetCurrentAudioDriver");
    p.get_version = symbol<decltype(p.get_version)>(p.library, "SDL_GetVersion");
    p.open = symbol<decltype(p.open)>(p.library, "SDL_OpenAudioDevice");
    p.close = symbol<decltype(p.close)>(p.library, "SDL_CloseAudioDevice");
    p.queue_audio = symbol<decltype(p.queue_audio)>(p.library, "SDL_QueueAudio");
    p.queued_bytes = symbol<decltype(p.queued_bytes)>(p.library, "SDL_GetQueuedAudioSize");
    p.pause_audio = symbol<decltype(p.pause_audio)>(p.library, "SDL_PauseAudioDevice");
    p.clear_audio = symbol<decltype(p.clear_audio)>(p.library, "SDL_ClearQueuedAudio");
    p.get_version(&p.version);
    if (p.version.major != 2 || (p.version.minor == 0 && p.version.patch < 4))
        throw std::runtime_error("audio: SDL2 2.0.4 or newer required for queue audio");
    if (p.init(init_audio) != 0) p.fail("initialize audio");
    p.initialized = true;
    AudioSpec wanted{};
    wanted.freq = static_cast<int>(format.sample_rate);
    wanted.format = native_s16();
    wanted.channels = static_cast<std::uint8_t>(format.channels);
    wanted.samples = 512;
    AudioSpec obtained{};
    // No permitted changes: SDL must accept this application-facing format.
    // SDL may internally convert for the physical device; we don't claim that
    // the DAC operates at the source rate or source sample format.
    p.device = p.open(nullptr, 0, &wanted, &obtained, 0);
    if (!p.device) p.fail("open audio device");
    if (obtained.freq != wanted.freq || obtained.format != wanted.format ||
        obtained.channels != wanted.channels)
        throw std::runtime_error("SDL2: application audio format changed unexpectedly");
}
LinuxAudio::~LinuxAudio() = default;
AudioFormat LinuxAudio::format() const { return impl_->format; }
std::string LinuxAudio::driver() const {
    const char* value = impl_->current_driver();
    if (!value) throw std::runtime_error("SDL2: no current audio driver");
    return value;
}
std::string LinuxAudio::runtime_version() const {
    const auto v = impl_->version;
    return std::to_string(v.major) + "." + std::to_string(v.minor) + "." + std::to_string(v.patch);
}
std::size_t LinuxAudio::queue(const std::int16_t* samples, std::size_t frames) {
    auto& p = *impl_;
    if (frames > std::numeric_limits<std::uint32_t>::max() / p.frame_bytes)
        throw std::invalid_argument("audio: input frame byte size exceeds uint32");
    if (frames && !samples) throw std::invalid_argument("audio: null PCM input");
    const auto available = (p.capacity_bytes - p.queued()) / p.frame_bytes;
    const auto accepted = std::min(frames, static_cast<std::size_t>(available));
    if (accepted && p.queue_audio(p.device, samples,
                                static_cast<std::uint32_t>(accepted * p.frame_bytes)) != 0)
        p.fail("queue PCM");
    return accepted;
}
std::size_t LinuxAudio::queued_frames() const { return impl_->queued() / impl_->frame_bytes; }
void LinuxAudio::pause() { impl_->pause_audio(impl_->device, 1); }
void LinuxAudio::start() { impl_->pause_audio(impl_->device, 0); }
void LinuxAudio::clear() { impl_->clear_audio(impl_->device); }

} // namespace odh_host
