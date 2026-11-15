/* SPDX-License-Identifier: MPL-2.0 */
#pragma once
#include <stdexcept>
#include <string>
#include <vector>

namespace odh::paths {
// libstdc++ uses POSIX path rules on these targets: "ux0:/" and "sdmc:/"
// would otherwise be relative names, prefixed with the process working folder.
inline std::string DevicePath(const std::string& input, const std::string& mount) {
    const std::string root = mount + ":/";
    if (input.compare(0, root.size(), root) != 0 || input.find('\\') != std::string::npos)
        throw std::runtime_error("Expected a device-rooted path beginning with " + root);
    std::vector<std::string> pieces;
    for (size_t start = root.size(); start < input.size();) {
        const size_t end = input.find('/', start);
        const auto piece = input.substr(start, end == std::string::npos ? end : end - start);
        if (piece == "..") {
            if (pieces.empty()) throw std::runtime_error("Path escapes the device root");
            pieces.pop_back();
        } else if (!piece.empty() && piece != ".") {
            if (piece.find(':') != std::string::npos || piece.find('\0') != std::string::npos)
                throw std::runtime_error("Invalid device path component");
            pieces.push_back(piece);
        }
        if (end == std::string::npos) break;
        start = end + 1;
    }
    if (pieces.empty()) throw std::runtime_error("Choose a directory below the device root");
    std::string output = root;
    for (const auto& piece : pieces) {
        if (output.back() != '/') output += '/';
        output += piece;
    }
    return output;
}
// Device filesystems have no directory symlinks. Admit only a private child
// of the normalized save tree, with install and saves on disjoint branches.
// All inputs must use the caller's expected device, not POSIX cwd resolution.
inline bool IsPrivateDeviceNeighbourhood(const std::string& install,
                                         const std::string& saves,
                                         const std::string& directory,
                                         const std::string& mount) {
    if (mount.empty() || mount.find_first_not_of(
            "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_") != std::string::npos)
        return false;
    try {
        const auto data = DevicePath(install, mount);
        const auto root = DevicePath(saves, mount);
        const auto target = DevicePath(directory, mount);
        const auto below = [](const std::string& parent, const std::string& child) {
            return child.size() > parent.size() &&
                child.compare(0, parent.size(), parent) == 0 && child[parent.size()] == '/';
        };
        return data != root && !below(data, root) && !below(root, data) && below(root, target);
    } catch (const std::runtime_error&) {
        return false;
    }
}
}
