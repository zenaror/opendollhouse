#pragma once
#include "odh/world.hpp"
#include <array>
#include <optional>
#include <string>
#include <vector>
namespace odh {
struct SessionBundle;
struct SessionBundleLimits;
struct Ts1TravelMember {
    std::uint32_t guid=0;
    std::int16_t neighbor_id=0;
    std::uint16_t definition_chunk_id=0;
    std::string character_asset;
    std::array<std::uint8_t,32> character_fingerprint{};
};
struct Ts1TravelDepartureMember {
    Ts1TravelMember member;
    WorldEntityReference avatar{};
};
struct Ts1TravelRequest {
    std::int16_t destination=0; // -1 means immutable permanent home.
    WorldEntityReference thread_owner{};
    std::uint64_t run_generation=0,ticket=0;
    VmFrame source_frame;
    Instruction source_instruction;
    // Sealed by the host after Generic17's GOTO_TRUE_NEXT_TICK boundary.
    // A save authenticates this exact continuation against the actual owner VM.
    std::optional<VmSnapshot> continuation;
    // TRIP v2: a new source departure can include a permanent relative who
    // exists as a visiting NPC, without changing the current admitted subset.
    // Empty + absent retain the exact legacy v1 representation and semantics.
    std::vector<Ts1TravelDepartureMember> departure_members;
    std::optional<std::int16_t> departure_transit;
};
struct Ts1TravelState {
    std::uint16_t home_house=0,current_house=0,family_chunk_id=0;
    std::uint8_t home_mode=1,current_mode=1;
    std::int32_t family_number=0;
    std::uint64_t occupancy_epoch=0;
    std::array<std::uint8_t,32> neighborhood_identity{},family_identity{};
    std::vector<Ts1TravelMember> permanent_roster,travelers; // Current admitted subset, not a pending departure group.
    std::uint32_t caller_guid=0;
    std::int16_t caller_neighbor_id=0,transit_info=0;
    bool family_activated=false; // Generic18/26 has activated this destination runtime subset.
    std::optional<Ts1TravelRequest> pending;
};
bool same_ts1_travel_member(const Ts1TravelMember&,const Ts1TravelMember&);
// Require a pending request. Legacy requests use their historical state fields;
// modern requests use their explicitly captured source party and transit data.
std::vector<Ts1TravelMember> ts1_travel_departure_members(const Ts1TravelState&);
std::int16_t ts1_travel_departure_transit(const Ts1TravelState&);
// Resolve the exact live source body through an authenticated resident or NPC
// carrier. Does not promote a visitor to household/control/occupancy authority.
const Entity& resolve_ts1_travel_departure_avatar(const SessionBundle&,const Ts1TravelMember&);
void validate_ts1_travel_state(const Ts1TravelState&);
void seal_ts1_travel_request(Ts1TravelState&,const SessionBundle&);
void validate_ts1_travel_bundle(const SessionBundle&);
void validate_ts1_travel_bundle(const SessionBundle&,const SessionBundleLimits&);
std::vector<std::uint8_t> encode_ts1_travel_state(const Ts1TravelState&);
Ts1TravelState decode_ts1_travel_state(const std::vector<std::uint8_t>&);
}
