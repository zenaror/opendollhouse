#pragma once

#include "odh/template_person.hpp"
#include "odh/sha256.hpp"

#include <filesystem>
#include <functional>

namespace odh_host {

struct CreatedPrivateCharacter {
    std::uint32_t guid;
    std::filesystem::path path;
    std::array<std::uint8_t,32> content_sha256{};
};

using CharacterGuidSource = std::function<std::uint32_t()>;

// Creates a character only under the player's private data directory. The
// installed character catalog is read to avoid reusing its person GUIDs.
// A custom GUID source is provided for deterministic tests; production uses OS entropy.
CreatedPrivateCharacter create_linux_private_character(
    const std::vector<std::uint8_t>& template_iff,
    const std::filesystem::path& installed_characters,
    const std::filesystem::path& private_characters,
    const odh::TemplatePersonRequest& request,
    CharacterGuidSource guid_source = {});

// Roll back only the exact private file created by the matching result. A file
// changed since creation is preserved and reported as not removed.
bool remove_linux_private_character_if_unchanged(const CreatedPrivateCharacter&);

} // namespace odh_host
