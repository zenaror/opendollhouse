#pragma once

#include <cstdint>

namespace odh_host {
// A live map may request management, but may not change family authority.
// The runtime must save and unwind before the launcher handles that intent.
enum class NeighborhoodRequestKind { travel, manage_families, reload_committed_session };
struct NeighborhoodMapRequest {
    NeighborhoodRequestKind kind = NeighborhoodRequestKind::travel;
    std::uint16_t destination_house = 0; // Used only for travel.
};
}
