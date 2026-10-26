#pragma once

#include "odh/session_archive.hpp"

#include <filesystem>

namespace odh_host {

// Publish immutable, private SHA-256-named bytes in an existing directory.
// Identical existing bytes are reused; conflicts and symbolic links reject.
// A failure after publication retains the archive for recovery.
std::filesystem::path publish_session_archive_immutable(
    const std::filesystem::path& directory, const odh::SessionArchive& archive,
    odh::SessionArchiveLimits limits = {});

// Linux-only filesystem boundary for the portable session archive codec.
// Saves are created privately and replaced by a same-directory atomic rename.
void write_session_archive_atomic(
    const std::filesystem::path& path, const odh::SessionArchive& archive,
    odh::SessionArchiveLimits limits = {});
odh::SessionArchive read_session_archive(
    const std::filesystem::path& path, odh::SessionArchiveLimits limits = {});

} // namespace odh_host
