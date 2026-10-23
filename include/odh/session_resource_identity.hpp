#pragma once

#include "odh/formats.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace odh {

enum class SessionExpansionFlag : std::uint8_t {
    livin_large = 1u << 0,
    house_party = 1u << 1,
    hot_date = 1u << 2,
    vacation = 1u << 3,
    unleashed = 1u << 4,
    superstar = 1u << 5,
    makin_magic = 1u << 6,
};

// Stable META payload for a saved session. The fingerprint is SHA-256 of the
// canonical manifest of the exact game resources used by that session.
struct SessionResourceIdentity {
    std::uint8_t language_code = 1;
    std::uint8_t expansion_mask = 0;
    std::array<std::uint8_t,32> resource_fingerprint{};
};

// Encodes a versioned, fixed-size META section. An all-zero fingerprint is
// rejected so callers cannot accidentally create an unbound save.
std::vector<std::uint8_t> encode_session_resource_identity(
    const SessionResourceIdentity& identity);
SessionResourceIdentity decode_session_resource_identity(
    const std::vector<std::uint8_t>& bytes);

} // namespace odh
