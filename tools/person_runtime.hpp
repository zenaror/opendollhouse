#pragma once
#include "neighborhood_storage.hpp"
#include "player_preferences.hpp"
#include "build_mode_presentation.hpp"

#include "odh/session_resource_identity.hpp"
#include "neighborhood_request.hpp"
#include "odh/ts1_travel.hpp"
#include "odh/architecture_edit.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace odh {
class PreparedHouseholdTransfer;
class InstalledOccupancyRetirement;
class PreparedTs1TravelReturn;
struct NeighborhoodFamilySource;
struct FamilyPropertyRelease;
struct ArchitectureSnapshot;
}

namespace odh_host {
class LinuxWindow;
class LinuxAudioRuntime;
class OriginalCursorPresentation;
class LinuxSessionLog;
class FamilySessionJournal;
class PrivateVacationSession;
class PreparedPropertyRelease;
struct CommunityLotIdentity;
struct CommunityLotDocument;
using NeighborhoodResumeCallback = std::function<std::optional<NeighborhoodMapRequest>()>;

struct NeighborhoodTransitionRequest {
    std::uint16_t destination_house = 0;
    std::filesystem::path saved_session_path;
    NeighborhoodRequestKind kind = NeighborhoodRequestKind::travel;
    bool resume_buy_mode = false; // Presentation state after a committed same-house command.
    bool paused_before_buy_mode = false;
    bool resume_build_mode = false;
    bool paused_before_build_mode = false;
    // Same-house committed UI continuation, never a save/travel payload.
    std::optional<BuildModePresentationState> build_presentation;
};
struct PersonRunResult {
    int exit_code = 0;
    std::optional<NeighborhoodTransitionRequest> transition;
    struct TemporaryTravel {
        odh::Ts1TravelState state;
        std::filesystem::path saved_session_path;
        std::vector<std::uint8_t> suspended_home;
    };
    std::optional<TemporaryTravel> temporary_travel;
    // The runtime may adopt an original family when publishing its first
    // neighborhood-wide state. Return that authority to the launcher.
    std::shared_ptr<FamilySessionJournal> family_journal;
    // UI exit to the ordinary neighborhood, distinct from travelling the Sims
    // or publishing a family-management transaction.
    bool return_to_neighborhood=false;
    // Host presentation selection only; not part of the save or travel state.
    std::optional<std::uint8_t> presentation_speed;
    std::shared_ptr<const PreparedPropertyRelease> prepared_property_release;
    std::shared_ptr<PrivateVacationSession> private_vacation_session;
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
// One external property command, consumed once before simulation advances.
// Target generations are resolved from the live session, never from CLI input.
struct SessionObjectCommand {
    enum class Kind { purchase, move, sell, cancel };
    Kind kind=Kind::cancel;
    std::uint32_t guid=0;
    std::int16_t object=0;
    std::int32_t x=0,y=0;
    std::int16_t level=1;
    std::uint8_t direction=0;
};
struct IntegratedSessionContext {
    // Host presentation lifetime only, never serialized. The launcher retains
    // the global SDL owner across house/session transitions.
    std::shared_ptr<LinuxAudioRuntime> audio_runtime;
    std::shared_ptr<const odh::ArchitectureSnapshot> community_architecture;
    // Authenticated launcher-owned request. Runtime prepares only, without
    // scheduler ticks, presentation capture, normal saves or publication.
    bool prepare_property_release=false;
    std::shared_ptr<const odh::NeighborhoodFamilySource> property_release_source;
    std::shared_ptr<const CommunityLotIdentity> property_release_identity;
    // Current vacant property head, loaded by the launcher and independently
    // authenticated/reconstructed before this runtime exposes a World.
    std::shared_ptr<const CommunityLotDocument> property_lot;
    std::shared_ptr<const odh::FamilyPropertyRelease> property_lot_reference;
    std::vector<std::uint8_t> property_lot_source_archive;
    // Separate from residence/transfer: a visit retains the permanent family
    // and exact suspended home while the active session describes another lot.
    std::optional<odh::Ts1TravelState> travel_state;
    std::vector<std::uint8_t> suspended_home;
    // Enabled only after the host can bootstrap the destination without
    // publishing or ticking its arriving avatars ahead of the source call.
    bool temporary_travel_enabled = false;
    // Launcher resource selection only; runtime must bind NPRM/departure NPCA.
    bool npc_execution_primary = false;
    // Metadata bootstrap never authorizes a primary body/VM/player identity.
    bool unanchored_family_lot = false;
    std::optional<odh::WorldEntityReference> metadata_bootstrap_reference;
    // Explicit fresh-import opt-in; existing NINV1/absent archives cannot migrate.
    bool original_companion_protocol_fresh = false;
    odh::Ts1TravelProtocol travel_protocol = odh::Ts1TravelProtocol::legacy_pin;
    // A private validation launcher can exercise destinations beyond Downtown.
    bool all_travel_destinations = false;
    // Fresh destination construction uses the exact departed active session,
    // which can differ from the permanent suspended home on later visits.
    // Its VM/entity IDs are never restored into the new destination World.
    bool temporary_travel_arrival = false;
    std::vector<std::uint8_t> travel_departure_archive;
    std::filesystem::path travel_checkpoint_path;
    std::optional<std::array<std::uint8_t,32>> travel_checkpoint_head;
    // The launcher authenticates the durable visited source separately. The
    // runtime prepares this unpublished home candidate, then publishes home()
    // with the same expected source head before allowing its first tick.
    std::shared_ptr<const odh::PreparedTs1TravelReturn> travel_return;
    std::shared_ptr<FamilySessionJournal> family_journal;
    std::shared_ptr<PrivateVacationSession> private_vacation_session;
    std::shared_ptr<const odh::PreparedHouseholdTransfer> family_transfer;
    std::shared_ptr<const odh::InstalledOccupancyRetirement> occupancy_retirement;
    // Actual HHRD execution cohort, possibly empty for a typed unanchored lot.
    std::vector<IntegratedHouseholdMember> household_members;
    bool single_resident_session = false;
    std::optional<odh::SessionResourceIdentity> resources;
    std::vector<std::uint8_t> restore_archive; // Verified launch input; never rewritten.
    bool save_on_exit = false;
    bool start_buy_mode = false;
    bool paused_before_buy_mode = false;
    bool start_build_mode = false;
    std::uint8_t presentation_speed = 1;
    bool paused_before_build_mode = false;
    std::optional<SessionObjectCommand> object_command;
    // A real architecture transaction, consumed once before simulation advances.
    std::optional<odh::ArchitectureEditCommand> architecture_command;
    bool bounded_headless = false;
    std::filesystem::path game_root;
    std::filesystem::path installed_user_data;
    unsigned neighborhood_number=1;
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
    std::shared_ptr<OriginalCursorPresentation> cursors;
    PlayerPreferences preferences;
    // Publish a preference only after its platform store accepts the change.
    std::function<bool(const PlayerPreferences&,std::string&)> save_preferences;
    std::optional<BuildModePresentationState> build_presentation;
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
