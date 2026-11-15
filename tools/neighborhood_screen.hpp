#pragma once

#include "neighborhood_presentation.hpp"
namespace odh_host { class LinuxWindow; }
#include "neighborhood_request.hpp"
#include "odh/session_entry.hpp"
#include "odh/neighborhood_picker.hpp"
#include "odh/ui_bitmap.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace odh_host {

struct MoveInFamilySelection {
    std::uint16_t family_chunk_id = 0;
    std::array<std::uint8_t, 32> family_identity{};
    enum class Action { move_in, archive, rename, create_family } action = Action::move_in;
};

enum class NeighborhoodPickerAction { lot, create_family, switch_mode, manage_lot, select_family, switch_neighborhood };
struct NeighborhoodPickerResult {
    NeighborhoodPickerAction action = NeighborhoodPickerAction::lot;
    std::uint16_t house_number = 0;
    unsigned mode = 0;
    int neighborhood_delta=0;
};

enum class OccupiedLotChoice { enter, move_out, cancel, rename };

enum class NeighborhoodLotPanelChoice { continue_to_lot, back_to_map };
NeighborhoodLotPanelChoice show_neighborhood_lot_panel(LinuxWindow& window,
    const PreparedNeighborhoodMap& map, std::uint16_t house_number,
    const std::string& title, const std::string& description,
    const std::string& summary, const std::string& localized_accept,
    const std::string& localized_back, std::uint8_t language);

PreparedNeighborhoodMap prepare_neighborhood_map(
    const std::filesystem::path& user_data,
    const std::filesystem::path& ui_graphics, unsigned mode,
    const odh::FamilyOverlay* saved_overlay = nullptr,
    std::uint8_t language = 1);

// Build the exact 800x600 neighborhood frame without opening an X11 window.
odh::RasterFrame compose_neighborhood_picker_frame(
    const PreparedNeighborhoodMap& map,
    const std::string& localized_title = "Neighborhood",
    const std::string& localized_cancel = "Cancel",
    std::uint8_t language = 1);

// First-run choice for one of the 20 language codes present in The Sims 1 PC.
// Returns nullopt when the player cancels or closes the window.
std::optional<std::uint8_t> show_language_picker(LinuxWindow& window,
                                                  std::uint8_t initial_language);

// Show the real neighborhood map in an existing 800x600 window.
// Returns a house number on selection, or nullopt when the user cancels.
std::optional<std::uint16_t> show_neighborhood_picker(
    LinuxWindow& window, const PreparedNeighborhoodMap& map,
    const std::string& localized_title = "Neighborhood",
    const std::string& localized_cancel = "Cancel",
    std::uint8_t language = 1);

// Neighborhood picker variant used by the game entry flow. C opens the family list;
// Escape or window close cancels; a clicked lot returns its house number.
std::optional<NeighborhoodPickerResult> show_neighborhood_picker_with_create(
    LinuxWindow& window, const PreparedNeighborhoodMap& map,
    const std::string& localized_title = "Neighborhood",
    const std::string& localized_cancel = "Cancel",
    std::uint8_t language = 1);

// Main neighborhood screen actions: select a lot, switch to another supported
// destination, or open the family list. Namespace arrows remain disabled until
// their caller can supply a separately owned neighborhood and saves.
std::optional<NeighborhoodPickerResult> show_neighborhood_picker_with_actions(
    LinuxWindow& window, const PreparedNeighborhoodMap& map,
    const std::string& localized_title, const std::string& localized_cancel,
    std::uint8_t language, bool allow_create_family,const std::string& create_label={},
    NeighborhoodHoverTextProvider hover_text={},const std::filesystem::path& private_characters={},
    NeighborhoodPortraitProvider portraits={},unsigned neighborhood_number=1,
    bool previous_available=false,bool next_available=false);

// Arrow hit returns -1/+1; the separate destination hit returns a mode or zero.
int neighborhood_arrow_at(std::int32_t x,std::int32_t y) noexcept;
unsigned neighborhood_mode_button_at(std::int32_t x, std::int32_t y) noexcept;

// Choose whether to enter an occupied lot or move its current family out.
// Move-out is disabled when the saved family history cannot calculate a
// verified refund. Escape cancels; E selects move-out; Enter/click enters.
OccupiedLotChoice show_occupied_lot_choice(LinuxWindow& window,
    const odh::FamilyEntry& family, std::uint16_t house_number,
    bool move_out_supported, std::uint8_t language,const PreparedNeighborhoodMap* map=nullptr);

// With a selected lot, confirm an affordable unhoused family. House 0 opens
// the family list before a vacant lot has been selected. Neither path writes.
std::optional<MoveInFamilySelection> show_move_in_family_picker(
    LinuxWindow& window, const std::vector<odh::FamilyEntry>& families,
    std::uint16_t house_number, std::int32_t purchase_price, std::uint8_t language,
    const std::string& localized_action = "Move In Family",
    const std::string& localized_title = "SELECT A FAMILY",
    const std::string& localized_no_family_title = "Sorry",
    const std::string& localized_no_family_message = "You can't afford it.",
    const std::string& localized_cancel = "Cancel",
    const std::filesystem::path& ui_graphics = {},
    const std::string& localized_net_worth = "Net Worth",
    const std::string& localized_family_friends = "Family Friends",
    const std::string& localized_archive = "Delete Family",
    const std::vector<std::uint16_t>& deletable_family_ids = {},NeighborhoodPortraitProvider portraits = {});

// Edits a UTF-8 display name in memory. Escape/window close cancels with no
// writes; the caller publishes only after revalidating its overlay snapshot.
std::optional<std::string> show_family_name_editor(LinuxWindow& window,
    const std::string& initial_name_utf8, std::uint8_t language,
    const std::string& localized_title = "Rename Family",
    const std::string& localized_accept = "Save",
    const std::string& localized_cancel = "Cancel",
    const std::filesystem::path& ui_graphics = {},
    const std::string& localized_prompt = {});

// Confirm logical deletion of an unhoused family. Source files remain intact.
bool show_family_archive_confirmation(LinuxWindow& window,
    const odh::FamilyEntry& family, std::uint8_t language,
    const std::string& localized_title, const std::string& localized_question,
    const std::string& localized_yes, const std::string& localized_no,
    const std::filesystem::path& ui_graphics = {});

// Final confirmation shown before a player-state write; closing cancels it.
bool show_move_in_confirmation(LinuxWindow& window, const odh::FamilyEntry& family,
    std::uint16_t house_number, std::int32_t purchase_price, std::uint8_t language,
    const std::string& localized_title = "Purchase House?",
    const std::string& localized_question = "Are you sure you want to buy this house?",
    const std::string& localized_yes = "Yes", const std::string& localized_no = "No",
    const std::filesystem::path& ui_graphics = {});

// Confirm move-out before writing; caller supplies the localized family/refund question.
bool show_move_out_confirmation(LinuxWindow& window, const odh::FamilyEntry& family,
    std::uint16_t house_number, std::uint8_t language,
    const std::string& localized_title = "Evict Family?",
    const std::string& localized_yes = "Yes", const std::string& localized_no = "No",
    const std::filesystem::path& ui_graphics = {},
    const std::string& localized_question = "Are you sure you want to evict this family?");

// Same-lot selection resumes live state; other lots request a saved transition.
// Management returns intent only, before the runtime saves and unwinds.
std::optional<NeighborhoodMapRequest> show_neighborhood_resume_picker(
    LinuxWindow& window, const PreparedNeighborhoodMap& map, std::uint16_t active_house);

// Select a family member in the existing window. Rows identify Sims by their
// neighborhood ID and character resource filename; this is not a name editor.
std::optional<std::size_t> show_resident_picker(
    LinuxWindow& window, const std::vector<odh::SessionEntryMember>& residents,
    std::uint8_t language);

} // namespace odh_host
