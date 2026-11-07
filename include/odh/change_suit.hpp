#pragma once

#include "odh/vm.hpp"
#include "odh/animation.hpp"
#include "odh/world.hpp"
#include <functional>
#include <optional>
#include <string>

namespace odh {
using Ts1ObjectSuitLookup = std::function<std::optional<std::string>(CodeOwner,std::uint16_t)>;
enum class Ts1SuitResourceState { ready, absent, unsupported };
struct Ts1SuitStringResult {
    Ts1SuitResourceState state=Ts1SuitResourceState::unsupported;
    std::string value,diagnostic;
};
struct Ts1PersonSuitStrings {
    std::string body,skin;
    std::array<std::string,6> hands{};
    std::array<std::optional<std::string>,5> expanded{}; // BodySTR30..34, empty and ADDED retained.
};
struct Ts1PersonSuitResult {
    Ts1SuitResourceState state=Ts1SuitResourceState::unsupported;
    Ts1PersonSuitStrings strings;
    std::string diagnostic;
};
struct Ts1JobSuitResult {
    Ts1SuitResourceState state=Ts1SuitResourceState::unsupported;
    std::string male_mesh,female_mesh,texture,diagnostic;
};
struct Ts1SuitServices {
    // update=true selects Callee STR304, otherwise scope0 Global / scope2 CodeOwner.
    std::function<Ts1SuitStringResult(const VmFrame&,std::uint8_t,std::uint16_t,bool)> string;
    std::function<Ts1PersonSuitResult(const World&,std::int16_t)> person;
    std::function<Ts1JobSuitResult(std::int16_t,std::int16_t)> job;
};
struct Ts1PersonSuitResolution {
    Ts1SuitResourceState state=Ts1SuitResourceState::unsupported;
    AvatarOutfitReference outfit;
    std::string diagnostic;
};
Ts1PersonSuitResolution resolve_ts1_person_suit(const World&,std::int16_t,std::uint16_t,const Ts1SuitServices&);
VmPrimitive ts1_change_suit_or_accessory(World&,Ts1SuitServices);
// Legacy exact-object provider adapter. Missing evidence remains explicit.
// Strict TS1 ChangeSuitOrAccessory profile for Object-scope `.apr` bindings.
// Object string resources are supplied by the exact BHAV code owner.
VmPrimitive ts1_change_suit_or_accessory(World&,Ts1ObjectSuitLookup);
}
