#include "odh/session_resource_identity.hpp"

#include <cstdlib>
#include <iostream>

namespace {
void check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}
template<class Function>
void rejects(Function&& function, const char* message) {
    try { function(); }
    catch (const odh::FormatError&) { return; }
    check(false, message);
}
}

int main() {
    odh::SessionResourceIdentity identity;
    identity.language_code = 14;
    identity.expansion_mask = 0x7f;
    for (std::size_t i = 0; i < identity.resource_fingerprint.size(); ++i)
        identity.resource_fingerprint[i] = static_cast<std::uint8_t>(i + 1);
    const auto encoded = odh::encode_session_resource_identity(identity);
    const auto decoded = odh::decode_session_resource_identity(encoded);
    check(encoded.size() == 44, "META payload has fixed size");
    check(decoded.language_code == 14, "language survives META round trip");
    check(decoded.expansion_mask == 0x7f, "all seven expansion flags survive");
    check(decoded.resource_fingerprint == identity.resource_fingerprint,
          "resource fingerprint survives META round trip");

    auto changed = encoded;
    changed[8] = 2;
    rejects([&] { (void)odh::decode_session_resource_identity(changed); },
            "unsupported META version rejects");
    changed = encoded;
    changed[10] = 21;
    rejects([&] { (void)odh::decode_session_resource_identity(changed); },
            "unknown language rejects");
    changed = encoded;
    changed[11] = 0x80;
    rejects([&] { (void)odh::decode_session_resource_identity(changed); },
            "unknown expansion flag rejects");
    changed = encoded;
    for (std::size_t i = 12; i < changed.size(); ++i) changed[i] = 0;
    rejects([&] { (void)odh::decode_session_resource_identity(changed); },
            "missing resource fingerprint rejects");
    changed = encoded;
    changed.pop_back();
    rejects([&] { (void)odh::decode_session_resource_identity(changed); },
            "truncated META payload rejects");
    std::cout << "session resource identity: 9 checks passed\n";
}
