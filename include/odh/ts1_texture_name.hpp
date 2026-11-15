// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <string>

namespace odh {
inline bool ts1_has_bmp_extension(const std::string& name) {
    if (name.size() < 4) return false;
    const auto i = name.size() - 4;
    return name[i] == '.' && (name[i+1] == 'b' || name[i+1] == 'B') &&
           (name[i+2] == 'm' || name[i+2] == 'M') && (name[i+3] == 'p' || name[i+3] == 'P');
}

inline std::string ts1_bmp_filename(const std::string& name) {
    return ts1_has_bmp_extension(name) ? name : name + ".bmp";
}

// FreeSO compares texture names without their skin tone and takes the last
// matching binding. Logical names and literal BMP filenames address one file.
inline bool ts1_override_matches(const std::string& mesh_texture, const std::string& override_texture) {
    auto uniform = [](std::string key) {
        if (ts1_has_bmp_extension(key)) key.resize(key.size() - 4);
        for (auto& c : key) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
        for (const auto* tone : {"lgt", "med", "drk"}) {
            for (auto at = key.find(tone); at != std::string::npos; at = key.find(tone)) key.erase(at, 3);
        }
        return key;
    };
    if (mesh_texture == "x" || mesh_texture == "X") return true;
    const auto mesh = uniform(mesh_texture), requested = uniform(override_texture);
    return mesh.size() >= requested.size() && mesh.compare(mesh.size() - requested.size(), requested.size(), requested) == 0;
}
}
