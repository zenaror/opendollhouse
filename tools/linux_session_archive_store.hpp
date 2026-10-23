#pragma once

#include "odh/session_archive.hpp"

#include <filesystem>

namespace odh_host {

// Linux-only filesystem boundary for the portable session archive codec.
// Saves are created privately and replaced by a same-directory atomic rename.
void write_session_archive_atomic(
    const std::filesystem::path& path, const odh::SessionArchive& archive,
    odh::SessionArchiveLimits limits = {});
odh::SessionArchive read_session_archive(
    const std::filesystem::path& path, odh::SessionArchiveLimits limits = {});

} // namespace odh_host
