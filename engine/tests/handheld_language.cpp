// SPDX-License-Identifier: MPL-2.0
#include "app/HandheldLanguage.h"
#include "odh/game_languages.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>

namespace fs = std::filesystem;

static void require(bool ok, const std::string& message) {
    if (!ok) throw std::runtime_error(message);
}

static std::string readFile(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    require(in.good(), "Could not open " + path.string());
    return {std::istreambuf_iterator<char>(in), {}};
}

static void writeFile(const fs::path& path, const std::string& bytes) {
    std::ofstream out(path, std::ios::binary);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    require(out.good(), "Could not write " + path.string());
}

int main(int argc, char** argv) {
    fs::path fixture;
    try {
        require(argc == 2, "Supply a temporary directory");
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        const fs::path candidate = fs::path(argv[1]) / ("handheld-language-" + std::to_string(stamp));
        require(fs::create_directory(candidate), "Could not create a private test directory");
        fixture = candidate;

        const auto saves = fixture / "saves";
        const auto preference = saves / "language.txt";
        const auto house = saves / "UserData" / "House09.iff";
        const auto commercial = fixture / "game" / "UserData" / "Neighborhood.iff";
        const auto options = saves / "sims_options.txt";
        const auto cache = fixture / "cache" / "cache_version.txt";
        fs::create_directories(house.parent_path());
        fs::create_directories(commercial.parent_path());
        fs::create_directory(cache.parent_path());
        writeFile(house, "existing household");
        writeFile(commercial, "original neighborhood");
        writeFile(options, "MusicVolume=40\n");
        writeFile(cache, "3 1\n");

        const auto root = saves.string();
        require(!simapp::ReadHandheldLanguage(root), "An old profile has no confirmed language");
        require(!fs::exists(preference), "Opening the chooser must not create a preference");
        for (const std::string& invalid : std::initializer_list<std::string>{
                 "", "0\n", "21", "255", "-1", "pt-BR", "14x", "1 4", "14\n2",
                 std::string("14\0x", 4), "14" + std::string(4096, ' ') + "x"}) {
            writeFile(preference, invalid);
            require(!simapp::ReadHandheldLanguage(root), "Malformed language record was accepted");
            require(readFile(preference) == invalid, "Reading changed the unconfirmed preference");
        }

        for (const auto* suffix : {".new", ".tmp", ".bak"}) {
            const auto recoveryRoot = fixture / (std::string("recovery") + suffix);
            fs::create_directory(recoveryRoot);
            const auto backup = recoveryRoot / (std::string("language.txt") + suffix);
            writeFile(backup, "14\r\n");
            require(simapp::ReadHandheldLanguage(recoveryRoot.string()) == 14,
                    "Could not recover a complete interrupted preference");
            writeFile(recoveryRoot / "language.txt", "3");
            require(simapp::ReadHandheldLanguage(recoveryRoot.string()) == 3,
                    "A stale sidecar overrode the confirmed language");
        }

        for (size_t i = 0; i < odh_host::game_languages.size(); ++i) {
            const auto selected = odh_host::parse_game_language(odh_host::game_languages[i].tag);
            require(selected && *selected == i + 1, "Picker code differs from the game's locale");
            require(simapp::WriteHandheldLanguage(root, *selected), "Could not confirm a valid language");
            require(simapp::ReadHandheldLanguage(root) == selected, "Saved language failed to reopen");
        }
        require(simapp::WriteHandheldLanguage(root, 14), "Could not reselect Brazilian Portuguese");
        const auto previous = readFile(preference);
        for (uint8_t code : {0, 21, 255}) {
            require(!simapp::WriteHandheldLanguage(root, code), "Out-of-range selection was accepted");
            require(readFile(preference) == previous, "Invalid selection overwrote a confirmed language");
        }
        const auto secondProfile = fixture / "other-saves";
        fs::create_directory(secondProfile);
        require(!simapp::ReadHandheldLanguage(secondProfile.string()), "Language leaked into another profile");
        require(simapp::ReadHandheldLanguage(root) == 14, "Existing language no longer opens");
        require(readFile(house) == "existing household" && readFile(commercial) == "original neighborhood" &&
                readFile(options) == "MusicVolume=40\n" && readFile(cache) == "3 1\n",
                "Selecting a language changed unrelated player or original data");

        fs::remove_all(fixture);
        std::cout << "Handheld language: legacy profile, malformed records, 20 language codes, reselection and isolated saves passed\n";
        return 0;
    } catch (const std::exception& error) {
        if (!fixture.empty()) { std::error_code ignored; fs::remove_all(fixture, ignored); }
        std::cerr << error.what() << '\n';
        return 1;
    }
}
