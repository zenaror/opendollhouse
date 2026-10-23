#include "odh/session_archive.hpp"

#include <cstdlib>
#include <initializer_list>
#include <iostream>

namespace {
void require(bool value, const char* message) {
    if (!value) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}
template<class F>
void rejects(F&& operation, const char* message) {
    try {
        operation();
    } catch (const odh::FormatError&) {
        return;
    }
    require(false, message);
}
odh::SessionArchiveSection section(const char (&id)[5], bool required,
                                   std::initializer_list<std::uint8_t> bytes) {
    return {{{id[0], id[1], id[2], id[3]}}, required, bytes};
}
}

int main() {
    odh::SessionArchive original;
    original.sections.push_back(section("META", true, {1, 2, 3, 4}));
    original.sections.push_back(section("NOTE", false, {}));
    const auto encoded = odh::encode_session_archive(original);
    require(encoded.size() == 72 && encoded[24] == 0xe1 && encoded[25] == 0x12 &&
            encoded[26] == 0xeb && encoded[27] == 0x1a,
            "header encoding matches the independent CRC-32 fixture");
    require(encoded[44] == 0xcd && encoded[45] == 0xfb &&
            encoded[46] == 0x3c && encoded[47] == 0xb6,
            "section encoding matches the independent CRC-32 fixture");
    const auto decoded = odh::decode_session_archive(encoded);
    require(decoded.version == 1 && decoded.sections.size() == 2, "round-trip version and count");
    require(decoded.sections[0].id == original.sections[0].id &&
            decoded.sections[0].required && decoded.sections[0].data == original.sections[0].data,
            "required section round-trips");
    require(decoded.sections[1].id == original.sections[1].id &&
            !decoded.sections[1].required && decoded.sections[1].data.empty(),
            "optional empty section round-trips");
    require(odh::encode_session_archive(decoded) == encoded, "encoding is deterministic");

    for (std::size_t size = 0; size < encoded.size(); ++size) {
        std::vector<std::uint8_t> truncated(encoded.begin(),
            encoded.begin() + static_cast<std::ptrdiff_t>(size));
        rejects([&] { (void)odh::decode_session_archive(truncated); },
                "every truncated prefix is rejected");
    }

    auto damaged_header = encoded;
    damaged_header[24] ^= 1;
    rejects([&] { (void)odh::decode_session_archive(damaged_header); },
            "header checksum detects changes");
    auto unsupported_version = encoded;
    unsupported_version[8] = 2;
    rejects([&] { (void)odh::decode_session_archive(unsupported_version); },
            "unsupported version is rejected");
    auto damaged_section = encoded;
    damaged_section[48] ^= 1;
    rejects([&] { (void)odh::decode_session_archive(damaged_section); },
            "section checksum detects changes");
    auto unsupported_section_flags = encoded;
    unsupported_section_flags[32] = 2;
    rejects([&] { (void)odh::decode_session_archive(unsupported_section_flags); },
            "unknown section flags are rejected");
    auto invalid_magic = encoded;
    invalid_magic[0] ^= 1;
    rejects([&] { (void)odh::decode_session_archive(invalid_magic); },
            "invalid signature is rejected");
    auto trailing = encoded;
    trailing.push_back(0);
    rejects([&] { (void)odh::decode_session_archive(trailing); },
            "unclaimed trailing data is rejected");

    auto bad_version = original;
    bad_version.version = 2;
    rejects([&] { (void)odh::encode_session_archive(bad_version); },
            "encoder rejects unsupported versions");
    auto duplicate = original;
    duplicate.sections[1].id = duplicate.sections[0].id;
    rejects([&] { (void)odh::encode_session_archive(duplicate); },
            "duplicate section IDs are rejected");
    auto invalid_id = original;
    invalid_id.sections[0].id[0] = 'm';
    rejects([&] { (void)odh::encode_session_archive(invalid_id); },
            "non-canonical section IDs are rejected");
    auto oversized = original;
    const odh::SessionArchiveLimits small_section{128, 2, 4};
    rejects([&] { (void)odh::encode_session_archive(oversized, small_section); },
            "section size limit is enforced");
    rejects([&] { (void)odh::decode_session_archive(encoded, {encoded.size() - 1, 64, 4}); },
            "file size limit is enforced");
    rejects([&] { (void)odh::decode_session_archive(encoded, {128, 64, 1}); },
            "section count limit is enforced");

    std::cout << "session archive checks passed\n";
}
