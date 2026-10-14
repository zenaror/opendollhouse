#pragma once
#include "odh/saved_architecture.hpp"
#include "resource.hpp"
#include <set>
struct SavedArchitectureResource {
    odh::SavedArchitecture architecture;
    std::vector<std::pair<std::string,odh::ArchitectureMap>> maps;
};
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
