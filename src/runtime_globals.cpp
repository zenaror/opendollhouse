// SPDX-License-Identifier: MPL-2.0
// TS1 VM.Get/SetGlobalValue and old/new VMTS1Activator, pinned FreeSO.
#include "odh/runtime_globals.hpp"
namespace odh {
namespace {
bool source_extent_valid(std::size_t size){return size==32||size==64;}
bool clock_index(std::int16_t index){return index==0||index==1||(index>=4&&index<=8);}
bool live_avatar(const World& world,RuntimeGlobalAvatar avatar){
    const auto* entity=world.find(avatar.id);
    return avatar.id>0&&avatar.generation&&entity&&entity->kind==EntityKind::avatar&&entity->generation==avatar.generation;
}
}
RuntimeGlobals::RuntimeGlobals(const std::vector<std::int16_t>& simi,Ts1GlobalActivation activation,
    std::int16_t selected_lot){
    if(!source_extent_valid(simi.size()))throw FormatError("runtime globals require exact SIMI32/64 bank");
    if(activation!=Ts1GlobalActivation::saved_new&&activation!=Ts1GlobalActivation::legacy_old)
        throw FormatError("unknown TS1 global activation profile");
    if(selected_lot<=0)throw FormatError("runtime globals require positive selected lot");
    state_.words.assign(38,std::int16_t{0});
    for(std::size_t n=0;n<simi.size()&&n<state_.words.size();++n)state_.words[n]=simi[n];
    state_.words[17]=4;state_.words[20]=255;state_.words[25]=4;
    if(activation==Ts1GlobalActivation::legacy_old){
        state_.words[3]=0;state_.words[9]=0;state_.words[10]=selected_lot;
        state_.words[32]=0;state_.words[33]=2;
    }
    state_.selected_sim_is_runtime_id=*state_.words[3]==0;
}
RuntimeGlobals RuntimeGlobals::from_legacy(const LotContext::Snapshot& legacy){
    RuntimeGlobalsSnapshot state;state.words.assign(legacy.begin(),legacy.end());
    // Native LotContext never stored a selected Sim. A supplied value, however,
    // is a native runtime ID, unlike a fresh commercial SIMI object index.
    state.selected_sim_is_runtime_id=bool(state.words[3]);
    return RuntimeGlobals(std::move(state));
}
std::optional<std::int16_t> RuntimeGlobals::read(std::int16_t index,const Ts1Clock* clock) const {
    if(index<0||static_cast<std::size_t>(index)>=state_.words.size())return {};
    if(clock_index(index))return clock?clock->global(index):std::nullopt;
    if(index==3&&!state_.selected_sim_is_runtime_id)return {};
    return state_.words[static_cast<std::size_t>(index)];
}
bool RuntimeGlobals::write(std::int16_t index,std::int16_t value){
    if(index<0||static_cast<std::size_t>(index)>=state_.words.size())return false;
    state_.words[static_cast<std::size_t>(index)]=value;
    if(index==3)state_.selected_sim_is_runtime_id=true;
    return true;
}
bool RuntimeGlobals::resolve_imported_selection(const World& world,std::int16_t source_id,RuntimeGlobalAvatar avatar){
    if(state_.selected_sim_is_runtime_id||!state_.words[3]||*state_.words[3]!=source_id||!source_id||!live_avatar(world,avatar))return false;
    state_.words[3]=avatar.id;state_.selected_sim_is_runtime_id=true;return true;
}
bool RuntimeGlobals::select_avatar(const World& world,RuntimeGlobalAvatar avatar){
    if(!live_avatar(world,avatar))return false;
    state_.words[3]=avatar.id;state_.selected_sim_is_runtime_id=true;return true;
}
void RuntimeGlobals::clear_selection(){state_.words[3]=0;state_.selected_sim_is_runtime_id=true;}
bool RuntimeGlobals::restore(const RuntimeGlobalsSnapshot& snapshot){
    if(snapshot.words.size()!=38||
       (snapshot.selected_sim_is_runtime_id&&!snapshot.words[3]))return false;
    auto replacement=snapshot;state_=std::move(replacement);return true;
}
}
