#pragma once
#include "odh/saved_architecture.hpp"
#include "odh/prepared_architecture.hpp"
#include "resource.hpp"
#include <set>
struct SavedArchitectureResource {
    odh::SavedArchitecture architecture;
    std::vector<std::pair<std::string,odh::ArchitectureMap>> maps;
};
// Simitone VMTS1ActivatorNew: Get<T>(1)?.Entries ?? Get<T>(0)?.Entries.
// Existence wins, including empty ID1. Other IDs are retained but not selected.
// Unlike IffFile.Get's first-match behavior, ambiguous selected IDs fail strictly.
inline const odh::ArchitectureMap* selected_architecture_map(
    const SavedArchitectureResource& resource,const std::string& type) {
    const odh::ArchitectureMap* preferred=nullptr,*fallback=nullptr;
    for(const auto& map:resource.maps)if(map.first==type){
        auto** selected=map.second.chunk_id==1?&preferred:map.second.chunk_id==0?&fallback:nullptr;
        if(selected){if(*selected)throw odh::FormatError("ambiguous selected architecture map ID");
            *selected=&map.second;}
    }
    return preferred?preferred:fallback;
}
inline SavedArchitectureResource saved_architecture_resource(const std::string& path) {
    std::vector<odh::SavedArchitectureArray> arrays;SavedArchitectureResource result;
    std::set<std::pair<std::string,std::uint16_t>> ids;std::uint64_t encoded=0,decoded=0;
    with_iff_file(path,[&](const auto& view){odh::Limits limits;limits.iff_chunks=4096;
        odh::iff_chunks(view,[&](const auto& chunk){
            if(chunk.type!="Arry"&&chunk.type!="ARRY"&&chunk.type!="FLRm"&&chunk.type!="WALm")return;
            const auto type=chunk.type=="ARRY"?"Arry":chunk.type;
            if(!ids.emplace(type,chunk.id).second)throw odh::FormatError("duplicate saved architecture chunk");
            if(arrays.size()+result.maps.size()>=64||chunk.payload.size()>1024*1024-encoded)
                throw odh::FormatError("saved architecture encoded/chunk budget exceeded");
            encoded+=chunk.payload.size();std::vector<std::uint8_t> bytes(static_cast<std::size_t>(chunk.payload.size()));
            chunk.payload.read(0,bytes.data(),bytes.size());
            if(type=="Arry"){
                odh::ArchitectureArrayLimits bounds;bounds.decoded_bytes=static_cast<std::size_t>(1024*1024-decoded);
                auto array=odh::read_architecture_array(bytes,bounds);decoded+=array.data.size();arrays.push_back({chunk.id,std::move(array)});
            }else result.maps.emplace_back(type,odh::read_architecture_map(bytes,chunk.id));
        },limits);});
    result.architecture=odh::interpret_saved_architecture(arrays);return result;
}

// Share the exact prepared material/terrain snapshot between saved-scene and
// the live lot renderer. Preparation validates the 64x64 source and crop; these
// optional terrain grids retain their original row-major coordinates.
inline odh::SavedArchitecture prepared_render_architecture(
    const odh::SavedArchitecture& raw,const odh::PreparedArchitecture& prepared) {
    if(!prepared.width||prepared.width!=prepared.height||prepared.width>raw.width||prepared.height>raw.height)
        throw odh::FormatError("prepared render dimensions outside source");
    odh::SavedArchitecture result;result.width=prepared.width;result.height=prepared.height;result.advanced=raw.advanced;
    for(std::size_t level=0;level<2;++level){
        result.levels[level].floors=prepared.levels[level].floors;
        result.levels[level].walls=prepared.levels[level].walls;
    }
    const auto crop=[&](const auto& source,auto& target){
        if(!source)return;
        if(source->size()!=std::size_t(raw.width)*raw.height)
            throw odh::FormatError("prepared render terrain extent mismatch");
        target.emplace();target->reserve(std::size_t(prepared.width)*prepared.height);
        for(std::uint32_t y=0;y<prepared.height;++y)for(std::uint32_t x=0;x<prepared.width;++x)
            target->push_back((*source)[std::size_t(y)*raw.width+x]);
    };
    crop(raw.altitude,result.altitude);crop(raw.grass,result.grass);crop(raw.target_grass,result.target_grass);
    // Prepared floors already include the overlays and any subsequent player edits.
    return result;
}
