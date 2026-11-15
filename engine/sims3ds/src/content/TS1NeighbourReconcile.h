/* SPDX-License-Identifier: MPL-2.0 */
// TS1(sims): FreeSO f86bbd34/a3c6c5aa TS1NeighbourProvider.AddMissingNeighbors,
// NBRS.GetFreeID/AddNeighbor. Scoped to canonical person resources: orphan user
// character files and another neighbourhood's cached GUIDs are not a roster.
#pragma once
#include "files/formats/iff/chunks/NBRS.h"
#include <algorithm>
#include <functional>
#include <exception>
#include <exception>
#include <limits>
#include <set>
#include <string>
#include <vector>
namespace FSO { namespace Content { namespace TS1 {
struct NeighbourTemplate { uint32_t GUID=0; std::string ResourceName; bool CanonicalPerson=false; };
using PrepareNeighbourData=std::function<bool(uint32_t,std::vector<int16_t>&,std::string&)>;
// No VM, disk, RNG or family mutation. A failed preparation publishes no record.
inline bool ReconcileMissingNeighbors(FSO::Files::Formats::IFF::Chunks::NBRS& registry,
        std::vector<NeighbourTemplate> candidates,const PrepareNeighbourData& prepare,
        size_t& added,std::string& error) {
    using FSO::Files::Formats::IFF::Chunks::Neighbour;
    added=0;error.clear();std::set<uint32_t> present;std::set<int16_t> reserved;
    for(const auto& n:registry.Entries)if(n){
        // Include inactive/ghost/unusual records: existence, not visibility or
        // PersonMode, prevents a template from recreating an existing identity.
        if(n->Unknown1!=0&&n->Unknown1!=1){error="The roster contains an unknown occupied slot marker";return false;}
        if(n->Unknown1==0)continue;
        present.insert(n->GUID);
        if(n->NeighbourID>0&&!reserved.insert(n->NeighbourID).second){error="Duplicate neighbour ID in roster";return false;}
    }
    for(const auto& entry:registry.NeighbourByID)if(entry.first>0)reserved.insert(entry.first);
    std::stable_sort(candidates.begin(),candidates.end(),[](const auto& a,const auto& b){return a.GUID<b.GUID;});
    std::vector<std::unique_ptr<Neighbour>> pending;
    for(const auto& candidate:candidates){
        if(!candidate.CanonicalPerson||!candidate.GUID||present.count(candidate.GUID))continue;
        if(!prepare){error="No person-data preparation service";return false;}
        int id=1;while(id<=std::numeric_limits<int16_t>::max()&&reserved.count(int16_t(id)))++id;
        if(id>std::numeric_limits<int16_t>::max()){error="No free neighbour ID";return false;}
        std::vector<int16_t> data;
        try {if(!prepare(candidate.GUID,data,error)||data.size()<88){if(error.empty())error="Could not prepare person data";return false;}}
        catch(const std::exception& e){error=std::string("Could not prepare person data: ")+e.what();return false;}
        auto n=std::make_unique<Neighbour>();n->NeighbourID=int16_t(id);n->GUID=candidate.GUID;
        n->Name=candidate.ResourceName;n->PersonMode=9;
        n->PersonData.assign(data.begin(),data.begin()+88);
        // Constructor supplies the version10/unknown9 and empty relationships
        // used by both references. Existing entries are never normalised.
        pending.push_back(std::move(n));reserved.insert(int16_t(id));present.insert(candidate.GUID);
    }
    for(auto& n:pending){auto raw=n.get();registry.NeighbourByID.emplace(raw->NeighbourID,raw);
        registry.DefaultNeighbourByGUID.emplace(raw->GUID,raw->NeighbourID);registry.Entries.push_back(std::move(n));++added;}
    if(added)std::stable_sort(registry.Entries.begin(),registry.Entries.end(),[](const auto& a,const auto& b){
        if(!a)return false;if(!b)return true;return a->NeighbourID<b->NeighbourID;});
    return true;
}
}}}
