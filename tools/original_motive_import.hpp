// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "odh/world.hpp"
#include "odh/objm_tail.hpp"
#include "odh/ts1_simulation.hpp"
#include "odh/vm_snapshot_codec.hpp"
#include <cmath>
#include <cstring>
#include <functional>
#include <limits>
#include <optional>
#include <string>
#include <tuple>

namespace odh_host {
// Original ConvertAvatar at FreeSO7e76, not a native cold-snapshot restorer.
// The source constructs new MotiveChanges/Decay: fractions, Ticked and decay
// bucket are zero, irrespective of SIMI minutes. Existing real clock is kept.
class OriginalMotiveImportPlan {
public:
    OriginalMotiveImportPlan(OriginalMotiveImportPlan&&)=default;
    OriginalMotiveImportPlan& operator=(OriginalMotiveImportPlan&&)=default;
    OriginalMotiveImportPlan(const OriginalMotiveImportPlan&)=delete;
    OriginalMotiveImportPlan& operator=(const OriginalMotiveImportPlan&)=delete;
    static std::optional<OriginalMotiveImportPlan> prepare(odh::World& world,
            odh::WorldEntityReference ref,odh::Vm& vm,odh::Ts1MotiveDecay& decay,
            const odh::Ts1Clock& clock,const odh::ObjmPersonState& source,
            const odh::VmSnapshot& prospective,std::function<bool()> authority,std::string& error){
        const auto fail=[&](const char* why)->std::optional<OriginalMotiveImportPlan>{error=why;return {};};
        const auto* actor=world.resolve_reference(ref);const auto before=vm.snapshot();
        if(!authority||!authority()||!world.is_live_reference(ref)||!actor||actor->dead||actor->kind!=odh::EntityKind::avatar||
           actor->motives.size()!=16||actor->person_data.size()<=27||vm.dispatch_active()||
           (before.status!=odh::VmStatus::running&&before.status!=odh::VmStatus::ready)||
           before.steps||before.ticket_sequence||before.pending||!before.suspended_pending.empty()||
           before.interaction_return||before.external_frame_return||!before.immediate_continuations.empty()||before.immediate_return||
           before.run_generation==UINT64_MAX||clock.snapshot().ticks||
           prospective.steps||prospective.run_generation!=before.run_generation+1||
           prospective.pending||!prospective.suspended_pending.empty()||
           source.motive_delta_count<0||std::size_t(source.motive_delta_count)!=source.motive_deltas.size()||
           source.motive_deltas.size()>4096)
            return fail("original motive import lacks its actual undispatched avatar/clock/source bank");
        OriginalMotiveImportPlan plan;plan.world_=&world;plan.ref_=ref;plan.vm_=&vm;plan.decay_=&decay;plan.clock_=&clock;
        plan.authority_=std::move(authority);plan.before_vm_=odh::encode_vm_snapshot(before);
        plan.after_vm_=odh::encode_vm_snapshot(prospective);plan.clock_before_=clock.snapshot();plan.decay_before_=decay.snapshot();
        plan.motives_=actor->motives;plan.person_data_=actor->person_data;plan.before_changes_=actor->motive_changes;
        // new VMMotiveChange has MaxValue0, unlike runtime Clear()'s32767.
        for(auto& change:plan.changes_)change.maximum=0;
        for(const auto& delta:source.motive_deltas){
            if(delta.motive<0||delta.motive>=16)return fail("original motive delta index is outside source bank16");
            const auto input=binary32(delta.tick_delta_bits),limit=binary32(delta.stop_at_bits);
            // C# float multiplication rounds to binary32 BEFORE Math.Round
            // promotes to double. No Energy ScaleRate or primitive29 is run.
            const volatile float product=input*1800.0f;
            if(!std::isfinite(input)||!std::isfinite(limit)||!std::isfinite(product))
                return fail("original motive delta is not finite");
            const double lower=std::floor(double(product)),fraction=double(product)-lower;
            const double rounded=lower+((fraction>0.5||(fraction==0.5&&std::fmod(std::abs(lower),2.0)!=0.0))?1.0:0.0);
            const double cap=std::trunc(double(limit));
            // Outside the representable short domain the source's unchecked
            // float conversion is platform-dependent. Do not manufacture a
            // portable wrap/NaN result for that unproved source profile.
            if(rounded<-32768.0||rounded>32767.0||cap<-32768.0||cap>32767.0)
                return fail("original motive delta requires unproved out-of-range unchecked short conversion");
            auto& change=plan.changes_[std::size_t(delta.motive)];
            change.maximum=static_cast<std::int16_t>(cap);change.per_hour_change=static_cast<std::int16_t>(rounded);
        }
        if(!plan.current(error))return {};
        return std::optional<OriginalMotiveImportPlan>{std::move(plan)};
    }
    bool current(std::string& error) const {
        if(!current_values(error))return false;
        if(odh::encode_vm_snapshot(vm_->snapshot())!=before_vm_){error="original motive Thread preimage changed";return false;}
        return true;
    }
    bool commit(const odh::VmSnapshot& actual,std::string& error){
        if(!current_values(error))return false;
        if(odh::encode_vm_snapshot(actual)!=after_vm_||odh::encode_vm_snapshot(vm_->snapshot())!=after_vm_){
            error="original motive publication lost its admitted actual Thread";return false;}
        if(!world_->publish_original_motive_changes(ref_,before_changes_,changes_)){
            error="actual World rejected original motive bank publication";return false;}
        // This exact new source state was validated by Ts1MotiveDecay's existing
        // restore contract. No tick, minute rebase or provider callback occurs.
        if(!decay_->restore(odh::Ts1MotiveSnapshot{})){
            error="actual motive decay rejected source defaults; discard candidate";return false;}
        consumed_=true;return true;
    }
private:
    OriginalMotiveImportPlan()=default;
    static float binary32(std::uint32_t bits){
        static_assert(sizeof(float)==4&&std::numeric_limits<float>::is_iec559,"source binary32 required");
        float value;std::memcpy(&value,&bits,sizeof(value));return value;
    }
    static bool same_clock(const odh::Ts1ClockSnapshot& a,const odh::Ts1ClockSnapshot& b){
        return std::tie(a.ticks,a.minute_fraction,a.minute,a.hour,a.day,a.month,a.year,a.fire_percent)==
            std::tie(b.ticks,b.minute_fraction,b.minute,b.hour,b.day,b.month,b.year,b.fire_percent);
    }
    bool current_values(std::string& error) const {
        const auto fail=[&](const char* why){error=why;return false;};
        if(consumed_||!world_||!vm_||vm_->dispatch_active()||!authority_||!authority_())
            return fail("original motive import authority expired");
        const auto* actor=world_->resolve_reference(ref_);
        if(!world_->is_live_reference(ref_)||!actor||actor->dead||actor->kind!=odh::EntityKind::avatar||actor->motives!=motives_||actor->person_data!=person_data_||
           actor->motive_changes.size()!=before_changes_.size()||!same_clock(clock_->snapshot(),clock_before_)||
           decay_->snapshot().last_two_minute_bucket!=decay_before_.last_two_minute_bucket||
           decay_->snapshot().fractions!=decay_before_.fractions)
            return fail("original motive avatar/banks/decay/clock preimage changed");
        for(std::size_t i=0;i<before_changes_.size();++i){const auto& a=actor->motive_changes[i];const auto& b=before_changes_[i];
            if(a.per_hour_change!=b.per_hour_change||a.maximum!=b.maximum||a.fractional!=b.fractional||
               std::signbit(a.fractional)!=std::signbit(b.fractional)||a.ticked!=b.ticked)
                return fail("original motive controller preimage changed");}
        return true;
    }
    odh::World* world_=nullptr;odh::WorldEntityReference ref_{};odh::Vm* vm_=nullptr;
    odh::Ts1MotiveDecay* decay_=nullptr;const odh::Ts1Clock* clock_=nullptr;
    std::function<bool()> authority_;bool consumed_=false;
    odh::Ts1ClockSnapshot clock_before_{};odh::Ts1MotiveSnapshot decay_before_{};
    std::vector<std::int16_t> motives_,person_data_;std::vector<odh::MotiveChangeState> before_changes_;
    std::array<odh::MotiveChangeState,16> changes_{};
    std::vector<std::uint8_t> before_vm_,after_vm_;
};
} // namespace odh_host
