// SPDX-License-Identifier: MPL-2.0
#include "native_light_cache.hpp"
#include "session_room_lighting.hpp"
#include <algorithm>

namespace odh_host {
unsigned NativeLightOffsetCell::count()const noexcept{return (offsets[0]!=127)+(offsets[2]!=127);}
void NativeLightOffsetCell::insert(std::int8_t y,std::int8_t x)noexcept{
    const auto n=count();
    if(!n){offsets[0]=y;offsets[1]=x;return;}
    if(n==1){offsets[2]=y;offsets[3]=x;return;}
    const int distance=int(y)*y+int(x)*x;
    if(distance<int(offsets[0])*offsets[0]+int(offsets[1])*offsets[1]){offsets[0]=y;offsets[1]=x;}
    else if(distance<int(offsets[2])*offsets[2]+int(offsets[3])*offsets[3]){offsets[2]=y;offsets[3]=x;}
}
void NativeLightOffsetCell::remove(std::int32_t y,std::int32_t x)noexcept{
    const auto n=count();
    if(n==1){if(offsets[0]==y&&offsets[1]==x)offsets.fill(127);return;}
    if(n!=2)return;
    if(offsets[0]==y&&offsets[1]==x){offsets[0]=offsets[2];offsets[1]=offsets[3];}
    else if(offsets[2]!=y||offsets[3]!=x)return;
    offsets[2]=offsets[3]=127;
}
std::array<std::optional<std::array<int,2>>,2> NativeLightOffsetCell::sources(int x,int y)const noexcept{
    std::array<std::optional<std::array<int,2>>,2> result{};
    for(unsigned i=0;i<2;++i)if(offsets[i*2]!=127)result[i]=std::array<int,2>{{x-offsets[i*2+1],y-offsets[i*2]}};
    return result;
}
BasicLightRemovalOwner::BasicLightRemovalOwner(odh::World& world,SessionRoomLighting& controller,
    State state,Rooms rooms,Registry registry,std::function<bool()> authority)
    :world_(world),controller_(controller),state_(std::move(state)),rooms_(std::move(rooms)),
      registry_(std::move(registry)),authority_(std::move(authority)){}
std::optional<BasicLightRemovalOwner::Evidence> BasicLightRemovalOwner::inspect(
    Ref ref,NativeLightRemovalProfile profile,std::string& error)const{
    const auto fail=[&](const char* why)->std::optional<Evidence>{error=why;return {};};
    if(profile!=NativeLightRemovalProfile::basic_room_color)return fail("light removal renderer profile is unproven");
    if(!authority_||!authority_())return fail("light removal original-source authority expired");
    if(!state_||!rooms_||!registry_)return fail("light removal actual owner unavailable");
    // Resolve callbacks first; never retain borrowed entity pointers across one.
    auto* state=state_();const auto* rooms=rooms_();const auto* registry=registry_();
    if(!authority_())return fail("light removal source expired resolving owners");
    if(!state||!state->position_registry||!state->lighting||registry!=&*state->position_registry||
        controller_.current()!=&*state->lighting)return fail("light removal has no prepared actual lighting owner");
    if(!rooms||!rooms->light_base_rooms_resolved||!rooms->floor_resources_resolved||!registry->complete||
        registry->width!=rooms->width||registry->height!=rooms->height||registry->rooms.size()!=rooms->rooms.size())
        return fail("light removal complete room registry unavailable");
    const auto* entity=world_.resolve_reference(ref);
    if(!entity||entity->kind!=odh::EntityKind::object||entity->dead||entity->ui_ghost||!entity->position)
        return fail("light removal live represented object unavailable");
    const auto light=entity->object_words.find(16),rawroom=entity->object_words.find(29);
    if(light==entity->object_words.end()||light->second!=1||rawroom==entity->object_words.end())
        return fail("light removal source Light16/room unknown or unsupported");
    if(entity->object_words.find(40)==entity->object_words.end()||entity->object_words.find(51)==entity->object_words.end())
        return fail("light removal actual visual classification words unavailable");
    const auto pose=*entity->position;
    if(!(world_.native_position(ref)==entity->position))return fail("light removal native and represented poses differ");
    const auto room=odh::geometric_room_at_runtime(*rooms,pose.x,pose.y,pose.level);
    if(!room||!*room||*room>=rooms->rooms.size()||int(rawroom->second)+1!=*room||registry->rooms[*room].id!=*room)
        return fail("light removal actual old room is unresolved");
    const odh::ObjectPositionIdentity identity{ref.id,ref.generation};
    const auto& members=registry->rooms[*room].entities;
    if(std::find_if(members.begin(),members.end(),[&](auto id){return id.id==identity.id&&id.generation==identity.generation;})==members.end())
        return fail("light removal source absent from actual old-room registry");
    if(!odh::room_lighting_for_room(*state->lighting,*rooms,*room))return fail("light removal actual room lighting was not prepared");
    const bool queued=std::find(registry->deferred_lighting.begin(),registry->deferred_lighting.end(),*room)!=registry->deferred_lighting.end();
    error.clear();return Evidence{pose,*room,queued};
}
bool BasicLightRemovalOwner::supports(Ref ref,NativeLightRemovalProfile profile,std::string& error)const{
    return bool(inspect(ref,profile,error));
}
std::optional<BasicLightRemovalReceipt> BasicLightRemovalOwner::remove_before_oow(
    Ref ref,const odh::EntityPosition& old,NativeLightRemovalProfile profile,std::string& error){
    const auto before=inspect(ref,profile,error);if(!before)return {};
    if(!(before->pose==old)){error="light removal supplied old pose changed";return {};}
    controller_.refresh({ref.id,ref.generation});
    const auto after=inspect(ref,profile,error);if(!after)return {};
    if(!(after->pose==old)||after->room!=before->room||!after->queued){
        error="light removal did not queue actual old room before physical unregister";return {};
    }
    return BasicLightRemovalReceipt{ref,old,before->room,before->queued};
}
}
