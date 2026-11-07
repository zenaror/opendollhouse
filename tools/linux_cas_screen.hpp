#pragma once

#include "linux_simitone_cas_member.hpp"
#include "linux_cas_catalog.hpp"
#include "linux_window.hpp"

#include <optional>
#include <string>
#include <vector>

namespace odh_host {

struct LinuxCasFamilyDraft {
    std::string family_name_utf8;
    std::vector<SimitoneCasPersonInput> members;
};

// Keyboard/mouse CAS flow. Escape dismisses the current question or editor;
// leaving the family asks before discarding the draft. Only Done returns it.
std::optional<LinuxCasFamilyDraft> show_linux_cas_family_creator(
    LinuxWindow& window, const LinuxCasCatalog& catalog, std::uint8_t language,const std::filesystem::path& game_root);

} // namespace odh_host
