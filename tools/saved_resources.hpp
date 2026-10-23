#pragma once
#include "odh/saved_world.hpp"
#include "resource.hpp"

struct SavedDefinitionSource {std::string path;bool standalone;};
inline std::unique_ptr<odh::DefinitionIndex> saved_definition_index(const std::vector<SavedDefinitionSource>& sources){
    if(sources.size()>24)throw odh::FormatError("definition source count exceeds limit");
    std::vector<odh::DefinitionCandidate> candidates;
    std::uint64_t available=1024*1024;
    const auto add=[&](const odh::View& view,const std::string& path,const std::string& asset){
        odh::Limits limits;limits.iff_chunks=4096;
        odh::iff_chunks(view,[&](const odh::Chunk& chunk){
            if(chunk.type!="OBJD")return;
            if(candidates.size()>=8192)throw odh::FormatError("definition candidate budget exceeded");
            if(path.size()>available||asset.size()>available-path.size())
                throw odh::FormatError("definition provenance byte budget exceeded");
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
        if(!candidate){
            const auto* matches=index.candidates(prefix.guid);
            if(!matches)throw odh::FormatError("saved World definition missing for object "+
                std::to_string(prefix.object_id)+" GUID "+std::to_string(prefix.guid));
            std::string message="saved World definition ambiguous for object "+
                std::to_string(prefix.object_id)+" GUID "+std::to_string(prefix.guid)+
                " ("+std::to_string(matches->size())+" candidates):";
            for(const auto& match:*matches)
                message+=" "+match.source+"!"+match.asset+"#"+std::to_string(match.chunk_id);
            throw odh::FormatError(message);
        }
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
inline std::vector<odh::SavedWorldEntity> saved_world_records(const odh::Objm& objects,const odh::Objt& types,
    std::vector<odh::SavedObjectPrefix> prefixes,std::vector<odh::ObjmCommonState> common_states,
    std::vector<std::optional<odh::ObjmTailState>> tail_states,const odh::DefinitionIndex& index){
    if(prefixes.size()!=objects.blocks.size()||common_states.size()!=prefixes.size()||tail_states.size()!=prefixes.size())
        throw odh::FormatError("saved World decoded state count mismatch");
    std::vector<odh::SavedWorldEntity> records;records.reserve(prefixes.size());
    for(std::size_t n=0;n<prefixes.size();++n){
        auto& prefix=prefixes[n];const auto* candidate=index.unique(prefix.guid);
        if(!candidate){
            const auto* matches=index.candidates(prefix.guid);
            if(!matches)throw odh::FormatError("saved World definition missing for object "+
                std::to_string(prefix.object_id)+" GUID "+std::to_string(prefix.guid));
            std::string message="saved World definition ambiguous for object "+
                std::to_string(prefix.object_id)+" GUID "+std::to_string(prefix.guid)+
                " ("+std::to_string(matches->size())+" candidates):";
            for(const auto& match:*matches)
                message+=" "+match.source+"!"+match.asset+"#"+std::to_string(match.chunk_id);
            throw odh::FormatError(message);
        }
        if(!tail_states[n])throw odh::FormatError("saved World tail unavailable for object "+
            std::to_string(prefix.object_id)+" GUID "+std::to_string(prefix.guid));
        const auto* type=types.entry_for_type(prefix.type_id);
        if(!type||!type->metadata)throw odh::FormatError("saved World object type unavailable for object "+
            std::to_string(prefix.object_id)+" type "+std::to_string(prefix.type_id));
        records.push_back({std::move(prefix),std::move(common_states[n]),std::move(*tail_states[n]),
            candidate->definition,type->metadata->objd_type});
    }
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
