// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "odh/playback.hpp"
#include "odh/objm_tail.hpp"
#include "odh/vm_snapshot_codec.hpp"
#include <algorithm>
#include <map>
#include <cmath>

namespace odh_host {
// One original-import activation over the same World/VM/Playback. Source is
// ConvertAvatar/VMAnimationState.Load at exact FreeSO7e76, not ODHS fake waits.
class OriginalPlaybackImportPlan {
public:
    OriginalPlaybackImportPlan(OriginalPlaybackImportPlan&&)=default;
    OriginalPlaybackImportPlan& operator=(OriginalPlaybackImportPlan&&)=default;
    OriginalPlaybackImportPlan(const OriginalPlaybackImportPlan&)=delete;
    OriginalPlaybackImportPlan& operator=(const OriginalPlaybackImportPlan&)=delete;
    using VisualPreparation=std::function<bool(const std::vector<std::string>&,odh::AvatarOutfitReference&,std::string&)>;
    static std::optional<OriginalPlaybackImportPlan> prepare(odh::World& world,odh::WorldEntityReference actor,
        odh::Vm& vm,odh::Playback& playback,const odh::ObjmPersonState& source,const odh::VmSnapshot& prospective,
        std::uint64_t tick,odh::Playback::NamedAnimationResolver resolver,VisualPreparation visuals,std::string& error){
        OriginalPlaybackImportPlan result;result.world_=&world;result.actor_=actor;result.vm_=&vm;result.playback_=&playback;
        result.resolver_=std::move(resolver);result.visuals_=std::move(visuals);
        const auto* body=world.resolve_reference(actor);
        if(!body||!world.is_live_reference(actor)||body->kind!=odh::EntityKind::avatar||!body->definition||vm.dispatch_active()||
           !playback.bound_to(vm)||body->person_data.size()<=65||source.person_data.size()<=65||!result.resolver_||!result.visuals_||
           source.person_data[8]!=body->person_data[8]||source.person_data[58]!=body->person_data[58]||source.person_data[65]!=body->person_data[65]){
            error="original playback body/source identity, age or gender mismatch";return {};}
        const auto direction=body->object_words.find(1);
        if(direction==body->object_words.end()||direction->second<0||direction->second>7){error="original avatar direction is not an eight-notch source value";return {};}
        result.direction_=direction->second;result.before_radians_=body->radian_direction;
        result.source_radians_=float(result.direction_)*float(3.14159265358979323846/4.0);
        result.job_=body->person_data[56];result.job_level_=body->person_data[57];result.current_outfit_=body->person_data[8];result.guid_=body->definition->guid;result.age_=body->person_data[58];result.gender_=body->person_data[65];result.before_accessories_=body->bound_appearances;
        result.before_appearance_=body->avatar_appearance;
        if(source.accessory_count<0||std::size_t(source.accessory_count)!=source.accessories.size()||source.accessories.size()>16){
            error="original appearance accessory count exceeds configured native visual budget";return {};}
        for(const auto& accessory:source.accessories){const auto& name=accessory.name.bytes;
            if(name.empty()||name.size()>256||std::any_of(name.begin(),name.end(),[](unsigned char c){return !((c>='a'&&c<='z')||
                (c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='_'||c=='.');})){
                error="original appearance accessory name is not bounded native content";return {};}
            result.accessories_.push_back(name);
        }
        std::sort(result.accessories_.begin(),result.accessories_.end());
        result.accessories_.erase(std::unique(result.accessories_.begin(),result.accessories_.end()),result.accessories_.end());
        try{result.before_vm_=odh::encode_vm_snapshot(vm.snapshot());result.target_vm_=odh::encode_vm_snapshot(prospective);}
        catch(const std::exception& e){error=e.what();return {};}
        if(!playback.capture_snapshot(result.before_playback_,&error))return {};
        if(!world.evaluate_appearance_lookup([&]{if(!result.visuals_(result.accessories_,result.outfit_,error))throw odh::FormatError(error);})){
            if(error.empty())error="original visual resources did not prepare safely";return {};}
        const auto names=[&](const std::string& name){auto clip=result.resolver_(name);
            if(clip)result.clips_.emplace(name,clip);return clip;};
        auto state=playback.prepare_original_import(actor,prospective,source.animation.bytes,source.carry_animation.bytes,
            source.animation_event_count,tick,names,error);
        if(!state)return {};result.playback_state_=std::move(*state);
        if(!result.current(error))return {};return std::optional<OriginalPlaybackImportPlan>{std::move(result)};
    }
    bool current(std::string& error)const{
        if(consumed_||!identity()||vm_->dispatch_active()){error="original playback import preimage expired";return false;}
        try{if(odh::encode_vm_snapshot(vm_->snapshot())!=before_vm_){error="original playback source VM changed";return false;}}
        catch(const std::exception& e){error=e.what();return false;}
        odh::PlaybackSnapshot now;if(!playback_->capture_snapshot(now,&error)||!same(before_playback_,now)){
            error="original playback source controller changed";return false;}
        return resources_current(error)&&identity();
    }
    bool commit(const odh::VmSnapshot& actual,std::string& error){
        if(consumed_||!identity()||vm_->dispatch_active()){error="original playback commit identity expired";return false;}
        if(!resources_current(error))return false;
        consumed_=true;
        try{if(odh::encode_vm_snapshot(actual)!=target_vm_||odh::encode_vm_snapshot(vm_->snapshot())!=target_vm_){
            error="original playback commit does not match the actual typed VM";return false;}}
        catch(const std::exception& e){error=e.what();return false;}
        const auto resolve=[&](const odh::PlaybackResourceKey& key)->std::shared_ptr<const odh::AnimationHeader>{
            if(key.kind!=odh::PlaybackResourceKey::Kind::original_animation)return {};
            const auto found=clips_.find(key.original_name);if(found==clips_.end())return {};
            const auto live=resolver_(key.original_name);return live==found->second?live:nullptr;};
        if(!playback_->restore_snapshot(playback_state_,*vm_,resolve,&error))return false;
        // ConvertAvatar owns BoundAppearances; body/head follow LoadCrossRef's
        // existing TS1 CurrentOutfit/BodySTR consumers, never raw DTO overrides.
        if(!world_->clear_bound_appearances(actor_.id)){error="original accessory clear failed; discard candidate";return false;}
        for(const auto& accessory:accessories_)if(!world_->add_bound_appearance(actor_.id,accessory)){
            error="original accessory publication failed; discard candidate";return false;}
        if(!world_->set_avatar_outfit(actor_.id,outfit_,false,current_outfit_)){
            error="original CurrentOutfit publication failed; discard candidate";return false;}
        if(!world_->set_radian_direction(actor_.id,source_radians_)){
            error="original direction publication failed; discard candidate";return false;}
        error.clear();return true;
    }
private:
    OriginalPlaybackImportPlan()=default;
    static bool same_clip(const odh::PlaybackClipSnapshot& a,const odh::PlaybackClipSnapshot& b){
        return a.resource==b.resource&&a.actor==b.actor&&a.actor_generation==b.actor_generation&&a.vm_generation==b.vm_generation&&
            a.frame==b.frame&&a.speed==b.speed&&a.frames==b.frames&&a.backwards==b.backwards&&a.end==b.end&&a.loop==b.loop&&
            a.low==b.low&&a.high==b.high&&a.queue==b.queue&&a.events_run==b.events_run&&a.ticks==b.ticks&&
            a.weight==b.weight&&a.original_import==b.original_import&&bool(a.wait)==bool(b.wait)&&
            (!a.wait||(a.wait->ticket==b.wait->ticket&&a.wait->bank==b.wait->bank&&a.wait->index==b.wait->index&&a.wait->expected==b.wait->expected));
    }
    static bool same(const odh::PlaybackSnapshot& a,const odh::PlaybackSnapshot& b){
        if(a.head_seek!=b.head_seek||bool(a.state)!=bool(b.state)||!(a.carry==b.carry)||bool(a.hands)!=bool(b.hands)||a.last_tick!=b.last_tick||
           bool(a.source_walk)!=bool(b.source_walk)||bool(a.original_carry)!=bool(b.original_carry))return false;
        if(a.state&&!same_clip(*a.state,*b.state))return false;
        if(a.hands&&(a.hands->actor!=b.hands->actor||a.hands->actor_generation!=b.hands->actor_generation||a.hands->vm_generation!=b.hands->vm_generation||
            a.hands->value.left!=b.hands->value.left||a.hands->value.right!=b.hands->value.right))return false;
        if(a.original_carry&&!same_clip(*a.original_carry,*b.original_carry))return false;
        if(a.source_walk){if(a.source_walk->weights!=b.source_walk->weights)return false;
            for(unsigned n=0;n<2;++n)if(!same_clip(a.source_walk->secondary[n],b.source_walk->secondary[n]))return false;}
        return true;
    }
    bool resources_current(std::string& error)const{
        for(const auto& clip:clips_){std::shared_ptr<const odh::AnimationHeader> live;
            if(!world_->evaluate_appearance_lookup([&]{live=resolver_(clip.first);})||live!=clip.second){
                error="original playback immutable catalog resource changed";return false;}}
        odh::AvatarOutfitReference outfit;
        if(!world_->evaluate_appearance_lookup([&]{if(!visuals_(accessories_,outfit,error))throw odh::FormatError(error);})||outfit!=outfit_){
            if(error.empty())error="original avatar visual resource/outfit selection changed";return false;}
        return true;
    }
    bool identity()const{const auto* body=world_->resolve_reference(actor_);return body&&world_->is_live_reference(actor_)&&
        body->radian_direction==before_radians_&&body->object_words.count(1)&&body->object_words.at(1)==direction_&&
        body->kind==odh::EntityKind::avatar&&body->definition&&body->definition->guid==guid_&&!body->ui_ghost&&body->person_data.size()>65&&body->person_data[58]==age_&&
        body->person_data[65]==gender_&&body->person_data[8]==current_outfit_&&body->person_data[56]==job_&&body->person_data[57]==job_level_&&body->bound_appearances==before_accessories_&&body->avatar_appearance==before_appearance_;}
    odh::World* world_=nullptr;odh::WorldEntityReference actor_{};odh::Vm* vm_=nullptr;odh::Playback* playback_=nullptr;
    std::int16_t direction_=0;float before_radians_=0,source_radians_=0;
    std::uint32_t guid_=0;std::int16_t age_=0,gender_=0,current_outfit_=0,job_=0,job_level_=0;bool consumed_=false;
    odh::Playback::NamedAnimationResolver resolver_;VisualPreparation visuals_;
    std::map<std::string,std::shared_ptr<const odh::AnimationHeader>> clips_;
    std::vector<std::uint8_t> before_vm_,target_vm_;
    odh::PlaybackSnapshot before_playback_,playback_state_;
    std::vector<std::string> before_accessories_,accessories_;
    std::optional<odh::AvatarAppearanceState> before_appearance_;
    odh::AvatarOutfitReference outfit_;
};
} // namespace odh_host
