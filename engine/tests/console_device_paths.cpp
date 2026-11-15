/* SPDX-License-Identifier: MPL-2.0 */
#include "../platform/console/DevicePaths.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>

static void Require(bool value) { if (!value) throw std::runtime_error("Device path contract failed"); }
int main() {
    namespace fs = std::filesystem;
    using odh::console::DevicePath;
    try {
        Require(DevicePath("ux0:/data/./OpenDollhouse//game", "ux0") == "ux0:/data/OpenDollhouse/game");
        Require(DevicePath("sdmc:/switch/a/../OpenDollhouse/saves", "sdmc") == "sdmc:/switch/OpenDollhouse/saves");
        for (const auto* invalid : {"ux0:/", "ux0:/data/..", "ux0:/../escape", "relative", "/ux0:/data", "sdmc:/data", "ux0:/data\\escape", "ux0:/data/sdmc:/escape"}) {
            bool rejected = false;
            try { DevicePath(invalid, "ux0"); } catch (const std::runtime_error&) { rejected = true; }
            Require(rejected);
        }
        // Pure admission contract for device paths. Host labels below do not
        // exercise Vita/Switch filesystem services or prove hardware support.
        using odh::paths::IsPrivateDeviceNeighbourhood;
        int admissionCases = 0;
        for (const std::string mount : {"ux0", "sdmc"}) {
            const auto base = mount + ":/data/OpenDollhouse";
            const auto data = base + "/game", saves = base + "/saves", dir = saves + "/UserData";
            const auto check = [&](const std::string& i, const std::string& s,
                                   const std::string& d, bool expected) {
                Require(IsPrivateDeviceNeighbourhood(i, s, d, mount) == expected);
                ++admissionCases;
            };
            check(data, saves, dir, true);
            check(base + "/./game//", base + "/tmp/../saves", dir + "/./", true);
            check(data, data + "-copy", data + "-copy/UserData", true); // components, not raw prefixes
            check(data, saves, saves + "/nested/UserData", true);
            check(data, data, data + "/UserData", false);
            check(data, data + "/saves", data + "/saves/UserData", false);
            check(saves + "/game", saves, dir, false);
            check(data, saves, saves, false);
            check(data, saves, base, false);
            check(data, saves, saves + "-copy/UserData", false);
            check(data, saves, dir + "/../../game/UserData", false);
            check(data, base + "/saves/../game", data + "/UserData", false);
            check(data, saves, saves + "/UserData/..", false);
            check(data, saves, mount + ":/../escape", false);
            check(mount + ":/data/../../escape", saves, dir, false);
            check(data, mount + ":/data/../../escape", dir, false);
            check("", saves, dir, false);
            check(data, "", dir, false);
            check(data, saves, "", false);
            check(mount + ":/", saves, dir, false);
            check(data, mount + ":/", dir, false);
            check(data, saves, mount + ":/", false);
            check("app0:/game", saves, dir, false);
            check(data, "app0:/saves", dir, false);
            check(data, saves, "app0:/saves/UserData", false);
            check(data, saves, "/" + dir, false);
            check(data, saves, "relative/UserData", false);
            check(data, saves, dir + "\\escape", false);
            check(data, saves, dir + "/other:/escape", false);
            check(data, saves, dir + std::string(1, '\0') + "suffix", false);
            Require(!IsPrivateDeviceNeighbourhood(data, saves, dir, "app0")); ++admissionCases;
        }
        Require(!IsPrivateDeviceNeighbourhood(":/game", ":/saves", ":/saves/UserData", "")); ++admissionCases;
        Require(!IsPrivateDeviceNeighbourhood("ux0:/game", "ux0:/saves", "ux0:/saves/UserData", "ux0:/")); ++admissionCases;
        // This models the raw directory traversal in an isolated host folder;
        // it does not emulate Vita/Switch filesystem services.
        const auto original = fs::current_path();
        const auto temp = fs::temp_directory_path() / ("odh-device-paths-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::create_directories(temp / "ux0:");
        struct Cleanup {
            fs::path Original, Temp;
            ~Cleanup() { fs::current_path(Original); fs::remove_all(Temp); }
        } cleanup{original,temp};
        fs::current_path(temp);
        odh::console::EnsureDeviceDirectory("ux0:/data/OpenDollhouse/saves");
        odh::console::EnsureDeviceDirectory("ux0:/data/OpenDollhouse/saves");
        Require(fs::is_directory("ux0:/data/OpenDollhouse/saves"));
        std::ofstream("ux0:/data/collision") << "keep";
        bool rejected = false;
        try { odh::console::EnsureDeviceDirectory("ux0:/data/collision/child"); }
        catch (const std::runtime_error&) { rejected = true; }
        Require(rejected && fs::is_regular_file("ux0:/data/collision"));
        std::cout << "device root normalization, traversal and collision contract passed; "
                  << admissionCases << " private admission cases passed (host labels only)\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
