#pragma once

#include "odh/formats.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace odh {

struct SessionArchiveSection {
    std::array<char,4> id{};
    bool required = true;
    std::vector<std::uint8_t> data;
};

struct SessionArchive {
    std::uint16_t version = 1;
    std::vector<SessionArchiveSection> sections;
};

struct SessionArchiveLimits {
    std::size_t max_file_bytes = 128u * 1024u * 1024u;
    std::size_t max_section_bytes = 64u * 1024u * 1024u;
    std::size_t max_sections = 128;
};

// The archive stores explicit little-endian fields, never C++ object layouts.
// Required-section meaning is interpreted by the higher-level session loader.
std::vector<std::uint8_t> encode_session_archive(
    const SessionArchive& archive, SessionArchiveLimits limits = {});
SessionArchive decode_session_archive(
    const std::vector<std::uint8_t>& bytes, SessionArchiveLimits limits = {});

} // namespace odh
