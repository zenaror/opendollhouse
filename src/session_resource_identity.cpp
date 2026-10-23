#include "odh/session_resource_identity.hpp"

#include <algorithm>

namespace odh {
namespace {
constexpr std::array<std::uint8_t,8> magic{{'O','D','H','R','I','D','\r','\n'}};
constexpr std::uint16_t version = 1;
constexpr std::size_t encoded_size = 44;
}

std::vector<std::uint8_t> encode_session_resource_identity(
        const SessionResourceIdentity& identity) {
    if (identity.language_code < 1 || identity.language_code > 20)
        throw FormatError("session resource identity has an unsupported language");
    if ((identity.expansion_mask & 0x80u) != 0)
        throw FormatError("session resource identity has unsupported expansion flags");
    if (std::all_of(identity.resource_fingerprint.begin(),
                    identity.resource_fingerprint.end(),
                    [](std::uint8_t byte) { return byte == 0; }))
        throw FormatError("session resource identity has no resource fingerprint");

    std::vector<std::uint8_t> bytes;
    bytes.reserve(encoded_size);
    bytes.insert(bytes.end(), magic.begin(), magic.end());
    bytes.push_back(static_cast<std::uint8_t>(version));
    bytes.push_back(static_cast<std::uint8_t>(version >> 8));
    bytes.push_back(identity.language_code);
    bytes.push_back(identity.expansion_mask);
    bytes.insert(bytes.end(), identity.resource_fingerprint.begin(),
                 identity.resource_fingerprint.end());
    return bytes;
}

SessionResourceIdentity decode_session_resource_identity(
        const std::vector<std::uint8_t>& bytes) {
    if (bytes.size() != encoded_size)
        throw FormatError("session resource identity has an invalid size");
    if (!std::equal(magic.begin(), magic.end(), bytes.begin()))
        throw FormatError("invalid session resource identity signature");
    const auto stored_version = static_cast<std::uint16_t>(bytes[8]) |
        static_cast<std::uint16_t>(static_cast<std::uint16_t>(bytes[9]) << 8);
    if (stored_version != version)
        throw FormatError("unsupported session resource identity version");
    SessionResourceIdentity result;
    result.language_code = bytes[10];
    result.expansion_mask = bytes[11];
    std::copy(bytes.begin() + 12, bytes.end(), result.resource_fingerprint.begin());
    (void)encode_session_resource_identity(result); // Apply the same bounds on read.
    return result;
}

} // namespace odh
