#pragma once

#include "linux_private_character_store.hpp"
#include "odh/family_overlay.hpp"

#include <filesystem>
#include <string>

namespace odh_host {

struct LinuxPrivateFamilyMemberDraft {
    std::string name_utf8;
    std::string biography_utf8;
    std::uint8_t language = 14;
    std::vector<odh::TemplateBodyString> body_strings;
    std::vector<std::int16_t> person_data; // 88 CAS-created words.
};

struct CreatedLinuxPrivateFamily {
    std::uint16_t family_chunk_id;
    std::int32_t family_number;
    std::vector<CreatedPrivateCharacter> members;
};

// Create character IFFs in the player's private directory, then append their
// family and neighbor records to the player overlay. Any pre-commit failure
// rolls back only files whose contents still match this operation.
CreatedLinuxPrivateFamily create_linux_private_family(
    const std::vector<std::uint8_t>& template_iff,
    const std::filesystem::path& installed_characters,
    const std::filesystem::path& private_characters,
    const std::filesystem::path& neighborhood_iff,
    const std::filesystem::path& family_overlay_path,
    const std::string& family_name_utf8,
    const std::vector<LinuxPrivateFamilyMemberDraft>& members,
    std::int32_t initial_budget = 20000,
    CharacterGuidSource guid_source = {});

} // namespace odh_host
