#include "odh/session_resource_identity.hpp"
#include "session_generic_resource_path.hpp"
#include <chrono>
#include <fstream>

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
    const auto directory=std::filesystem::temp_directory_path()/
        ("odh-generic-resource-path-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    check(std::filesystem::create_directory(directory),"create owned portable resource fixture");
    struct Cleanup {std::filesystem::path path;~Cleanup(){std::error_code error;std::filesystem::remove_all(path,error);}} cleanup{directory};
    const auto first=directory/"first",second=directory/"relocated";
    const auto key=std::filesystem::path("GameData/Objects/Objects.far");
    for(const auto& root:{first,second}){
        std::filesystem::create_directories((root/key).parent_path());
        std::ofstream(root/key)<<"synthetic resource";
    }
    check(odh_host::generic_session_source_key(first,(first/key).string())==key.generic_string(),
        "absolute live resource becomes a portable generic key");
    check(odh_host::generic_session_source_key(first,key.generic_string())==key.generic_string(),
        "restored relative generic key normalizes idempotently");
    check(odh_host::generic_session_source_key(second,(second/key).string())==
        odh_host::generic_session_source_key(first,(first/key).string()),
        "relocating the game root preserves generic resource identity");
    rejects([&]{(void)odh_host::generic_session_source_key(first,(second/key).string());},
        "a checkpoint cannot disguise an outside-root resource as game-relative");
    odh_host::validate_generic_session_source_key(key.generic_string());
    for(const auto& invalid:{std::string{},std::string("../outside.far"),std::string("/absolute.far"),
        std::string("C:/Objects.far"),std::string("GameData/../Objects.far"),
        std::string("GameData")+char(92)+"Objects.far"})
        rejects([&]{odh_host::validate_generic_session_source_key(invalid);},"nonportable archive key accepted");
    std::cout << "session resource identity and portable generic path checks passed\n";
}
