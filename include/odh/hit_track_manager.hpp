#pragma once

#include "odh/hit_arguments.hpp"
#include "odh/hit_native_controller.hpp"
#include "odh/hit_source_contributions.hpp"
#include <functional>
#include <memory>
#include <string_view>
#include <vector>

namespace odh {
struct HitTrackManagerLimits {
    std::size_t controllers = 64;
    std::size_t source_pairs = 256;
};
// Providers may inspect snapshots and external source state, but must not mutate
// controllers/catalog/resources through retained aliases or reenter this manager.
struct HitTrackManagerProviders {
    // External queue/admission and initializer group gates. Both are required;
    // this component does not invent the native queue or group registry.
    std::function<bool(std::uint32_t, const NativeHitState&)> admission;
    std::function<bool(std::uint32_t, const NativeHitState&)> initializer_group;
    // Called only for positive R1. Missing data refuses Play before insertion;
    // no residual-output emulation or World-generation inference occurs.
    std::function<std::optional<HitSourceControls>(
        std::uint32_t track_id, std::int32_t source, const NativeHitState&)> source;
};
enum class HitTrackPlayStatus { dispatched, admission_refused, zero_source,
                                capacity_refused, source_unavailable };
struct HitTrackPlayResult {
    HitTrackPlayStatus status;
    std::uint32_t track_id;
    HitArguments arguments;
    bool contribution_inserted = false;
    bool initializer_enabled = false;
};

// Owns one explicitly registered controller per TrackID plus the independent
// source table. Immutable catalog/data and callback captures must outlive this
// manager and its controllers. register_track calls its factory once, at an
// explicit load boundary; Play never creates a controller. The factory must
// configure that resolved Track/entry and its owning HOT with global Patch
// callbacks, resource resolver and explicit controller budgets. This manager
// does not validate audio providers or reproduce native loading/replacement.
//
// Storage is reserved at construction: O(controllers + source_pairs), with
// two source scratch arrays. Controller/provider allocations are additional
// and belong to the factory's declared budgets. No global scheduler, automatic
// polling, World identity/generation, geometry or physical audio is implied.
// Callback/execution exceptions latch a manager fault and retain partial
// effects for inspection; no later mutation is allowed. Destruction follows
// NativeHitController's external audio-cleanup contract (no implicit effects).
class HitTrackManager {
public:
    using Factory = std::function<std::unique_ptr<NativeHitController>(
        const ResolvedGlobalHitTrack&)>;
    HitTrackManager(const NativeHitObjectCatalog&, HitTrackManagerProviders,
                    HitTrackManagerLimits);
    HitTrackManager(const HitTrackManager&) = delete;
    HitTrackManager& operator=(const HitTrackManager&) = delete;
    // Invalid/duplicate/over-budget registration refuses before the factory
    // without faulting. Once the factory begins, callback failures still latch.
    void register_track(std::uint32_t track_id, const Factory&);
    // Type1 Play dispatch applies raw aggregate controls. The service+0x114
    // decrement belongs to refresh/removal, and is not an input to Play.
    // Invalid event/Track/registration/arguments are refused before effects
    // without faulting the manager.
    HitTrackPlayResult play(std::size_t event_group, std::string_view event,
                           const HitArguments& caller, std::uint32_t clock);
    // Erase one pair, apply that track's effect, restart the source search.
    // ODH insertion order is deterministic but not native hash order.
    // For refresh/removal only, adjustment is the explicitly supplied
    // service+0x114 != 0 condition: decrement modulo32 then negative->1.
    // No cadence/lifecycle for that external service field is inferred.
    std::size_t remove_source(std::int32_t source, bool adjustment);
    void refresh_track(std::uint32_t track_id, bool adjustment);
    NativeHitPoll poll_track(std::uint32_t track_id, std::uint32_t clock);
    void stop_track(std::uint32_t track_id);
    const NativeHitController* controller(std::uint32_t track_id) const noexcept;
    std::size_t registered_count() const noexcept { return tracks_.size(); }
    std::size_t source_count(std::uint32_t track_id) const noexcept;
    bool contains_source(std::uint32_t track_id, std::int32_t source) const noexcept;
    bool faulted() const noexcept { return faulted_; }
private:
    struct Track { std::uint32_t id; std::unique_ptr<NativeHitController> controller; };
    const NativeHitObjectCatalog& catalog_;
    HitTrackManagerProviders providers_;
    HitTrackManagerLimits limits_;
    std::vector<Track> tracks_;
    HitSourceContributions sources_;
    std::vector<HitSourcePair> pairs_;
    std::vector<HitSourceControls> controls_;
    bool busy_ = false, faulted_ = false;
    void enter();
    void check_live() const;
    NativeHitController& require_track(std::uint32_t);
    std::optional<HitSourceAggregate> aggregate(std::uint32_t, const NativeHitState&,
                                               std::optional<std::int32_t> extra,
                                               bool adjustment);
    void refresh_impl(std::uint32_t, bool adjustment);
};
}
