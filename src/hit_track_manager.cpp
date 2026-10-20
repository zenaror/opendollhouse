#include "odh/hit_track_manager.hpp"
#include <algorithm>
#include <limits>
#include <utility>

namespace odh {
namespace {
void require(bool value, const char* reason) {
    if (!value) throw FormatError(reason);
}
std::int32_t signed_bits(std::uint32_t value) {
    return value <= static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max())
        ? static_cast<std::int32_t>(value) : -1 - static_cast<std::int32_t>(~value);
}
}
HitTrackManager::HitTrackManager(const NativeHitObjectCatalog& catalog,
        HitTrackManagerProviders providers, HitTrackManagerLimits limits)
    : catalog_(catalog), providers_(std::move(providers)), limits_(limits),
      sources_(limits.source_pairs) {
    require(limits.controllers && limits.controllers <= HitSourceContributions::hard_limit,
            "HIT track manager: invalid controller budget");
    require(providers_.admission && providers_.initializer_group && providers_.source,
            "HIT track manager: complete explicit providers required");
    tracks_.reserve(limits.controllers);
    pairs_.resize(limits.source_pairs);
    controls_.resize(limits.source_pairs);
}
void HitTrackManager::check_live() const {
    require(!faulted_, "HIT track manager is faulted");
}
void HitTrackManager::enter() {
    if (busy_) {
        faulted_ = true;
        throw FormatError("HIT track manager: callback reentered mutation");
    }
    check_live();
    busy_ = true;
}
const NativeHitController* HitTrackManager::controller(std::uint32_t id) const noexcept {
    const auto found = std::find_if(tracks_.begin(), tracks_.end(),
        [=](const auto& track) { return track.id == id; });
    return found == tracks_.end() ? nullptr : found->controller.get();
}
NativeHitController& HitTrackManager::require_track(std::uint32_t id) {
    const auto found = std::find_if(tracks_.begin(), tracks_.end(),
        [=](const auto& track) { return track.id == id; });
    require(found != tracks_.end(), "HIT track manager: TrackID not registered");
    return *found->controller;
}
void HitTrackManager::register_track(std::uint32_t id, const Factory& factory) {
    enter();
    const auto resolved = [&]() -> ResolvedGlobalHitTrack {
        try {
            require(!controller(id), "HIT track manager: TrackID already registered");
            require(tracks_.size() < limits_.controllers,
                    "HIT track manager: controller budget exhausted");
            require(static_cast<bool>(factory), "HIT track manager: factory required");
            return catalog_.resolve_track(id);
        } catch (...) { busy_ = false; throw; }
    }();
    try {
        auto created = factory(resolved);
        check_live();
        require(created && !created->state().active && !created->state().faulted,
                "HIT track manager: factory must return a prepared inactive controller");
        tracks_.push_back({id, std::move(created)});
        busy_ = false;
    } catch (...) { faulted_ = true; busy_ = false; throw; }
}
std::optional<HitSourceAggregate> HitTrackManager::aggregate(std::uint32_t id,
        const NativeHitState& snapshot, std::optional<std::int32_t> extra,
        bool adjustment) {
    auto count = sources_.copy_track(id, pairs_.data(), pairs_.size());
    if (extra) {
        require(count < pairs_.size(), "HIT track manager: aggregate scratch exhausted");
        pairs_[count++] = {id, *extra};
    }
    for (std::size_t index = 0; index < count; ++index) {
        const auto source = pairs_[index].source_r1;
        if (source <= 0) controls_[index] = {1024, 512};
        else {
            const auto sample = providers_.source(id, source, snapshot);
            check_live();
            if (!sample) return std::nullopt;
            controls_[index] = *sample;
        }
    }
    auto result = aggregate_hit_source_controls(controls_.data(), count);
    if (adjustment) {
        result.external_volume = signed_bits(
            static_cast<std::uint32_t>(result.external_volume) - 1U);
        if (result.external_volume < 0) result.external_volume = 1;
    }
    return result;
}
HitTrackPlayResult HitTrackManager::play(std::size_t group, std::string_view name,
        const HitArguments& caller, std::uint32_t clock) {
    enter();
    struct Input {
        std::uint32_t id;
        HitArguments args;
        NativeHitController* track;
    };
    // Immutable lookup/precondition refusal has no callback or runtime effect.
    // Keep the manager usable; execution/provider failures below still latch.
    const auto input = [&]() -> Input {
        try {
            const auto event = catalog_.resolve_global_track_event(group, name);
            const auto args = resolve_hit_arguments(*event.event, caller);
            const auto id = event.track.object.id;
            auto& track = require_track(id);
            require(event.track.object.track->arg_type &&
                    *event.track.object.track->arg_type == 2,
                    "HIT track manager: only ArgStyle2 supported");
            return {id, args, &track};
        } catch (...) { busy_ = false; throw; }
    }();
    try {
        const auto id = input.id;
        const auto& args = input.args;
        auto& track = *input.track;
        HitTrackPlayResult result{HitTrackPlayStatus::dispatched, id, args};
        if (!track.state().active) sources_.clear_track(id);
        const NativeHitState before_admission = track.state();
        const bool admitted = providers_.admission(id, before_admission);
        check_live();
        if (!admitted) result.status = HitTrackPlayStatus::admission_refused;
        else if (!args[0]) result.status = HitTrackPlayStatus::zero_source;
        else {
            const bool exists = sources_.contains(id, args[0]);
            if (!exists && sources_.size() == sources_.capacity())
                result.status = HitTrackPlayStatus::capacity_refused;
            else {
                const auto value = aggregate(id, track.state(),
                    exists ? std::nullopt : std::optional<std::int32_t>(args[0]), false);
                if (!value) result.status = HitTrackPlayStatus::source_unavailable;
                else {
                    result.contribution_inserted =
                        sources_.add(id, args[0]) == HitSourceAdd::inserted;
                    const NativeHitState before_group = track.state();
                    result.initializer_enabled = providers_.initializer_group(id, before_group);
                    check_live();
                    track.dispatch_type1(args, value->external_volume, value->pan,
                                         clock, result.initializer_enabled);
                    check_live();
                }
            }
        }
        busy_ = false;
        return result;
    } catch (...) { faulted_ = true; busy_ = false; throw; }
}
void HitTrackManager::refresh_impl(std::uint32_t id, bool adjustment) {
    auto& track = require_track(id);
    if (!sources_.count_track(id)) track.request_fade_stop();
    else {
        const auto value = aggregate(id, track.state(), std::nullopt, adjustment);
        require(value.has_value(), "HIT track manager: remaining positive source unavailable");
        track.set_spatial_controls(value->external_volume, value->pan);
    }
    check_live();
}
std::size_t HitTrackManager::remove_source(std::int32_t source, bool adjustment) {
    enter();
    try {
        std::size_t count = 0;
        while (const auto id = sources_.remove_one_source(source)) {
            ++count;
            refresh_impl(*id, adjustment);
        }
        busy_ = false;
        return count;
    } catch (...) { faulted_ = true; busy_ = false; throw; }
}
void HitTrackManager::refresh_track(std::uint32_t id, bool adjustment) {
    enter();
    try { refresh_impl(id, adjustment); busy_ = false; }
    catch (...) { faulted_ = true; busy_ = false; throw; }
}
NativeHitPoll HitTrackManager::poll_track(std::uint32_t id, std::uint32_t clock) {
    enter();
    try {
        const auto result = require_track(id).poll(clock);
        check_live(); busy_ = false; return result;
    } catch (...) { faulted_ = true; busy_ = false; throw; }
}
void HitTrackManager::stop_track(std::uint32_t id) {
    enter();
    try { require_track(id).stop(); check_live(); busy_ = false; }
    catch (...) { faulted_ = true; busy_ = false; throw; }
}
std::size_t HitTrackManager::source_count(std::uint32_t id) const noexcept {
    return sources_.count_track(id);
}
bool HitTrackManager::contains_source(std::uint32_t id, std::int32_t source) const noexcept {
    return sources_.contains(id, source);
}
}
