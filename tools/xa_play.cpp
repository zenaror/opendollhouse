#include "odh/xa_audio.hpp"
#include "linux_audio.hpp"
#include "json.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>

int main(int argc, char** argv) { try {
    if (argc == 2 && std::string(argv[1]) == "--help") {
        std::cout << "Usage: odh-xa-play INPUT_XA [--play]\n"
                     "Without --play, decode and report only. With --play, open Linux audio.\n"
                     "SDL_AUDIODRIVER=dummy selects silent validation.\n";
        return 0;
    }
    if (argc < 2 || argc > 3 || (argc == 3 && std::string(argv[2]) != "--play"))
        throw odh::FormatError("usage: odh-xa-play INPUT_XA [--play]");
    const std::filesystem::path path = argv[1];
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw odh::FormatError("cannot open XA input");
    // Validate and decode before opening output; retain the decoder's budgets.
    const auto audio = odh::read_xa(odh::View(stream, std::filesystem::file_size(path)));
    const auto frames = audio.samples.size() / audio.channels;
    std::size_t delivered = 0;
    std::string driver;
    if (argc == 3) {
        odh_host::LinuxAudio output({audio.sample_rate, audio.channels});
        driver = output.driver();
        // Finite source duration plus allowance for driver scheduling. This
        // blocking utility is not the game's scheduler or soundthread.
        const auto deadline = std::chrono::steady_clock::now() +
            std::chrono::milliseconds((frames * 1000 + audio.sample_rate - 1) / audio.sample_rate) +
            std::chrono::seconds(2);
        output.start();
        while (delivered < frames || output.queued_frames()) {
            if (std::chrono::steady_clock::now() >= deadline)
                throw std::runtime_error("XA playback queue timeout");
            if (delivered < frames)
                delivered += output.queue(audio.samples.data() + delivered * audio.channels,
                                          frames - delivered);
            if (delivered < frames || output.queued_frames())
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }
    std::cout << "{\"evidence\":\"XA_PCM_Linux_probe\",\"sample_rate\":" << audio.sample_rate
              << ",\"channels\":" << audio.channels << ",\"frames\":" << frames
              << ",\"pcm_bytes\":" << audio.samples.size() * sizeof(std::int16_t)
              << ",\"remaining_bytes\":" << audio.remaining_bytes
              << ",\"delivered_frames\":" << delivered << ",\"driver\":";
    std::cout << json_bytes(driver);
    std::cout << ",\"audibility_verified\":false}\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "odh-xa-play: " << error.what() << '\n';
    return 2;
} }
