/* SPDX-License-Identifier: MPL-2.0 */
#pragma once
#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <string>
#include "../../sims3ds/src/core/DevicePath.h"
#include <sys/stat.h>

namespace odh::console {
using odh::paths::DevicePath;
inline void EnsureDeviceDirectory(const std::string& path) {
    const auto separator = path.find(":/");
    if (separator == std::string::npos) throw std::runtime_error("Missing device root");
    size_t end = separator + 2;
    for (;;) {
        const std::string directory = path.substr(0, end);
        struct stat state{};
        if (::stat(directory.c_str(), &state) != 0) {
            if (errno != ENOENT || ::mkdir(directory.c_str(), 0777) != 0) {
                // Another operation may have created it after stat.
                if (::stat(directory.c_str(), &state) != 0 || !S_ISDIR(state.st_mode))
                    throw std::runtime_error("Cannot create directory: " + directory + ": " + std::strerror(errno));
            }
        } else if (!S_ISDIR(state.st_mode)) {
            throw std::runtime_error("Not a directory: " + directory);
        }
        if (end == path.size()) break;
        end = path.find('/', end + (path[end] == '/' ? 1 : 0));
        if (end == std::string::npos) end = path.size();
    }
}
}
