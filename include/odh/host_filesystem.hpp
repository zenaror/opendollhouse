/* SPDX-License-Identifier: MPL-2.0 */
#pragma once
#include "odh/device_path.hpp"
#include <cerrno>
#include <filesystem>
#include <system_error>
#if defined(__SWITCH__) || defined(__vita__)
#include <sys/stat.h>
#endif

namespace odh::hostfs {
namespace fs = std::filesystem;

// PC callers retain physical canonicalization and symlink checks. The selected
// console mounts expose files/directories without symlinks, but their names
// are not POSIX absolute paths. Normalize those names without consulting cwd.
#if defined(__SWITCH__) || defined(__vita__)
inline const char* device_mount() {
#ifdef __SWITCH__
    return "sdmc";
#else
    return "ux0";
#endif
}
inline fs::path absolute(const fs::path& input, std::error_code& error) {
    try {
        auto result=fs::path(odh::paths::DevicePath(input.generic_string(),device_mount()));
        error.clear();return result;
    } catch(const std::runtime_error&) {
        error=std::make_error_code(std::errc::invalid_argument);return {};
    }
}
#else
inline fs::path absolute(const fs::path& input, std::error_code& error) {
    return fs::absolute(input,error);
}
#endif
inline fs::path absolute(const fs::path& input) {
#if defined(__SWITCH__) || defined(__vita__)
    std::error_code error;auto result=odh::hostfs::absolute(input,error);
    if(error)throw fs::filesystem_error("cannot resolve absolute resource path",input,error);
    return result;
#else
    return fs::absolute(input);
#endif
}
inline bool is_absolute(const fs::path& input) {
#if defined(__SWITCH__) || defined(__vita__)
    const auto text=input.generic_string();const auto prefix=std::string(device_mount())+":/";
    return text.compare(0,prefix.size(),prefix)==0;
#else
    return input.is_absolute();
#endif
}
inline fs::file_status symlink_status(const fs::path& input,std::error_code& error) {
#if defined(__SWITCH__) || defined(__vita__)
    const auto path=odh::hostfs::absolute(input,error);
    if(error)return fs::file_status(fs::file_type::none);
    struct stat info{};
    if(::stat(path.string().c_str(),&info)!=0) {
        const auto failure=errno;error=std::error_code(failure,std::generic_category());
        return fs::file_status(failure==ENOENT||failure==ENOTDIR?fs::file_type::not_found:fs::file_type::none);
    }
    error.clear();return fs::file_status(S_ISDIR(info.st_mode)?fs::file_type::directory:
        S_ISREG(info.st_mode)?fs::file_type::regular:fs::file_type::unknown);
#else
    return fs::symlink_status(input,error);
#endif
}
inline fs::file_status symlink_status(const fs::path& input) {
#if defined(__SWITCH__) || defined(__vita__)
    std::error_code error;const auto result=odh::hostfs::symlink_status(input,error);
    if(error&&result.type()!=fs::file_type::not_found)
        throw fs::filesystem_error("cannot inspect native resource",input,error);
    return result;
#else
    return fs::symlink_status(input);
#endif
}
inline fs::path canonical(const fs::path& input,std::error_code& error) {
#if defined(__SWITCH__) || defined(__vita__)
    const auto result=odh::hostfs::absolute(input,error);if(error)return {};
    const auto status=odh::hostfs::symlink_status(result,error);
    if(error||!fs::exists(status))return {};
    return result;
#else
    return fs::canonical(input,error);
#endif
}
inline fs::path canonical(const fs::path& input) {
#if defined(__SWITCH__) || defined(__vita__)
    std::error_code error;auto result=odh::hostfs::canonical(input,error);
    if(error)throw fs::filesystem_error("cannot resolve native resource",input,error);
    return result;
#else
    return fs::canonical(input);
#endif
}
inline fs::path weakly_canonical(const fs::path& input,std::error_code& error) {
#if defined(__SWITCH__) || defined(__vita__)
    // Missing private save/cache descendants are permitted here. The same
    // device parser rejects wrong mounts, backslashes, NUL and root traversal.
    return odh::hostfs::absolute(input,error);
#else
    return fs::weakly_canonical(input,error);
#endif
}
inline fs::path weakly_canonical(const fs::path& input) {
#if defined(__SWITCH__) || defined(__vita__)
    return odh::hostfs::absolute(input);
#else
    return fs::weakly_canonical(input);
#endif
}
} // namespace odh::hostfs
