#pragma once

#include "linux_private_family.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <string>

namespace odh_host {

// Data selected by the TS1 Create-a-Sim screen. body_type is the three-letter
// clothing body class used by Simitone's CAS collection (for example, "fat").
struct SimitoneCasPersonInput {
    std::string name_utf8;
    std::string biography_utf8;
    std::uint8_t language = 14;
    bool female = false;
    bool child = false;
    std::string skin; // lgt, med, or drk
    std::string body_type;
    std::string body_appearance;
    std::string body_texture;
    std::string head_appearance;
    std::string head_texture;
    std::string handgroup_texture;
    std::array<std::uint8_t, 5> personality_points{}; // 0..10, total at most 25
};

using CasRandomBelow = std::function<std::uint32_t(std::uint32_t)>;

// Converts one TS1 CAS choice to the bounded data consumed by the Linux
// private-family writer. Supplying random_below makes generated starting needs
// and traits reproducible in tests and save workflows.
LinuxPrivateFamilyMemberDraft make_simitone_cas_member(
    const SimitoneCasPersonInput& input, CasRandomBelow random_below = {});

} // namespace odh_host
