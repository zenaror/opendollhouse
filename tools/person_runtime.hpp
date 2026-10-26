#pragma once

#include "odh/session_resource_identity.hpp"
#include "neighborhood_request.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace odh { class PreparedHouseholdTransfer; class InstalledOccupancyRetirement; }

namespace odh_host {
class LinuxWindow;
class LinuxSessionLog;
class FamilySessionJournal;
using NeighborhoodResumeCallback = std::function<std::optional<NeighborhoodMapRequest>()>;

struct NeighborhoodTransitionRequest {
    std::uint16_t destination_house = 0;
    std::filesystem::path saved_session_path;
    NeighborhoodRequestKind kind = NeighborhoodRequestKind::travel;
};
struct PersonRunResult {
    int exit_code = 0;
    std::optional<NeighborhoodTransitionRequest> transition;
};

// Stable launch identity retained by the Linux runtime for save metadata.
// Direct diagnostic invocations may omit it; integrated game launches supply it.
struct IntegratedHouseholdMember {
    std::filesystem::path character_path;
    std::uint32_t guid = 0;
    std::int16_t neighbor_id = 0;
    std::uint16_t object_definition_id = 0;
    // Stable logical location, independent of where the installation lives.
    std::string character_asset;
};
struct IntegratedSessionContext {
    std::shared_ptr<FamilySessionJournal> family_journal;
    std::shared_ptr<const odh::PreparedHouseholdTransfer> family_transfer;
    std::shared_ptr<const odh::InstalledOccupancyRetirement> occupancy_retirement;
    // Empty only for the explicit single-resident save/restore test profile.
    std::vector<IntegratedHouseholdMember> household_members;
    bool single_resident_session = false;
    std::optional<odh::SessionResourceIdentity> resources;
    std::vector<std::uint8_t> restore_archive; // Verified launch input; never rewritten.
    bool save_on_exit = false;
    bool bounded_headless = false;
    std::filesystem::path game_root;
    std::filesystem::path player_character_root;
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
odh_host::PersonRunResult odh_person_run(int argc, char** argv, odh_host::LinuxWindow* shared_window,
                    odh_host::NeighborhoodResumeCallback neighborhood_resume = {},
                    std::optional<odh_host::IntegratedSessionContext> session_context = std::nullopt,
                    odh_host::LinuxSessionLog* session_log = nullptr);

// Integrated Linux entry point selected by the executable wrapper when the
// user supplies --game-root.
int odh_linux_game_entry(int argc, char** argv, odh_host::LinuxSessionLog* session_log = nullptr);
