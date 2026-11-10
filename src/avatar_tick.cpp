// SPDX-License-Identifier: MPL-2.0
// Counter update from VMAvatar.Tick at FreeSO7e76c1960252ddd033d8937d5edb47d326f5b69c.
#include "odh/avatar_tick.hpp"

namespace odh {
bool tick_ts1_avatar_counter(World& world,WorldEntityReference ref) {
    const auto* actor=world.resolve_reference(ref);
    if(!world.is_live_reference(ref)||!actor||actor->dead||actor->kind!=EntityKind::avatar||actor->person_data.size()<=27)return false;
    const auto bits=static_cast<std::uint16_t>(static_cast<std::uint16_t>(actor->person_data[27])+1U);
    const auto value=static_cast<std::int16_t>(bits<=32767U?static_cast<std::int32_t>(bits):
        static_cast<std::int32_t>(bits)-65536);
    return world.set_person_data(ref.id,27,value);
}
bool tick_ts1_avatar_counters(World& world,std::size_t avatar_budget) {
    constexpr std::size_t maximum_avatars=4096;
    if(avatar_budget>maximum_avatars)return false;
    // No bank growth, provider calls or allocation during either pass. World
    // is single-threaded; field27 cannot invoke the CurrentOutfit resolver.
    std::size_t count=0;
    for(auto id=world.next_entity(0,EntityKind::avatar,true);id;
        id=world.next_entity(*id,EntityKind::avatar,true)) {
        const auto* actor=world.find(*id);
        if(!actor||actor->person_data.size()<=27||count>=avatar_budget)return false;
        ++count;
    }
    for(auto id=world.next_entity(0,EntityKind::avatar,true);id;
        id=world.next_entity(*id,EntityKind::avatar,true)) {
        const auto* actor=world.find(*id);
        const auto bits=static_cast<std::uint16_t>(static_cast<std::uint16_t>(actor->person_data[27])+1U);
        const auto value=static_cast<std::int16_t>(bits<=32767U?static_cast<std::int32_t>(bits):
            static_cast<std::int32_t>(bits)-65536);
        // The only possible rejection after preflight is a World callback
        // guard, which rejects the first write before any state is changed.
        if(!world.set_person_data(*id,27,value))return false;
    }
    return true;
}
}
