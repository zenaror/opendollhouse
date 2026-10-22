#pragma once
#include "odh/saved_world.hpp"
#include "resource.hpp"

struct SavedDefinitionSource {std::string path;bool standalone;};
inline std::unique_ptr<odh::DefinitionIndex> saved_definition_index(const std::vector<SavedDefinitionSource>& sources){
    if(sources.size()>16)throw odh::FormatError("definition source count exceeds limit");
    std::vector<odh::DefinitionCandidate> candidates;
    std::uint64_t available=256*1024;
    const auto add=[&](const odh::View& view,const std::string& path,const std::string& asset){
        odh::Limits limits;limits.iff_chunks=4096;
        odh::iff_chunks(view,[&](const odh::Chunk& chunk){
            if(chunk.type!="OBJD")return;
            if(candidates.size()>=8192||path.size()>available||asset.size()>available-path.size())
                throw odh::FormatError("definition candidate/provenance budget exceeded");
            auto definition=odh::read_object_definition(chunk.payload);
            available-=path.size()+asset.size();candidates.push_back({path,asset,chunk.id,definition});
        },limits);
    };
    for(const auto& source:sources){
        if(source.path.size()>4096)throw odh::FormatError("definition path exceeds limit");
        if(source.standalone){with_iff_file(source.path,[&](const auto& view){add(view,source.path,"");});continue;}
        std::ifstream stream(source.path,std::ios::binary);
        if(!stream)throw odh::FormatError("cannot open definition archive");
        const odh::View archive(stream,std::filesystem::file_size(source.path));
        odh::Limits limits;limits.far_entries=8192;limits.far_name_bytes=256*1024;
        for(const auto& entry:odh::far1_index(archive,limits)){
            // Definition archives may also contain strings, textures and other
            // non-IFF resources. Only the explicit IFF family is parsed; a
            // malformed .iff still fails, and duplicate GUIDs stay ambiguous.
            const auto extension=std::filesystem::path(entry.name).extension().string();
            std::string folded=extension;
            for(auto& c:folded)if(c>='A'&&c<='Z')c=static_cast<char>(c-'A'+'a');
            if(folded!=".iff")continue;
            add(archive.subview(entry.offset,entry.length),source.path,entry.name);
        }
    }
    return std::make_unique<odh::DefinitionIndex>(candidates);
}
inline std::vector<odh::SavedWorldEntity> saved_world_records(const std::string& path,const odh::DefinitionIndex& index,
    odh::Objt* output_types=nullptr){
    std::optional<odh::Objm> objects;std::optional<odh::Objt> types;
    with_iff_file(path,[&](const auto& view){odh::iff_chunks(view,[&](const auto& chunk){
        if(chunk.type=="ObjM"&&chunk.id==1){if(objects)throw odh::FormatError("duplicate ObjM1");objects=odh::read_objm(chunk.payload);}
        if(chunk.type=="objt"&&chunk.id==0){if(types)throw odh::FormatError("duplicate objt0");types=odh::read_objt(chunk.payload);}
    });});
    if(!objects||!types)throw odh::FormatError("ObjM1/objt0 absent for World import");
    auto prefixes=odh::read_saved_object_prefixes(*objects,*types);
    std::vector<odh::SavedWorldEntity> records;records.reserve(prefixes.size());
    std::uint64_t common_available=65536,tail_available=65536,strings_available=256*1024;
    for(std::size_t n=0;n<prefixes.size();++n){
        auto& prefix=prefixes[n];const auto* candidate=index.unique(prefix.guid);
        if(!candidate)throw odh::FormatError("saved World definition missing or ambiguous for object "+std::to_string(prefix.object_id));
        odh::ObjmCommonLimits common_limits;common_limits.decoded_words=common_available;
        auto common=odh::read_objm_common(objects->blocks[n],prefix.prefix,common_limits);common_available-=common.decoded_words;
        odh::ObjmTailLimits tail_limits;tail_limits.decoded_words=tail_available;tail_limits.string_bytes=strings_available;
        const auto* type=types->entry_for_type(prefix.type_id);
        auto tail=odh::read_objm_tail(objects->blocks[n],common,objects->version,candidate->definition,*type,tail_limits);
        tail_available-=tail.decoded_words;strings_available-=tail.string_bytes;
        records.push_back({std::move(prefix),std::move(common),std::move(tail),candidate->definition,type->metadata->objd_type});
    }
    if(output_types)*output_types=std::move(*types);
    return records;
}
inline odh::Nbrs saved_neighbors(const std::string& path){
    std::optional<odh::Nbrs> result;
    with_iff_file(path,[&](const auto& view){odh::iff_chunks(view,[&](const auto& chunk){
        if(chunk.type!="NBRS"||chunk.id!=1)return;
        if(result)throw odh::FormatError("duplicate NBRS1");
        result=odh::read_nbrs(chunk.payload);
    });});
    if(!result)throw odh::FormatError("NBRS1 absent");
    return std::move(*result);
}
