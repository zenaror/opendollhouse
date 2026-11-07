#pragma once
#include "odh/dialog.hpp"
#include <filesystem>
#include <stdexcept>
namespace odh { struct Ts1TravelState; }
namespace odh_host {
// Resource-backed selector choices only. These do not establish booking,
// occupancy, progression, or original-game destination eligibility rules.
std::optional<std::vector<odh::DialogChoice>> load_ts1_travel_choices(
    const std::filesystem::path& game_root,const std::filesystem::path& user_data,
    std::uint8_t dialog_type,std::uint8_t language,std::string& reason);

// Only list choices that a subsequent lot-switch request can identify uniquely.
std::optional<std::vector<odh::DialogChoice>> load_ts1_travel_selector_choices(
    const std::filesystem::path& game_root,const std::filesystem::path& user_data,
    std::uint8_t dialog_type,std::uint8_t language,std::string& reason);

// Normal play admits residential departures to Downtown and their return.
// Development builds can separately exercise the other expansion maps.
class Ts1TravelPolicyDenied : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};
bool ts1_travel_selector_allowed(bool all_destinations,std::uint8_t dialog_type) noexcept;
void validate_ts1_travel_source(const std::filesystem::path& game_root,
    const std::filesystem::path& user_data,const odh::Ts1TravelState& state,
    std::uint8_t language,bool all_destinations);
unsigned resolve_ts1_travel_destination(const std::filesystem::path& game_root,
    const std::filesystem::path& user_data,const odh::Ts1TravelState& state,int requested,
    std::uint8_t language,bool all_destinations);
}
