/* SPDX-License-Identifier: MPL-2.0 */
#pragma once
#include "files/formats/iff/chunks/FAMI.h"
#include "files/formats/iff/chunks/NBRS.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include "odh/named_float_constants.hpp"

namespace simapp { namespace original {
// 45928c loads Global FCNS#2 before querying the named property. The
// constructor fallback is25; the shipped DVD record overrides it with50.
inline int DesktopFamilyFriendThreshold(const odh::NamedFloatConstants* tuning) {
    if(tuning)for(const auto& record:tuning->records)if(record.name=="friendship threshold") {
        const auto value=double(record.value);
        if(!std::isfinite(value)||value<std::numeric_limits<int>::min()||value>std::numeric_limits<int>::max())
            throw std::runtime_error("Friendship threshold exceeds integer bounds");
        return static_cast<int>(value); // DVD63604c truncates toward zero.
    }
    return 25;
}

// PC DVD 6e598c22: 427c21 counts external neighbour records once, using
// the mutual slot-0 predicate 42720f. TS1 PD61 is family number, not the
// inherited TSO enum named IncomingFriends. PD65 >= 2 denotes a pet.
inline int DesktopFamilyFriendCount(
        const FSO::Files::Formats::IFF::Chunks::FAMI& family,
        const FSO::Files::Formats::IFF::Chunks::NBRS& neighbors,int threshold) {
    constexpr int familyNumber=61,gender=65;
    auto relationship=[threshold](const auto& from,int16_t to) {
        const auto found=from.Relationships.find(to);
        return found!=from.Relationships.end()&&!found->second.empty()&&found->second[0]>=threshold;
    };
    int count=0;
    for(const auto& record:neighbors.Entries) {
        if(!record)continue;
        const auto& candidate=*record;
        if(candidate.PersonData.size()<=gender||candidate.PersonData[familyNumber]==0||
           candidate.PersonData[gender]>1||
           std::find(family.FamilyGUIDs.begin(),family.FamilyGUIDs.end(),candidate.GUID)!=family.FamilyGUIDs.end())continue;
        for(const auto guid:family.FamilyGUIDs) {
            const auto id=neighbors.DefaultNeighbourByGUID.find(guid);
            if(id==neighbors.DefaultNeighbourByGUID.end())continue;
            const auto member=neighbors.NeighbourByID.find(id->second);
            if(member!=neighbors.NeighbourByID.end()&&member->second&&
               relationship(candidate,member->second->NeighbourID)&&
               relationship(*member->second,candidate.NeighbourID)) {++count;break;}
        }
    }
    return count;
}
}}
