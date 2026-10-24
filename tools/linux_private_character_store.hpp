#pragma once

#include "odh/template_person.hpp"

#include <filesystem>
#include <functional>

namespace odh_host {

struct CreatedPrivateCharacter {
    std::uint32_t guid;
    std::filesystem::path path;
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

} // namespace odh_host
