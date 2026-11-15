/* SPDX-License-Identifier: MPL-2.0 */
#pragma once
#include <filesystem>
#include <stdexcept>
#include <string>

namespace odh_host {
namespace desktop_fs = std::filesystem;

// Only the named installation layout is inferred. Never search the working
// directory, pick an unrelated installation, or use original UserData for writes.
inline desktop_fs::path desktop_installed_data_root(const desktop_fs::path& executableDirectory) {
    if (executableDirectory.empty() || !executableDirectory.is_absolute())
        throw std::runtime_error("Cannot locate the application directory; use --data-root DIR.");
    auto current = desktop_fs::weakly_canonical(executableDirectory);
    for (;;) {
        if (current.filename() == "odl-launcher") {
            const auto data = current.parent_path();
            if (desktop_fs::is_directory(data / "GameData") &&
                desktop_fs::is_regular_file(data / "GameData/Global/Global.far") &&
                desktop_fs::is_regular_file(data / "GameData/Objects/Objects.far") &&
                desktop_fs::is_regular_file(data / "UIGraphics/UIGraphics.far")) return data;
            break;
        }
        const auto parent = current.parent_path();
        if (parent == current || parent.empty()) break;
        current = parent;
    }
    throw std::runtime_error("Game data was not found. Put the complete odl-launcher folder inside "
        "your The Sims installation, beside GameData and UIGraphics, or use --data-root DIR. "
        "The launcher does not include or download game data.");
}

struct DesktopUserEnvironment {
    std::string saves, cache, xdgData, xdgCache, home;
};

inline void desktop_writable_defaults(desktop_fs::path& saves, desktop_fs::path& cache,
        const desktop_fs::path& preferenceDirectory, const DesktopUserEnvironment& env, bool linuxHost) {
    if (saves.empty() && !env.saves.empty()) saves = desktop_fs::u8path(env.saves);
    if (cache.empty() && !env.cache.empty()) cache = desktop_fs::u8path(env.cache);
    if (linuxHost) {
        // Preserve the existing Linux launcher's profile and cache locations.
        const auto home = desktop_fs::u8path(env.home);
        if (saves.empty()) {
            const auto base = env.xdgData.empty() ? home / ".local/share" : desktop_fs::u8path(env.xdgData);
            if (!base.is_absolute()) throw std::runtime_error("No absolute user data directory; use --save-root DIR.");
            saves = base / "opendollhouse";
        }
        if (cache.empty()) {
            const auto base = env.xdgCache.empty() ? home / ".cache" : desktop_fs::u8path(env.xdgCache);
            if (!base.is_absolute()) throw std::runtime_error("No absolute user cache directory; use --cache-dir DIR.");
            cache = base / "opendollhouse";
        }
    } else if (saves.empty() || cache.empty()) {
        if (preferenceDirectory.empty() || !preferenceDirectory.is_absolute())
            throw std::runtime_error("Cannot locate user storage; use --save-root DIR and --cache-dir DIR.");
        if (saves.empty()) saves = preferenceDirectory / "saves";
        if (cache.empty()) cache = preferenceDirectory / "cache";
    }
}
} // namespace odh_host
