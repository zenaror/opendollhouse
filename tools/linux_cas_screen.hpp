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

// Keyboard/mouse CAS flow. Escape cancels without writing player data.
std::optional<LinuxCasFamilyDraft> show_linux_cas_family_creator(
    LinuxWindow& window, const LinuxCasCatalog& catalog, std::uint8_t language);

} // namespace odh_host
