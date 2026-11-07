// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "community_lot_document.hpp"
#include <filesystem>
#include <optional>
namespace odh_host {
struct CommunityLotStoreOptions {
    std::filesystem::path game_root,user_data,lot_path,private_directory;
    unsigned neighborhood_number=1;
    std::uint8_t mode=1;
    std::uint16_t house=0;
    std::optional<CommunityLotIdentity> expected_identity;
};
struct StoredCommunityLot {
    CommunityLotDocument document;
    std::array<std::uint8_t,32> head{};
    std::filesystem::path checkpoint;
};
// Document and exact immutable byte head originate from one owned read.
// Immutable document blobs only. These never read/write the legacy editor
// architecture.odhl HEAD; residential authority belongs to FamilyOverlay.
StoredCommunityLot publish_community_lot_blob(const CommunityLotStoreOptions&,const CommunityLotDocument&);
StoredCommunityLot load_community_lot_blob(const CommunityLotStoreOptions&,const std::array<std::uint8_t,32>& head);
std::optional<StoredCommunityLot> load_private_community_lot(const CommunityLotStoreOptions&);
StoredCommunityLot publish_private_community_lot(const CommunityLotStoreOptions&,
    const std::optional<std::array<std::uint8_t,32>>& expected_head,const CommunityLotDocument&);
CommunityLotIdentity community_lot_identity(const CommunityLotStoreOptions&);
std::optional<odh::ArchitectureSnapshot> load_private_community_architecture(const CommunityLotStoreOptions&);
std::optional<std::array<std::uint8_t,32>> community_lot_checkpoint_head(const CommunityLotStoreOptions&);
std::filesystem::path publish_private_community_architecture(const CommunityLotStoreOptions&,
    const std::optional<std::array<std::uint8_t,32>>& expected_head,const odh::ArchitectureSnapshot&);
}
