#pragma once
#include "odh/architecture_resource_catalog.hpp"
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>

// Host-only reopenable resource cache support. No directory discovery, payload
// copies or filename re-resolution: a FAR winner uses its exact manifest index.
struct ArchitectureMaterialAsset {
    std::unique_ptr<std::ifstream> stream;
    std::vector<odh::Chunk> chunks;
    const odh::Chunk* find(const std::string& type,std::uint16_t id) const {
        const odh::Chunk* found=nullptr;
        for(const auto& chunk:chunks)if(chunk.type==type&&chunk.id==id){
            if(found)throw odh::FormatError("ambiguous architecture material chunk");
            found=&chunk;
        }
        return found;
    }
    const odh::View& require(const std::string& type,std::uint16_t id) const {
        const auto* found=find(type,id);
        if(!found)throw odh::FormatError("architecture material "+type+" missing for ID "+std::to_string(id));
        return found->payload;
    }
};
class ArchitectureMaterialResources {
public:
    explicit ArchitectureMaterialResources(const odh::ArchitectureResourceCatalog& catalog) {
        if(catalog.sources.size()>256||catalog.resources.size()>65536)
            throw odh::FormatError("architecture material catalog entry budget exceeded");
        std::size_t bytes=0;
        const auto charge=[&](const std::string& name){
            if(name.size()>4096||name.size()>1024*1024-bytes)
                throw odh::FormatError("architecture material catalog name budget exceeded");
            bytes+=name.size();
        };
        for(const auto& source:catalog.sources){charge(source.path);
            if(source.path.empty()||(source.kind!=odh::ArchitectureResourceSourceKind::loose_file&&
               source.kind!=odh::ArchitectureResourceSourceKind::far1_archive))
                throw odh::FormatError("architecture material source invalid");}
        for(const auto& record:catalog.resources){
            charge(record.catalog.name);charge(record.source.entry_name);
            if(record.catalog.id<256||record.catalog.id>65533||
               record.source.source_index>=catalog.sources.size()||
               (record.family!=odh::ArchitectureResourceFamily::floor&&record.family!=odh::ArchitectureResourceFamily::wall))
                throw odh::FormatError("architecture material catalog identity invalid");
            if(!records_.emplace(std::make_pair(record.family,record.catalog.id),record).second)
                throw odh::FormatError("ambiguous architecture material runtime ID");
        }
        sources_=catalog.sources;
    }
    const odh::ArchitectureResourceRecord& record(odh::ArchitectureResourceFamily family,std::uint16_t id) const {
        const auto found=records_.find({family,id});
        if(found==records_.end())throw odh::FormatError("architecture material runtime ID missing");
        return found->second;
    }
    const std::string& path(const odh::ArchitectureResourceRecord& record) const {
        return sources_.at(record.source.source_index).path;
    }
    ArchitectureMaterialAsset open(const odh::ArchitectureResourceRecord& record,std::uint32_t chunk_limit) const {
        const auto& source=sources_.at(record.source.source_index);
        ArchitectureMaterialAsset result;
        result.stream=std::make_unique<std::ifstream>(source.path,std::ios::binary);
        if(!*result.stream)throw odh::FormatError("architecture material open failure");
        odh::View view(*result.stream,std::filesystem::file_size(source.path));
        const auto& locator=record.source;
        if(source.kind==odh::ArchitectureResourceSourceKind::loose_file){
            if(locator.offset!=0||locator.entry_index!=0||locator.size!=view.size())
                throw odh::FormatError("architecture loose material locator changed");
        }else{
            odh::Limits limits;limits.far_entries=65536;limits.far_name_bytes=1024*1024;
            const auto entries=odh::far1_index(view,limits);
            if(locator.entry_index>=entries.size())throw odh::FormatError("architecture FAR material locator absent");
            const auto& entry=entries[locator.entry_index];
            if(entry.name!=locator.entry_name||entry.offset!=locator.offset||entry.length!=locator.size)
                throw odh::FormatError("architecture FAR material locator changed");
            view=view.subview(locator.offset,locator.size);
        }
        odh::Limits limits;limits.iff_chunks=chunk_limit;
        odh::iff_chunks(view,[&](const auto& chunk){result.chunks.push_back(chunk);},limits);
        return result;
    }
private:
    std::vector<odh::ArchitectureResourceSource> sources_;
    std::map<std::pair<odh::ArchitectureResourceFamily,std::uint16_t>,odh::ArchitectureResourceRecord> records_;
};
