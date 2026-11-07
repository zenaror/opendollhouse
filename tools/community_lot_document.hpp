// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "odh/architecture_snapshot.hpp"
#include "odh/actorless_object_trade.hpp"
#include "odh/session_archive.hpp"

namespace odh_host {
// A lot edit is not a household session: no actor, family budget or lease.
struct CommunityLotIdentity {
    std::uint8_t neighborhood=1,mode=1;
    std::uint16_t house=0;
    std::array<std::uint8_t,32> neighborhood_sha256{},lot_sha256{};
};
bool same_community_lot(const CommunityLotIdentity&,const CommunityLotIdentity&) noexcept;
struct CommunityLotDocument {
    CommunityLotIdentity identity;
    odh::ArchitectureSnapshot architecture;
    std::optional<odh::ActorlessObjectTradeState> objects;
};
odh::SessionArchive encode_community_lot_document(const CommunityLotDocument&);
CommunityLotDocument decode_community_lot_document(const odh::SessionArchive&);
}
