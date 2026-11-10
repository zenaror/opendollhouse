#include "session_resource_manifest.hpp"

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>

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
void write(const std::filesystem::path& path, const std::string& contents) {
    std::ofstream output(path, std::ios::binary);
    output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    if (!output) throw std::runtime_error("fixture write failed");
}
std::string hex(const std::array<std::uint8_t,32>& digest) {
    constexpr char digits[] = "0123456789abcdef";
    std::string result;
    result.reserve(digest.size() * 2);
    for (const auto byte : digest) {
        result.push_back(digits[byte >> 4]);
        result.push_back(digits[byte & 15]);
    }
    return result;
}
}

int main() {
    const auto suffix = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    const auto root = std::filesystem::temp_directory_path() /
        ("odh-resource-manifest-" + std::to_string(suffix));
    const auto outside = std::filesystem::temp_directory_path() /
        ("odh-resource-outside-" + std::to_string(suffix));
    std::filesystem::create_directories(root / "GameData");
    write(root / "GameData/Global.far", "global fixture bytes");
    write(root / "GameData/Objects.far", "object fixture");
    write(outside, "outside");
    try {
        const std::vector<std::filesystem::path> forward{
            "GameData/Global.far", "GameData/Objects.far"};
        const std::vector<std::filesystem::path> reverse{
            "GameData/Objects.far", "GameData/Global.far"};
        const auto first = odh_host::identify_session_resources(root, forward, 14, 0x7f);
        check(hex(first.resource_fingerprint) ==
              "ec2b5a89dfe733aa0957f5d96c5d0bf7159fc8915d214aba794c91f5c67a94f1",
              "accelerated and built-in SHA-256 produce the same stable manifest fingerprint");
        const auto reordered = odh_host::identify_session_resources(root, reverse, 14, 0x7f);
        check(first.resource_fingerprint == reordered.resource_fingerprint,
              "manifest fingerprint ignores input enumeration order");
        check(first.language_code == 14 && first.expansion_mask == 0x7f,
              "identity retains language and expansion flags");
        const auto saved_meta = odh::encode_session_resource_identity(first);
        const auto reopened_meta = odh::decode_session_resource_identity(saved_meta);
        check(reopened_meta.resource_fingerprint == first.resource_fingerprint,
              "manifest fingerprint fits the versioned META payload");

        write(root / "GameData/Objects.far", "changed object fixture");
        const auto modified = odh_host::identify_session_resources(root, forward, 14, 0x7f);
        check(first.resource_fingerprint != modified.resource_fingerprint,
              "changed resource bytes change identity");
        std::filesystem::rename(root / "GameData/Objects.far", root / "GameData/Renamed.far");
        const auto renamed = odh_host::identify_session_resources(root,
            {"GameData/Global.far", "GameData/Renamed.far"}, 14, 0x7f);
        check(modified.resource_fingerprint != renamed.resource_fingerprint,
              "changed relative resource path changes identity");
        std::filesystem::rename(root / "GameData/Renamed.far", root / "GameData/Objects.far");
        const auto other_language = odh_host::identify_session_resources(root, forward, 3, 0x7f);
        const auto other_expansions = odh_host::identify_session_resources(root, forward, 14, 0x3f);
        check(modified.resource_fingerprint == other_language.resource_fingerprint &&
              modified.resource_fingerprint == other_expansions.resource_fingerprint,
              "resource fingerprint covers files while metadata keeps language and expansions separate");
        check(odh::encode_session_resource_identity(modified)!=odh::encode_session_resource_identity(other_language)&&
              odh::encode_session_resource_identity(modified)!=odh::encode_session_resource_identity(other_expansions),
              "selected identity admission compares full META including language/mask, not only same-file fingerprint");
        rejects([&] { (void)odh_host::identify_session_resources(root, {}, 14, 0x7f); },
            "fresh selected plan cannot manufacture identity from an empty resource catalog");
        rejects([&] { (void)odh_host::identify_session_resources(root, forward, 14, 0x80); },
            "fresh selected plan cannot admit unsupported expansion metadata");


        rejects([&] { (void)odh_host::identify_session_resources(root,
            {"GameData/Global.far", "GameData/Global.far"}, 1, 0); },
            "duplicate manifest path rejects");
        rejects([&] { (void)odh_host::identify_session_resources(root, {outside}, 1, 0); },
            "resource outside game root rejects");
        rejects([&] { (void)odh_host::identify_session_resources(root,
            {"missing.far"}, 1, 0); }, "missing resource rejects");
        rejects([&] { (void)odh_host::identify_session_resources(root, forward, 21, 0); },
            "unsupported language rejects before reading files");
        rejects([&] { (void)odh_host::identify_session_resources(root, forward, 1, 0,
            4); }, "manifest over byte budget rejects");
        std::cout << "session resource manifest: 15 checks passed\n";
    } catch (...) {
        std::filesystem::remove_all(root);
        std::filesystem::remove(outside);
        throw;
    }
    std::filesystem::remove_all(root);
    std::filesystem::remove(outside);
}
