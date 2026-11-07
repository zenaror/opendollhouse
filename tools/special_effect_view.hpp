#pragma once

#include "odh/entity_reference.hpp"
#include "odh/projection.hpp"
#include <cmath>
#include <optional>
#include <string>

namespace odh_host {

enum class SpecialEffectVisualKind {object,avatar};
struct SpecialEffectRoomState {
    SpecialEffectVisualKind kind=SpecialEffectVisualKind::object;
    bool positioned=false;
    std::optional<std::int16_t> object_room,flag_field2,lighting;
    std::optional<std::uint32_t> disabled;
};
// WorldUI.Room is not the geometry lookup. VMAvatar.SetRoom uses GetValue(Room),
// whereas VMGameObject.RefreshLight uses raw ObjectData and lighting overrides.
inline std::optional<std::uint16_t> special_effect_component_room(
        const SpecialEffectRoomState& state,std::string* diagnostic=nullptr){
    const auto missing=[&](const char* why)->std::optional<std::uint16_t>{
        if(diagnostic)*diagnostic=why;return {};
    };
    if(state.kind==SpecialEffectVisualKind::avatar){
        if(!state.positioned)return std::uint16_t{65532}; // ushort(GetValue(Room)=-5 + 1)
        if(!state.object_room)return missing("SpecialEffect avatar Room backing is unavailable");
        return static_cast<std::uint16_t>(static_cast<int>(*state.object_room)+1);
    }
    if(!state.flag_field2||!state.disabled)
        return missing("SpecialEffect object FlagField2/Disabled policy is unavailable");
    if(*state.disabled>=4)return std::uint16_t{65533};
    const auto flags=static_cast<std::uint16_t>(*state.flag_field2);
    if(flags&0x400U){
        if(!state.lighting)return missing("SpecialEffect light contribution is unavailable");
        if(*state.lighting>0&&(flags&0xc000U)==0)return std::uint16_t{65535};
    }
    if(!state.object_room)return missing("SpecialEffect object raw Room backing is unavailable");
    return static_cast<std::uint16_t>(static_cast<int>(*state.object_room)+1);
}

// Renderer-owned facts captured at CenterTo, never a logical World relocation.
// Avatars supply their animated pelvis; other entities supply WorldUI.Position.
struct SpecialEffectVisualTarget {
    odh::WorldEntityReference identity;
    std::uint16_t room=0;
    std::uint8_t level=0;
    std::optional<std::array<float,3>> center;
};
class SpecialEffectView {
public:
    bool center_to(odh::WorldEntityReference target,const SpecialEffectVisualTarget& visual,
                   std::string* diagnostic=nullptr) {
        const auto reject=[&](const char* why){if(diagnostic)*diagnostic=why;return false;};
        if(target.id<=0||!target.generation||target.id!=visual.identity.id||
           target.generation!=visual.identity.generation)
            return reject("SpecialEffect visual target identity changed");
        // World.CenterTo's early return precedes position/pelvis access.
        if(visual.room==0||visual.room==65531)return true;
        if(!visual.center)return reject("SpecialEffect visual center/pelvis backend is unavailable");
        if(visual.level<1||visual.level>2)
            return reject("SpecialEffect target level is outside the current renderer profile");
        for(const auto value:*visual.center)if(!std::isfinite(value))
            return reject("SpecialEffect visual center is not finite");
        pending_=visual;focused_=true;return true;
    }
    bool focused() const noexcept{return focused_;}
    // Applying a pending instant center once leaves subsequent manual pan intact.
    // The host must stop its diagnostic avatar-follow when focused() is true.
    std::optional<std::uint8_t> apply(odh::WorldProjection& camera) noexcept {
        if(!pending_)return {};
        camera.center_tile=*pending_->center;
        const auto level=pending_->level;pending_.reset();return level;
    }
private:
    bool focused_=false;
    std::optional<SpecialEffectVisualTarget> pending_;
};
} // namespace odh_host
