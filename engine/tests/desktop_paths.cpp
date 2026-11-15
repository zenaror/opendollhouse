/* SPDX-License-Identifier: MPL-2.0 */
#include "../platform/desktop/DesktopPaths.h"
#include <fstream>
#include <iostream>
#include <system_error>

namespace fs = std::filesystem;
static void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
template<class F> static void rejects(F f) {
    bool failed = false;
    try { f(); } catch (const std::runtime_error&) { failed = true; }
    require(failed, "invalid layout was accepted");
}
int main(int argc, char** argv) {
    try {
        require(argc == 2, "fixture output directory required");
        const auto root = fs::absolute(argv[1]);
        // Reject existing paths so cleanup owns only this new fixture.
        require(!fs::exists(root), "fixture output already exists");
        const auto game = root / "The Sims install";
        const auto base = game / "odl-launcher/bin";
        fs::create_directories(base);
        rejects([&] { odh_host::desktop_installed_data_root(base); });
        for (const char* name : {"GameData/Global/Global.far", "GameData/Objects/Objects.far", "UIGraphics/UIGraphics.far"}) {
            const auto file = game / name; fs::create_directories(file.parent_path());
            std::ofstream(file) << "fixture";
        }
        const auto cwd = fs::current_path();
        fs::current_path(root);
        require(odh_host::desktop_installed_data_root(base) == fs::canonical(game), "bin layout detection failed");
        require(odh_host::desktop_installed_data_root(game / "odl-launcher") == fs::canonical(game), "flat layout detection failed");
        fs::current_path(cwd);
        rejects([&] { odh_host::desktop_installed_data_root(root); });
        rejects([&] { odh_host::desktop_installed_data_root("relative/odl-launcher/bin"); });
        odh_host::DesktopUserEnvironment env;
        env.home = (root / "home").u8string();
        fs::path saves, cache;
        odh_host::desktop_writable_defaults(saves, cache, {}, env, true);
        require(saves == root / "home/.local/share/opendollhouse", "existing Linux saves moved");
        require(cache == root / "home/.cache/opendollhouse", "existing Linux cache moved");
        env.saves = (root / "environment-saves").u8string();
        env.cache = (root / "environment-cache").u8string();
        saves = root / "explicit-saves"; cache.clear();
        odh_host::desktop_writable_defaults(saves, cache, {}, env, false);
        require(saves == root / "explicit-saves" && cache == root / "environment-cache", "override precedence changed");
        env = {}; saves.clear(); cache.clear();
        odh_host::desktop_writable_defaults(saves, cache, root / "user-preferences", env, false);
        require(saves == root / "user-preferences/saves" && cache == root / "user-preferences/cache", "non-Linux storage failed");
        env.xdgData = "relative"; saves.clear(); cache.clear();
        rejects([&] { odh_host::desktop_writable_defaults(saves, cache, {}, env, true); });
        require(!fs::exists(game / "saves") && !fs::exists(game / "cache") && !fs::exists(game / "UserData"), "resolver wrote original game tree");
        fs::remove_all(root);
        std::cout << "desktop paths contract: PASS\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n'; return 1;
    }
}
