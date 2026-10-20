#pragma once
#include "saved_resources.hpp"
#include "odh/wall_appearance.hpp"
#include <map>
struct SavedWallAppearanceBinding {
    std::uint32_t guid;
    std::string source,asset;
    std::uint16_t chunk_id;
    odh::ObjdWallAppearance binding;
};
struct SavedWallAppearance {
    odh::WallAppearance appearance;
    std::vector<SavedWallAppearanceBinding> bindings;
    std::vector<odh::WallAppearanceInput> inputs;
};
inline SavedWallAppearance saved_wall_appearance(const odh::SavedWorld& saved,const odh::DefinitionIndex& index,const odh::SavedArchitecture& architecture,bool allow_removed=false){
    if(!saved.world||saved.records.size()>4096)throw odh::FormatError("wall appearance saved World/record budget invalid");
    SavedWallAppearance out;std::map<std::uint32_t,std::size_t> bindings;std::uint64_t names=0;
    for(const auto& raw:saved.records){const auto id=static_cast<std::int16_t>(raw.saved.object_id);const auto* e=saved.world->find(id);
        // Removed entities no longer contribute door/window appearance.
        if(!e&&allow_removed)continue;
        if(!e||!e->definition)throw odh::FormatError("wall appearance saved entity unavailable");
        const odh::VmFrame frame{odh::routine_key(4096),0,{id,id,id,0},{},{}};
        const auto flag=saved.world->read(frame,3,40),placement=saved.world->read(frame,3,13);
        if(!flag||!placement)throw odh::FormatError("wall appearance saved bank unavailable");
        odh::WallAppearanceInput input{id,e->generation,e->definition->guid,e->kind,e->position,e->attachment.has_value(),static_cast<std::uint16_t>(*flag),static_cast<std::uint16_t>(*placement),{}};
        if(e->kind==odh::EntityKind::object&&e->position&&!e->attachment&&(input.flags2&0xc000U)){
            auto found=bindings.find(input.guid);
            if(found==bindings.end()){
                if(bindings.size()>=256)throw odh::FormatError("wall appearance binding cache budget exceeded");
                const auto* candidate=index.unique(input.guid);if(!candidate)throw odh::FormatError("wall appearance concrete definition unavailable");
                if(candidate->source.size()+candidate->asset.size()>256*1024-names)throw odh::FormatError("wall appearance provenance budget exceeded");
                std::optional<odh::ObjdWallAppearance> binding;
                auto load=[&](const auto& v){odh::Limits limits;limits.iff_chunks=4096;odh::iff_chunks(v,[&](const auto& c){if(c.type=="OBJD"&&c.id==candidate->chunk_id){if(binding)throw odh::FormatError("duplicate wall appearance OBJD");binding=odh::read_objd_wall_appearance(c.payload);}},limits);};
                if(candidate->asset.empty())with_iff_file(candidate->source,load);else with_far_entry(candidate->source,candidate->asset,load);
                if(!binding||binding->guid!=input.guid)throw odh::FormatError("wall appearance OBJD identity mismatch");
                names+=candidate->source.size()+candidate->asset.size();const auto at=out.bindings.size();out.bindings.push_back({input.guid,candidate->source,candidate->asset,candidate->chunk_id,*binding});found=bindings.emplace(input.guid,at).first;
            }
            input.binding=out.bindings[found->second].binding;
        }
        out.inputs.push_back(input);
    }
    out.appearance=odh::derive_wall_appearance(architecture,out.inputs);return out;
}
