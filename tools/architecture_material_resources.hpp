#pragma once
#include "odh/architecture_resource_catalog.hpp"
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>

struct ArchitectureMaterialCacheLimits {
    std::size_t manifest_entries=262144;
    std::size_t manifest_name_bytes=8*1024*1024;
};
struct ArchitectureMaterialCacheStats {
    std::size_t manifest_reads=0,manifest_hits=0;
    std::size_t retained_entries=0,retained_name_bytes=0;
};

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
    explicit ArchitectureMaterialResources(const odh::ArchitectureResourceCatalog& catalog,
        ArchitectureMaterialCacheLimits cache_limits={}):cache_limits_(cache_limits) {
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
    ArchitectureMaterialCacheStats cache_stats()const noexcept{return stats_;}
    ArchitectureMaterialAsset open(const odh::ArchitectureResourceRecord& record,std::uint32_t chunk_limit) const {
        const auto& source=sources_.at(record.source.source_index);
        const auto stamp=source_stamp(source.path);
        ArchitectureMaterialAsset result;
        result.stream=std::make_unique<std::ifstream>(source.path,std::ios::binary);
        if(!*result.stream)throw odh::FormatError("architecture material open failure");
        odh::View view(*result.stream,stamp.size);
        const auto& locator=record.source;
        if(source.kind==odh::ArchitectureResourceSourceKind::loose_file){
            if(locator.offset!=0||locator.entry_index!=0||locator.size!=view.size())
                throw odh::FormatError("architecture loose material locator changed");
        }else{
            const auto entries=manifest(locator.source_index,view,stamp);
            if(locator.entry_index>=entries->size())throw odh::FormatError("architecture FAR material locator absent");
            const auto& entry=entries->at(locator.entry_index);
            if(entry.name!=locator.entry_name||entry.offset!=locator.offset||entry.length!=locator.size)
                throw odh::FormatError("architecture FAR material locator changed");
            view=view.subview(locator.offset,locator.size);
        }
        odh::Limits limits;limits.iff_chunks=chunk_limit;
        odh::iff_chunks(view,[&](const auto& chunk){result.chunks.push_back(chunk);},limits);
        // The cache is an optimization of a reopenable resource, not an
        // assertion that external files are immutable. Recheck the source
        // after reading headers; a changed source must not publish stale views.
        if(!(source_stamp(source.path)==stamp)){
            discard_manifest(locator.source_index);
            throw odh::FormatError("architecture material changed while reading");
        }
        return result;
    }
private:
    struct SourceStamp {
        std::uintmax_t size;
        std::filesystem::file_time_type modified;
        bool operator==(const SourceStamp& other)const noexcept{
            return size==other.size&&modified==other.modified;
        }
    };
    struct Manifest {
        SourceStamp stamp;
        std::shared_ptr<const std::vector<odh::FarEntry>> entries;
        std::size_t name_bytes=0;
    };
    static SourceStamp source_stamp(const std::string& path){
        std::error_code error;
        const auto size=std::filesystem::file_size(path,error);
        if(error)throw odh::FormatError("architecture material size unavailable");
        const auto modified=std::filesystem::last_write_time(path,error);
        if(error)throw odh::FormatError("architecture material modification time unavailable");
        return {size,modified};
    }
    void discard_manifest(std::size_t source_index)const{
        const auto found=manifests_.find(source_index);
        if(found==manifests_.end())return;
        stats_.retained_entries-=found->second.entries->size();
        stats_.retained_name_bytes-=found->second.name_bytes;
        manifests_.erase(found);
    }
    std::shared_ptr<const std::vector<odh::FarEntry>> manifest(std::size_t source_index,const odh::View& view,
        const SourceStamp& stamp)const{
        auto found=manifests_.find(source_index);
        if(found!=manifests_.end()&&found->second.stamp==stamp){
            ++stats_.manifest_hits;
            return found->second.entries;
        }
        discard_manifest(source_index);
        odh::Limits limits;limits.far_entries=65536;limits.far_name_bytes=1024*1024;
        ++stats_.manifest_reads;
        auto entries=odh::far1_index(view,limits);
        std::size_t names=0;
        for(const auto& entry:entries)names+=entry.name.size();
        if(!(source_stamp(sources_.at(source_index).path)==stamp))
            throw odh::FormatError("architecture material changed while indexing");
        auto owner=std::make_shared<const std::vector<odh::FarEntry>>(std::move(entries));
        // Cache capacity must not narrow the material format/domain. The
        // original per-archive parser bounds still apply; an index too large
        // for this optional cache remains transient for this open only.
        if(owner->size()>cache_limits_.manifest_entries||names>cache_limits_.manifest_name_bytes)
            return owner;
        while(owner->size()>cache_limits_.manifest_entries-stats_.retained_entries||
              names>cache_limits_.manifest_name_bytes-stats_.retained_name_bytes)
            discard_manifest(manifests_.begin()->first);
        auto inserted=manifests_.emplace(source_index,Manifest{stamp,std::move(owner),names});
        stats_.retained_entries+=inserted.first->second.entries->size();
        stats_.retained_name_bytes+=names;
        return inserted.first->second.entries;
    }
    std::vector<odh::ArchitectureResourceSource> sources_;
    std::map<std::pair<odh::ArchitectureResourceFamily,std::uint16_t>,odh::ArchitectureResourceRecord> records_;
    ArchitectureMaterialCacheLimits cache_limits_;
    // Per-provider metadata only: no retained streams or material payloads.
    // Like View and the calling UI resource providers, not thread-safe.
    mutable std::map<std::size_t,Manifest> manifests_;
    mutable ArchitectureMaterialCacheStats stats_;
};
