/* SPDX-License-Identifier: MPL-2.0 */
#include "odh/host_filesystem.hpp"
#pragma once
#include "app/front/NhoodModel.h"
#include "content/TS1NeighbourProvider.h"
#include "content/TS1LotLocationFiles.h"
#include "neighborhood_resources.hpp"

namespace simapp { namespace original {
// Reuse current-engine save/template precedence for the whole PC resource
// preparation service. A different neighbourhood remains an explicit read.
inline odh_host::PreparedNeighborhoodMap PrepareNeighborhoodMapResources(
    const front::NhoodModel& model,const std::filesystem::path& archive,
    unsigned mode,std::uint8_t language,FSO::Content::TS1::TS1NeighborhoodProvider& provider) {
    odh_host::NeighborhoodHouseSources sources;
    std::vector<std::string> locations{(std::filesystem::path(model.UserDir)/"LotLocations.iff").string()};
    if(provider.UserPath==model.UserDir){
        sources.resolve=[&provider](std::uint16_t house){return provider.GetHousePath(house);};
        for(const auto& directory:provider.TemplateDirs){
            std::error_code error;auto root=odh::hostfs::canonical(directory,error);
            if(!error)sources.roots.push_back(std::move(root));
        }
        locations=FSO::Content::TS1::LotLocationFiles(provider);
    }
    return odh_host::prepare_neighborhood_map_resources(model.UserDir,archive,mode,language,
        [&model](std::uint16_t house){return model.FamilyInHouse(house)!=nullptr;},locations,sources);
}
}}
