// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "community_lot_document.hpp"
#include <filesystem>
#include <functional>
#include <optional>

namespace odh_host {
using CommunityLotCheckpointHead=std::array<std::uint8_t,32>;
struct CommunityLotEditPublication {
    CommunityLotCheckpointHead head{};
    std::filesystem::path checkpoint;
};
// The host supplies authenticated compare-and-publish I/O. It must either throw
// or return the durable head of exactly this complete document. No file service,
// X11 surface, household, money or object constructor is owned by the session.
using CommunityLotEditPublisher=std::function<CommunityLotEditPublication(
    const std::optional<CommunityLotCheckpointHead>&,const CommunityLotDocument&)>;

class CommunityLotEditSession {
public:
    CommunityLotEditSession(CommunityLotDocument baseline,
        std::optional<CommunityLotCheckpointHead> head,CommunityLotEditPublisher);
    const CommunityLotDocument& current()const noexcept{return current_;}
    const CommunityLotDocument& baseline()const noexcept{return baseline_;}
    const std::optional<CommunityLotCheckpointHead>& expected_head()const noexcept{return head_;}
    bool dirty()const noexcept{return dirty_;}
    // Canonical full-document comparison includes future object state. Identity
    // changes or invalid state reject before replacing the in-memory draft.
    void stage(CommunityLotDocument);
    void stage_architecture(odh::ArchitectureSnapshot);
    // A clean save is a no-op, including when the original has no private file.
    // Conflict/failure preserves the draft, baseline and expected head.
    std::optional<CommunityLotEditPublication> save();
    // Restore the session's last successful save, not another writer's changes.
    // It never writes, deletes a checkpoint or silently rebases a conflict.
    void discard();
private:
    CommunityLotDocument baseline_,current_;
    std::vector<std::uint8_t> baseline_bytes_,current_bytes_;
    std::optional<CommunityLotCheckpointHead> head_;
    CommunityLotEditPublisher publish_;
    bool dirty_=false,publishing_=false;
};
}
