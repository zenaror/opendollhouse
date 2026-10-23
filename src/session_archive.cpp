#include "odh/session_archive.hpp"

#include <algorithm>
#include <limits>
#include <set>
#include <utility>

namespace odh {
namespace {
constexpr std::array<std::uint8_t,8> magic{{'O','D','H','S','E','S','\r','\n'}};
constexpr std::uint16_t format_version = 1;
constexpr std::size_t header_without_crc = 24;
constexpr std::size_t header_size = 28;
constexpr std::size_t section_header_size = 20;
constexpr std::uint32_t required_flag = 1;

std::uint32_t crc32(const std::uint8_t* bytes, std::size_t size) {
    std::uint32_t crc = 0xffffffffu;
    for (std::size_t i = 0; i < size; ++i) {
        crc ^= bytes[i];
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return crc ^ 0xffffffffu;
}

void append_u16(std::vector<std::uint8_t>& out, std::uint16_t value) {
    out.push_back(static_cast<std::uint8_t>(value));
    out.push_back(static_cast<std::uint8_t>(value >> 8));
}
void append_u32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    for (unsigned shift = 0; shift < 32; shift += 8)
        out.push_back(static_cast<std::uint8_t>(value >> shift));
}
void append_u64(std::vector<std::uint8_t>& out, std::uint64_t value) {
    for (unsigned shift = 0; shift < 64; shift += 8)
        out.push_back(static_cast<std::uint8_t>(value >> shift));
}
std::uint16_t read_u16(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    return static_cast<std::uint16_t>(bytes[offset]) |
        static_cast<std::uint16_t>(static_cast<std::uint16_t>(bytes[offset + 1]) << 8);
}
std::uint32_t read_u32(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    std::uint32_t value = 0;
    for (unsigned shift = 0; shift < 32; shift += 8)
        value |= static_cast<std::uint32_t>(bytes[offset++]) << shift;
    return value;
}
std::uint64_t read_u64(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    std::uint64_t value = 0;
    for (unsigned shift = 0; shift < 64; shift += 8)
        value |= static_cast<std::uint64_t>(bytes[offset++]) << shift;
    return value;
}
void require_room(const std::vector<std::uint8_t>& bytes, std::size_t offset, std::size_t count) {
    if (offset > bytes.size() || count > bytes.size() - offset)
        throw FormatError("session archive is truncated");
}
bool valid_id(const std::array<char,4>& id) {
    return std::all_of(id.begin(), id.end(), [](unsigned char c) {
        return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == ' ';
    });
}
void validate_limits(SessionArchiveLimits limits) {
    if (limits.max_file_bytes < header_size || limits.max_sections == 0 ||
        limits.max_section_bytes > limits.max_file_bytes)
        throw FormatError("invalid session archive limits");
}
}

std::vector<std::uint8_t> encode_session_archive(
        const SessionArchive& archive, SessionArchiveLimits limits) {
    validate_limits(limits);
    if (archive.version != format_version)
        throw FormatError("unsupported session archive version");
    if (archive.sections.size() > limits.max_sections ||
        archive.sections.size() > std::numeric_limits<std::uint32_t>::max())
        throw FormatError("session archive section count exceeds limit");

    std::set<std::array<char,4>> ids;
    std::size_t total = header_size;
    for (const auto& section : archive.sections) {
        if (!valid_id(section.id)) throw FormatError("invalid session archive section ID");
        if (!ids.insert(section.id).second) throw FormatError("duplicate session archive section ID");
        if (section.data.size() > limits.max_section_bytes)
            throw FormatError("session archive section exceeds limit");
        if (total > limits.max_file_bytes ||
            section_header_size > limits.max_file_bytes - total ||
            section.data.size() > limits.max_file_bytes - total - section_header_size)
            throw FormatError("session archive exceeds file size limit");
        total += section_header_size + section.data.size();
    }

    std::vector<std::uint8_t> out;
    out.reserve(total);
    out.insert(out.end(), magic.begin(), magic.end());
    append_u16(out, format_version);
    append_u16(out, 0);
    append_u32(out, static_cast<std::uint32_t>(archive.sections.size()));
    append_u64(out, static_cast<std::uint64_t>(total - header_size));
    append_u32(out, crc32(out.data(), header_without_crc));

    for (const auto& section : archive.sections) {
        for (const char c : section.id) out.push_back(static_cast<std::uint8_t>(c));
        append_u32(out, section.required ? required_flag : 0);
        append_u64(out, static_cast<std::uint64_t>(section.data.size()));
        append_u32(out, crc32(section.data.data(), section.data.size()));
        out.insert(out.end(), section.data.begin(), section.data.end());
    }
    return out;
}

SessionArchive decode_session_archive(
        const std::vector<std::uint8_t>& bytes, SessionArchiveLimits limits) {
    validate_limits(limits);
    if (bytes.size() > limits.max_file_bytes)
        throw FormatError("session archive exceeds file size limit");
    require_room(bytes, 0, header_size);
    if (!std::equal(magic.begin(), magic.end(), bytes.begin()))
        throw FormatError("invalid session archive signature");
    if (read_u16(bytes, 8) != format_version)
        throw FormatError("unsupported session archive version");
    if (read_u16(bytes, 10) != 0)
        throw FormatError("unsupported session archive header flags");
    const auto section_count = read_u32(bytes, 12);
    if (section_count > limits.max_sections)
        throw FormatError("session archive section count exceeds limit");
    const auto payload_size = read_u64(bytes, 16);
    if (payload_size != bytes.size() - header_size)
        throw FormatError("session archive size does not match its header");
    if (read_u32(bytes, 24) != crc32(bytes.data(), header_without_crc))
        throw FormatError("session archive header checksum mismatch");

    SessionArchive result;
    result.version = format_version;
    result.sections.reserve(section_count);
    std::set<std::array<char,4>> ids;
    std::size_t offset = header_size;
    for (std::uint32_t i = 0; i < section_count; ++i) {
        require_room(bytes, offset, section_header_size);
        SessionArchiveSection section;
        for (std::size_t n = 0; n < section.id.size(); ++n)
            section.id[n] = static_cast<char>(bytes[offset + n]);
        if (!valid_id(section.id)) throw FormatError("invalid session archive section ID");
        if (!ids.insert(section.id).second) throw FormatError("duplicate session archive section ID");
        const auto flags = read_u32(bytes, offset + 4);
        if ((flags & ~required_flag) != 0)
            throw FormatError("unsupported session archive section flags");
        section.required = (flags & required_flag) != 0;
        const auto data_size = read_u64(bytes, offset + 8);
        if (data_size > limits.max_section_bytes ||
            data_size > std::numeric_limits<std::size_t>::max())
            throw FormatError("session archive section exceeds limit");
        const auto size = static_cast<std::size_t>(data_size);
        offset += section_header_size;
        require_room(bytes, offset, size);
        const auto expected_crc = read_u32(bytes, offset - 4);
        if (expected_crc != crc32(bytes.data() + offset, size))
            throw FormatError("session archive section checksum mismatch");
        section.data.assign(bytes.begin() + static_cast<std::ptrdiff_t>(offset),
                            bytes.begin() + static_cast<std::ptrdiff_t>(offset + size));
        offset += size;
        result.sections.push_back(std::move(section));
    }
    if (offset != bytes.size())
        throw FormatError("session archive has trailing or unclaimed data");
    return result;
}

} // namespace odh
