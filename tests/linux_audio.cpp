#include "linux_audio.hpp"

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {
int checks = 0;
void require(bool value, const char* why) {
    ++checks;
    if (!value) throw std::runtime_error(why);
}
template<class F> void rejects(F action) {
    bool rejected = false;
    try { action(); } catch (const std::invalid_argument&) { rejected = true; }
    require(rejected, "invalid audio input accepted");
}
void exercise(odh_host::LinuxAudioRuntime& runtime,odh_host::AudioFormat format) {
    using odh_host::LinuxAudio;
    const std::size_t frames = 800;
    const auto bytes = frames * format.channels * sizeof(std::int16_t);
    std::vector<std::int16_t> pcm((frames + 10) * format.channels, 512);
    LinuxAudio output(runtime,format, {bytes});
    require(output.driver() == "dummy", "test must never open physical audio");
    require(output.format().sample_rate == format.sample_rate &&
            output.format().channels == format.channels, "application format changed");
    require(output.runtime_version().rfind("2.", 0) == 0, "runtime is not SDL2");
    require(output.queue(nullptr, 0) == 0, "empty input accepted as data");
    rejects([&] { output.queue(nullptr, 1); });
    rejects([&] { output.queue(pcm.data(), std::numeric_limits<std::size_t>::max()); });
    require(output.queue(pcm.data(), 300) == 300, "first prefix rejected");
    require(output.queue(pcm.data(), frames) == 500, "partial queue did not honor capacity");
    require(output.queued_frames() == frames, "paused queue size not exact");
    require(output.queue(pcm.data(), 1) == 0, "full queue accepted frames");
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    require(output.queued_frames() == frames, "new device did not start paused");
    output.clear();
    require(output.queued_frames() == 0, "clear left queued frames");
    require(output.queue(pcm.data(), frames + 10) == frames, "bounded oversized prefix failed");
    output.start();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (output.queued_frames() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    require(output.queued_frames() == 0, "dummy queue did not drain within 2 seconds");
    output.pause();
    require(output.queue(pcm.data(), 100) == 100, "queue after pause failed");
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    require(output.queued_frames() == 100, "pause did not preserve queued frames");
    // Destructor must safely close while there is still data in the queue.
}
void shared_subsystem(odh_host::LinuxAudioRuntime& runtime) {
    // An existing SDL audio owner must survive this backend's destruction.
    void* library = dlopen("libSDL2-2.0.so.0", RTLD_NOW | RTLD_LOCAL);
    require(library != nullptr, "SDL runtime missing for ownership check");
    struct LibraryGuard {
        void* value;
        ~LibraryGuard() { dlclose(value); }
    } guard{library};
    int (*init)(std::uint32_t) = nullptr;
    void (*quit)(std::uint32_t) = nullptr;
    std::uint32_t (*was_init)(std::uint32_t) = nullptr;
    auto bind = [&](auto& destination, const char* name) {
        void* address = dlsym(library, name);
        require(address != nullptr, "ownership check symbol missing");
        static_assert(sizeof(destination) == sizeof(address), "Linux ABI pointer size");
        std::memcpy(&destination, &address, sizeof(address));
    };
    bind(init, "SDL_InitSubSystem");
    bind(quit, "SDL_QuitSubSystem");
    bind(was_init, "SDL_WasInit");
    constexpr std::uint32_t audio = 0x10;
    require(was_init(audio) == 0, "backend left subsystem initialized");
    require(init(audio) == 0, "external dummy audio initialization failed");
    struct SubsystemGuard {
        void (*quit)(std::uint32_t);
        ~SubsystemGuard() { quit(0x10); }
    } external{quit};
    { odh_host::LinuxAudio output(runtime,{44100, 1}); }
    require(was_init(audio) == audio, "backend destroyed another owner's subsystem");
}
} // namespace

int main() { try {
    const char* driver = std::getenv("SDL_AUDIODRIVER");
    require(driver && std::string(driver) == "dummy",
            "refusing test: set SDL_AUDIODRIVER=dummy explicitly");
    using odh_host::LinuxAudio;
    odh_host::LinuxAudioRuntime runtime(odh_host::LinuxAudioRuntime::Ownership::application);
    rejects([&] { LinuxAudio output(runtime,{7999, 1}); });
    rejects([&] { LinuxAudio output(runtime,{192001, 2}); });
    rejects([&] { LinuxAudio output(runtime,{44100, 0}); });
    rejects([&] { LinuxAudio output(runtime,{44100, 3}); });
    rejects([&] { LinuxAudio output(runtime,{44100, 1}, {0}); });
    rejects([&] { LinuxAudio output(runtime,{44100, 2}, {6}); });
    rejects([&] { LinuxAudio output(runtime,{44100, 1}, {std::uint64_t{1} << 32}); });
    exercise(runtime,{8000, 1});
    exercise(runtime,{44100, 2});
    exercise(runtime,{192000, 2}); // Reopening also checks subsystem/device cleanup.
    shared_subsystem(runtime);
    {
        LinuxAudio output(runtime,{44100, 2});
        std::vector<std::int16_t> pcm(2 * (65536 + 1), 0);
        require(output.queue(pcm.data(), 65537) == 65536,
                "default 256 KiB budget not honored");
        require(output.queued_frames() == 65536, "default queued frame count wrong");
    }
    std::cout << checks << " Linux audio checks passed (SDL dummy only)\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
} }
