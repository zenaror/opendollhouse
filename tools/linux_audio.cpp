#include "linux_audio.hpp"

#include <algorithm>
#include <cstring>
#include <dlfcn.h>
#include <limits>
#include <mutex>
#include <thread>
#include <utility>
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

namespace {
struct RuntimeRegistry {
    std::mutex mutex;
    std::weak_ptr<void> state; // Lookup only, never keeps SDL or a device alive.
    const void* live=nullptr;  // Also excludes acquisition during finalization.
    // Admission metadata stays available even after weak expiry, until SDL
    // cleanup finishes. Foreign callers must not acquire a strong old State.
    std::thread::id owner_thread;
    LinuxAudioRuntime::Ownership ownership=LinuxAudioRuntime::Ownership::external;
};
RuntimeRegistry& runtime_registry(){static RuntimeRegistry registry;return registry;}
struct SdlApi {
    void* library=nullptr;
    bool global_owner=false;
    Version version{};
    int (*init)(std::uint32_t)=nullptr;
    void (*quit)(std::uint32_t)=nullptr;
    void (*quit_all)()=nullptr;
    const char* (*error)()=nullptr;
    const char* (*current_driver)()=nullptr;
    void (*get_version)(Version*)=nullptr;
    std::uint32_t (*open)(const char*,int,const AudioSpec*,AudioSpec*,int)=nullptr;
    void (*close)(std::uint32_t)=nullptr;
    int (*queue_audio)(std::uint32_t,const void*,std::uint32_t)=nullptr;
    std::uint32_t (*queued_bytes)(std::uint32_t)=nullptr;
    void (*pause_audio)(std::uint32_t,int)=nullptr;
    void (*clear_audio)(std::uint32_t)=nullptr;
    ~SdlApi(){
        // A subsystem lease never calls global Quit. Only the explicit host
        // owner's final shared state reaches here, after all devices close.
        if(global_owner&&quit_all)quit_all();
        if(library)dlclose(library);
    }
};
}
struct LinuxAudioRuntime::Impl {
    Ownership ownership;
    std::thread::id owner_thread=std::this_thread::get_id();
    std::unique_ptr<SdlApi> api;
    bool ready=false;
    std::string load_error;
    explicit Impl(Ownership value):ownership(value){}
    void require_thread()const{
        if(owner_thread!=std::this_thread::get_id())
            throw std::runtime_error("audio: SDL runtime belongs to another host thread");
    }
    void load(){
        require_thread();
        if(ready)return;
        if(api)throw std::runtime_error(load_error);
        // Keep even a partial binding until the last runtime lease. A failed
        // symbol/version load must not globally quit another active consumer.
        api=std::make_unique<SdlApi>();auto* next=api.get();
        try {
        next->global_owner=ownership==Ownership::application;
        next->library=dlopen("libSDL2-2.0.so.0",RTLD_NOW|RTLD_LOCAL);
        if(!next->library){const char* detail=dlerror();throw std::runtime_error(std::string("SDL2 load: ")+(detail?detail:"unknown error"));}
        // Resolve global cleanup before making any SDL call, including version
        // queries, so a later constructor error cannot strand owned SDL state.
        next->quit_all=symbol<decltype(next->quit_all)>(next->library,"SDL_Quit");
        next->init=symbol<decltype(next->init)>(next->library,"SDL_InitSubSystem");
        next->quit=symbol<decltype(next->quit)>(next->library,"SDL_QuitSubSystem");
        next->error=symbol<decltype(next->error)>(next->library,"SDL_GetError");
        next->current_driver=symbol<decltype(next->current_driver)>(next->library,"SDL_GetCurrentAudioDriver");
        next->get_version=symbol<decltype(next->get_version)>(next->library,"SDL_GetVersion");
        next->open=symbol<decltype(next->open)>(next->library,"SDL_OpenAudioDevice");
        next->close=symbol<decltype(next->close)>(next->library,"SDL_CloseAudioDevice");
        next->queue_audio=symbol<decltype(next->queue_audio)>(next->library,"SDL_QueueAudio");
        next->queued_bytes=symbol<decltype(next->queued_bytes)>(next->library,"SDL_GetQueuedAudioSize");
        next->pause_audio=symbol<decltype(next->pause_audio)>(next->library,"SDL_PauseAudioDevice");
        next->clear_audio=symbol<decltype(next->clear_audio)>(next->library,"SDL_ClearQueuedAudio");
        next->get_version(&next->version);
        if(next->version.major!=2||(next->version.minor==0&&next->version.patch<4))
            throw std::runtime_error("audio: SDL2 2.0.4 or newer required for queue audio");
        ready=true;
        }catch(const std::exception& error){load_error=error.what();throw;}
    }
    ~Impl(){
        // The weak state is expired, but registry.live excludes a new owner
        // until cleanup finishes. Do not hold our mutex across foreign calls.
        api.reset();
        auto& registry=runtime_registry();std::lock_guard<std::mutex> lock(registry.mutex);
        if(registry.live==this){registry.live=nullptr;registry.owner_thread={};}
    }
};
LinuxAudioRuntime::LinuxAudioRuntime(Ownership ownership){
    if(ownership!=Ownership::application&&ownership!=Ownership::external)
        throw std::invalid_argument("audio: invalid SDL runtime ownership policy");
    auto& registry=runtime_registry();
    std::shared_ptr<Impl> acquired; // Released outside the registry mutex, also on exceptions.
    {
        std::lock_guard<std::mutex> lock(registry.mutex);
        if(registry.live){
            // Check admission before weak.lock: even a refused temporary strong
            // reference could otherwise become the last owner on a foreign thread.
            if(registry.owner_thread!=std::this_thread::get_id())
                throw std::runtime_error("audio: SDL runtime belongs to another host thread");
            if(registry.ownership!=ownership)
                throw std::runtime_error("audio: live SDL runtime ownership policy differs");
            acquired=std::static_pointer_cast<Impl>(registry.state.lock());
            if(!acquired)throw std::runtime_error("audio: SDL runtime finalization is in progress");
        }else{
            acquired=std::make_shared<Impl>(ownership);
            registry.owner_thread=std::this_thread::get_id();registry.ownership=ownership;
            registry.live=acquired.get();registry.state=acquired;
        }
    }
    impl_=std::move(acquired);
}
LinuxAudioRuntime::~LinuxAudioRuntime()=default;
struct LinuxAudio::Impl {
    std::shared_ptr<LinuxAudioRuntime::Impl> runtime;
    bool initialized=false;
    std::uint32_t device=0;
    AudioFormat format;
    std::uint32_t capacity_bytes=0,frame_bytes=0;
    ~Impl(){
        if(device)runtime->api->close(device);
        if(initialized)runtime->api->quit(init_audio); // Exactly our successful Init.
        // runtime is released last; a failed Open balances Init before cleanup.
    }
    [[noreturn]] void fail(const char* operation) const {
        const char* detail = runtime->api->error();
        throw std::runtime_error(std::string("SDL2 ") + operation + ": " +
                                 (detail && *detail ? detail : "unknown error"));
    }
    std::uint32_t queued() const {
        runtime->require_thread();
        const auto bytes = runtime->api->queued_bytes(device);
        if (bytes % frame_bytes || bytes > capacity_bytes)
            throw std::runtime_error("SDL2: queued bytes violate frame/capacity contract");
        return bytes;
    }
};

LinuxAudio::LinuxAudio(LinuxAudioRuntime& runtime, AudioFormat format, AudioLimits limits)
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
    p.runtime=runtime.impl_;p.runtime->load();
    if(p.runtime->api->init(init_audio)!=0)p.fail("initialize audio");
    p.initialized=true;
    AudioSpec wanted{};
    wanted.freq = static_cast<int>(format.sample_rate);
    wanted.format = native_s16();
    wanted.channels = static_cast<std::uint8_t>(format.channels);
    wanted.samples = 512;
    AudioSpec obtained{};
    // No permitted changes: SDL must accept this application-facing format.
    // SDL may internally convert for the physical device; we don't claim that
    // the DAC operates at the source rate or source sample format.
    p.device = p.runtime->api->open(nullptr, 0, &wanted, &obtained, 0);
    if (!p.device) p.fail("open audio device");
    if (obtained.freq != wanted.freq || obtained.format != wanted.format ||
        obtained.channels != wanted.channels)
        throw std::runtime_error("SDL2: application audio format changed unexpectedly");
}
LinuxAudio::~LinuxAudio() = default;
AudioFormat LinuxAudio::format() const { return impl_->format; }
std::string LinuxAudio::driver() const {
    impl_->runtime->require_thread();
    const char* value = impl_->runtime->api->current_driver();
    if (!value) throw std::runtime_error("SDL2: no current audio driver");
    return value;
}
std::string LinuxAudio::runtime_version() const {
    const auto v = impl_->runtime->api->version;
    return std::to_string(v.major) + "." + std::to_string(v.minor) + "." + std::to_string(v.patch);
}
std::size_t LinuxAudio::queue(const std::int16_t* samples, std::size_t frames) {
    auto& p = *impl_;
    if (frames > std::numeric_limits<std::uint32_t>::max() / p.frame_bytes)
        throw std::invalid_argument("audio: input frame byte size exceeds uint32");
    if (frames && !samples) throw std::invalid_argument("audio: null PCM input");
    const auto available = (p.capacity_bytes - p.queued()) / p.frame_bytes;
    const auto accepted = std::min(frames, static_cast<std::size_t>(available));
    if (accepted && p.runtime->api->queue_audio(p.device, samples,
                                static_cast<std::uint32_t>(accepted * p.frame_bytes)) != 0)
        p.fail("queue PCM");
    return accepted;
}
std::size_t LinuxAudio::queued_frames() const { return impl_->queued() / impl_->frame_bytes; }
void LinuxAudio::pause() { impl_->runtime->require_thread();impl_->runtime->api->pause_audio(impl_->device, 1); }
void LinuxAudio::start() { impl_->runtime->require_thread();impl_->runtime->api->pause_audio(impl_->device, 0); }
void LinuxAudio::clear() { impl_->runtime->require_thread();impl_->runtime->api->clear_audio(impl_->device); }

} // namespace odh_host
