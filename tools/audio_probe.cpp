#include "linux_audio.hpp"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

int main(int argc, char** argv) { try {
    if (argc == 1 || (argc == 2 && std::string(argv[1]) == "--help")) {
        std::cout << "Usage: odh-audio-probe --play\n"
                     "Opens the default Linux SDL2 output only with --play.\n"
                     "Queues 100 ms synthetic mono PCM16 at 44100 Hz, amplitude 1024.\n"
                     "Set SDL_AUDIODRIVER=dummy for a silent backend check.\n"
                     "Queue depletion does not confirm audible playback.\n";
        return 0;
    }
    if (argc != 2 || std::string(argv[1]) != "--play")
        throw std::invalid_argument("usage: odh-audio-probe --play (or --help)");
    constexpr std::uint32_t rate = 44100;
    constexpr std::size_t frames = rate / 10;
    std::vector<std::int16_t> pcm(frames);
    // 441 Hz square wave with a 5 ms edge envelope, generated locally only.
    for (std::size_t i = 0; i < frames; ++i) {
        const auto edge = std::min(i, frames - 1 - i);
        const auto amplitude = static_cast<int>(1024 * std::min(edge, std::size_t{220}) / 220);
        pcm[i] = static_cast<std::int16_t>((i % 100 < 50) ? amplitude : -amplitude);
    }
    odh_host::LinuxAudioRuntime runtime(odh_host::LinuxAudioRuntime::Ownership::application);
    odh_host::LinuxAudio output(runtime,{rate, 1});
    std::cout << "SDL " << output.runtime_version() << ", driver " << output.driver() << '\n';
    const auto accepted = output.queue(pcm.data(), frames);
    if (accepted != frames) throw std::runtime_error("audio probe: incomplete PCM queue");
    output.start();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (output.queued_frames() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    if (output.queued_frames()) throw std::runtime_error("audio probe: queue timeout after 2 seconds");
    std::cout << accepted << " synthetic frames delivered to backend; audibility unverified\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
} }
