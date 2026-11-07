// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "odh/world.hpp"
#include <functional>
#include <memory>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <vector>

namespace odh_host {
// Native325af/32888 floor-head ownership. The ordinary spatial registry stores
// all occupants and cannot establish their native head order. Seed is permitted
// only from a complete authenticated cohort and its exact reciprocal chains.
class NativeFloorHeads {
public:
    using Ref=odh::WorldEntityReference;
    NativeFloorHeads(odh::World&,std::uint32_t width,std::uint32_t height,std::function<bool()> authority);
    bool seed(const std::vector<Ref>& whole_cohort,std::string& error);
    using PendingUnlinkedSource=std::function<std::vector<Ref>()>;
    // Runtime deletion uses a short-lived cell owner, not a persistent World
    // snapshot. The provider contains ONLY live Delete receipts certified by
    // NativeDeletionHierarchy::certified_detached(), never inferred raw words.
    static std::shared_ptr<NativeFloorHeads> for_current_world(odh::World&,std::uint32_t,std::uint32_t,
        std::function<bool()>,PendingUnlinkedSource={});
    // Record a completed native unlink while its old XYZ intentionally remains.
    // No floor head may still reference it. Does not erase unknown cells.
    bool mark_source_unlinked(Ref,std::string& error);
    bool ready()const noexcept{return seeded_;}
    bool original_cohort_owner()const noexcept{return seeded_&&!current_world_mode_;}
    // Unknown/ambiguous is nullopt; known-empty is exactly {0,0}.
    std::optional<Ref> head(const odh::EntityPosition&);
    // Same admission as head(), without publishing World changes. Failure
    // retains its actual resolver cause plus a read-only cell inventory.
    std::optional<Ref> checked_head(const odh::EntityPosition&,std::string& error);
    std::string describe_cell(const odh::EntityPosition&)const;
    bool publish(const odh::EntityPosition&,Ref expected,Ref next,std::string& error);
    // Actual constructors may extend the cohort only while their native pose
    // proves OOW and both hierarchy and own zero slot are empty. An unexplained
    // new/missing generation otherwise invalidates all empty-cell claims.
    bool admit_out_of_world(Ref,std::string& error);
    bool forget_unlinked(Ref,std::string& error);
    // Actual World retirement lease only. It removes the retired parent from
    // this index's cohort, while authenticating children still physically held
    // by its retained body. Tokens are copied; a bare parent ID is insufficient.
    bool begin_retired_container(const odh::World::RetiredContainer&,std::string& error);
    bool end_retired_container(const odh::World::RetiredContainer&,std::string& error);
    void abandon_retired_container(const odh::World::RetiredContainer&);
    // Around the actual source child OOW/find callback. Pending admission does
    // not excuse unknown cells or nonreciprocal links. A placed unparented body
    // can publish only into a known-empty head; coincident owners are rejected.
    bool begin_retired_child_position(const odh::World::RetiredContainer&,Ref,std::string& error);
    bool end_retired_child_position(const odh::World::RetiredContainer&,Ref,std::string& error);
    // Before dispatch/save, all pending source-order index writes must have
    // settled through the actual scalar/hierarchy consumers.
    bool settled(std::string& error);
private:
    using Tile=std::tuple<std::int16_t,std::int16_t,std::int16_t>;
    struct View {Ref ref{};std::optional<odh::EntityPosition> pose;std::optional<Ref> child;
        std::optional<std::int16_t> slot,parent;std::optional<Ref> physical;};
    struct Entry {Ref head{};std::optional<odh::EntityPosition> pending_pose;};
    odh::World* world_;std::uint32_t width_,height_;std::function<bool()> authority_;
    bool seeded_=false,knowledge_=false;
    std::set<Ref> cohort_;std::map<Tile,Entry> heads_;std::set<Tile> unknown_;
    std::map<Tile,std::string> unknown_reasons_;
    std::map<Ref,odh::EntityPosition> removed_pending_;
    std::optional<Ref> publication_next_;
    std::map<Ref,odh::World::RetiredContainer> retired_containers_;
    std::optional<std::pair<Ref,Ref>> pending_retired_child_;
    bool retained_physical_parent(Ref,Ref,std::size_t) const;
    bool current_world_mode_=false;
    PendingUnlinkedSource pending_unlinked_source_;
    std::set<Ref> forgotten_refs_;
    std::optional<Ref> current_cell_head(const odh::EntityPosition&,std::string&);
    bool publish_current_cell(const odh::EntityPosition&,Ref,Ref,std::string&);
    bool settled_current_cells(std::string&);
    std::optional<Tile> tile(const odh::EntityPosition&)const;
    bool authority()const;
    bool collect(std::map<Ref,View>&,std::string& error)const;
    bool refresh(std::map<Ref,View>&,std::string& error);
};
} // namespace odh_host
