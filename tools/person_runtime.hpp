#pragma once

#include "odh/session_resource_identity.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <vector>

namespace odh_host {
class LinuxWindow;
using NeighborhoodResumeCallback = std::function<std::optional<std::uint16_t>()>;

// Stable launch identity retained by the Linux runtime for save metadata.
// Direct diagnostic invocations may omit it; integrated game launches supply it.
struct IntegratedHouseholdMember {
    std::filesystem::path character_path;
    std::uint32_t guid = 0;
    std::int16_t neighbor_id = 0;
    std::uint16_t object_definition_id = 0;
};
struct IntegratedSessionContext {
    // Empty only for the explicit single-resident save/restore test profile.
    std::vector<IntegratedHouseholdMember> household_members;
    bool single_resident_session = false;
    std::optional<odh::SessionResourceIdentity> resources;
    std::vector<std::uint8_t> restore_archive; // Verified launch input; never rewritten.
    bool save_on_exit = false;
    bool bounded_headless = false;
    std::filesystem::path game_root;
    std::vector<std::filesystem::path> resource_paths;
    std::uint8_t language_code = 1;
    std::uint8_t expansion_mask = 0;
    std::uint8_t neighborhood_mode = 0;
    std::uint16_t house_number = 0;
    std::uint16_t family_chunk_id = 0;
    std::int32_t family_number = 0;
    std::uint32_t resident_guid = 0;
    std::int16_t resident_neighbor_id = 0;
};
}

// Run the person/lot runtime using the existing command-line options. When
// render-window is requested, an optional shared host window is reused.
int odh_person_run(int argc, char** argv, odh_host::LinuxWindow* shared_window,
                    odh_host::NeighborhoodResumeCallback neighborhood_resume = {},
                    std::optional<odh_host::IntegratedSessionContext> session_context = std::nullopt);

// Integrated Linux entry point selected by the executable wrapper when the
// user supplies --game-root.
int odh_linux_game_entry(int argc, char** argv);
